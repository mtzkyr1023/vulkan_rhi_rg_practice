
#include <algorithm>
#include "engine.h"

#include "rg/render_graph.h"

#include "imgui.h"
#include "backends/imgui_impl_win32.h"

#include <cstdlib>
#include <fstream>
#include <iterator>
#include <vector>
#include <string>

#include "game/sound_synth.h"

namespace
{
	// Order must match the MV_DEBUG_* defines in common.hlsli.
	const char* const kDebugModeNames[] =
	{
		"Shaded",
		"Draw ID",
		"Primitive ID",
		"Material ID",
		"Barycentrics",
		"UV",
		"Normal",
		"Mip level",
		"VT page",
		"VT level",
		"Cascade",
	};

	constexpr int kDebugModeCount = (int)(sizeof(kDebugModeNames) / sizeof(kDebugModeNames[0]));

	// The size of the per-frame scene constant buffer. A static_assert in tick() keeps the
	// struct that goes into it from outgrowing it.
	constexpr mv::types::u64 kSceneConstantsSize = 768;

	// Screen-space motion in UV. Two channels of half float: it needs sign and sub-pixel
	// precision, and never leaves the range of the screen.
	constexpr mv::rhi::ETextureFormat kVelocityFormat = mv::rhi::ETextureFormat::eR16G16_SFLOAT;

	// Must match DrawInfo in vb_shade.hlsl. At file scope because the table is rewritten
	// whenever the active scene changes, not only when it is first built.
	struct DrawInfo
	{
		mv::types::u32 firstIndex;
		mv::types::u32 materialIndex;
		mv::types::u32 pad[2];
	};

	// The per-draw table is allocated once at this many entries and rewritten in place on
	// a scene change, so the descriptor that names it never has to be replaced.
	constexpr mv::types::u32 kMaxDraws = 1024;

	// Shaders are copied next to the executable by the project's post-build step, so
	// resolving against the module path keeps the sample independent of the working
	// directory it happens to be launched from.
	std::string shaderPath(const char* name)
	{
		char modulePath[MAX_PATH]{};
		GetModuleFileNameA(nullptr, modulePath, MAX_PATH);

		std::string path = modulePath;
		const size_t slash = path.find_last_of('\\');
		path = (slash == std::string::npos) ? std::string() : path.substr(0, slash + 1);

		return path + "shaders\\" + name;
	}

	// The toroidal cell-to-slot map the sculpt streaming lives on.
	mv::types::u32 sculptSlotOf(mv::types::s32 cx, mv::types::s32 cz, mv::types::u32 side)
	{
		const auto wrap = [side](mv::types::s32 c)
		{
			return (mv::types::u32)(((c % (mv::types::s32)side) + (mv::types::s32)side) % (mv::types::s32)side);
		};

		return wrap(cz) * side + wrap(cx);
	}

	mv::types::u64 sculptCellKey(mv::types::s32 cx, mv::types::s32 cz)
	{
		return ((mv::types::u64)(mv::types::u32)cx << 32) | (mv::types::u64)(mv::types::u32)cz;
	}

	std::vector<mv::types::u32> readShaderFile(const char* name)
	{
		const std::string path = shaderPath(name);

		std::ifstream file(path, std::ios::binary | std::ios::ate);
		if (!file)
		{
			OutputDebugStringA(("Failed to open shader: " + path + "\n").c_str());
			return {};
		}

		const std::streamsize size = file.tellg();
		file.seekg(0, std::ios::beg);

		// SPIR-V is consumed as 32-bit words, so the storage is sized in words and
		// rounded up; DXIL only cares about the byte count reported alongside it.
		std::vector<mv::types::u32> data((size_t(size) + sizeof(mv::types::u32) - 1) / sizeof(mv::types::u32));
		file.read(reinterpret_cast<char*>(data.data()), size);

		return data;
	}
}

LRESULT CALLBACK WndProc(
    HWND hWnd,
    UINT msg,
    WPARAM wParam,
    LPARAM lParam);

bool Engine::initialize(HINSTANCE hInstance, int nCmdShow)
{
	if (!initializeWindow(hInstance, nCmdShow))
		return false;

	if (!initializeRHI(hwnd_))
		return false;

	return true;
}

void Engine::deinitialize()
{
	// ImGui and the material system own RHI resources, so they have to release them while
	// the device is still alive.
	if (rhi_) rhi_->waitIdle();
	audio_.deinitialize();
	physics_.deinitialize();
	physicsDebugRenderer_.deinitialize();
	hudRenderer_.deinitialize();
	skinnedPropRenderer_.deinitialize();
	for (auto& chunk : sculptChunks_)
		chunk.deinitialize();
	sculptGpu_.deinitialize();
	deinitializeImGui();
	postStack_.deinitialize();
	clouds_.deinitialize();
	environment_.deinitialize();
	terrain_.deinitialize();
	shadowMap_.deinitialize();
	virtualTextures_.deinitialize();
	materialSystem_.deinitialize();

	deinitializeRHI();
	deinitializeWindow();
}

void Engine::run()
{

    MSG msg{};
    while (true)
    {
        if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
		
		if (msg.message == WM_QUIT)
		{
			break;
		}
        else
        {
            tick();
        }
    }
}

bool Engine::initializeWindow(HINSTANCE hInstance, int nCmdShow)
{
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.hInstance = hInstance;
    wc.lpfnWndProc = WndProc;
    wc.lpszClassName = L"RenderGraphSample";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);

    RegisterClassExW(&wc);

    hwnd_ = CreateWindowExW(
        0,
        wc.lpszClassName,
        L"RenderGraph Sample",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        1280,
        720,
        nullptr,
        nullptr,
        hInstance,
        this);

    if (!hwnd_)
    {
        return false;
    }

    ShowWindow(hwnd_, nCmdShow);
    UpdateWindow(hwnd_);

	// The swap chain is sized from the client rect, not the 1280x720 window rect, so the
	// viewport has to follow the same measurement or the triangle is scaled wrong.
	RECT clientRect{};
	GetClientRect(hwnd_, &clientRect);
	width_ = (mv::types::u32)(clientRect.right - clientRect.left);
	height_ = (mv::types::u32)(clientRect.bottom - clientRect.top);

	return true;
}

bool Engine::initializeRHI(void* hwnd)
{
	rhi_ = useVulkan_ ? mv::rhi::IRHI::createVulkanRHI() : mv::rhi::IRHI::createDx12RHI();
	rhi_->initialize(hwnd);

	if (!initializeSceneResources())
		return false;

	if (!initializeImGui())
		return false;

	return true;
}

bool Engine::initializeShadows()
{
	if (!shadowMap_.initialize(rhi_))
		return false;

	// The cascades reach a fraction of the way across the scene. Covering all of Sponza
	// with four cascades would leave the near ones far coarser than they need to be.
	const mv::math::Vec3 extent = scene_->boundsMax - scene_->boundsMin;
	const float radius = std::sqrt(mv::math::dot(extent, extent)) * 0.5f;

	shadowDistance_ = radius * 1.2f;
	shadowMap_.setDistance(shadowDistance_);
	shadowMap_.setLambda(shadowLambda_);

	const std::vector<mv::types::u32> vsCode = readShaderFile(useVulkan_ ? "shadow_depth.vs.spv" : "shadow_depth.vs.cso");
	const std::vector<mv::types::u32> psCode = readShaderFile(useVulkan_ ? "shadow_depth.ps.spv" : "shadow_depth.ps.cso");

	if (vsCode.empty() || psCode.empty())
		return false;

	mv::rhi::ShaderDesc vsDesc{};
	vsDesc.stage = mv::rhi::EShaderType::eVertex;
	vsDesc.bytecode = vsCode.data();
	vsDesc.bytecodeSize = (mv::types::u32)(vsCode.size() * sizeof(mv::types::u32));
	vsDesc.entryPoint = "VSMain";

	mv::rhi::ShaderDesc psDesc{};
	psDesc.stage = mv::rhi::EShaderType::eFragment;
	psDesc.bytecode = psCode.data();
	psDesc.bytecodeSize = (mv::types::u32)(psCode.size() * sizeof(mv::types::u32));
	psDesc.entryPoint = "PSMain";

	mv::rhi::GraphicsPipelineDesc pipelineDesc{};
	pipelineDesc.vs = rhi_->createShader(vsDesc);
	pipelineDesc.ps = rhi_->createShader(psDesc);
	// The same layout the material passes use: the cascade matrices are in set 0 and the
	// alpha mask needs the material buffer and the bindless textures in set 1.
	pipelineDesc.layoutHandle = materialSystem_.pipelineLayout();

	pipelineDesc.vertexLayout.bindings.push_back({ .binding = 0, .stride = sizeof(mv::asset::ModelVertex), .perInstance = false });
	pipelineDesc.vertexLayout.attributes.push_back({
		.location = 0, .semanticName = "POSITION", .semanticIndex = 0,
		.binding = 0, .format = mv::rhi::EVertexFormat::eFloat3, .offset = (mv::types::u32)offsetof(mv::asset::ModelVertex, position) });
	pipelineDesc.vertexLayout.attributes.push_back({
		.location = 1, .semanticName = "NORMAL", .semanticIndex = 0,
		.binding = 0, .format = mv::rhi::EVertexFormat::eFloat3, .offset = (mv::types::u32)offsetof(mv::asset::ModelVertex, normal) });
	pipelineDesc.vertexLayout.attributes.push_back({
		.location = 2, .semanticName = "TEXCOORD", .semanticIndex = 0,
		.binding = 0, .format = mv::rhi::EVertexFormat::eFloat2, .offset = (mv::types::u32)offsetof(mv::asset::ModelVertex, uv) });

	pipelineDesc.depth.depthTestEnable = true;
	pipelineDesc.depth.depthWriteEnable = true;
	pipelineDesc.depth.depthCompareOp = mv::rhi::ECompareOp::eLessEqual;

	// No culling. Front-face culling is the usual trick for hiding acne, but Sponza's
	// foliage and curtains are single-sided geometry that would then cast nothing.
	pipelineDesc.rasterizer.cullMode = mv::rhi::ECullMode::eNone;

	// Slope-scaled bias does most of the work: the error is largest where a surface is
	// nearly edge-on to the light, which is exactly what the slope term measures.
	pipelineDesc.rasterizer.depthBiasConstant = 2.0f;
	pipelineDesc.rasterizer.depthBiasSlope = 3.0f;

	// Depth only. No colour target at all, which is what makes this cheap.
	pipelineDesc.depthFormat = mv::rhi::ETextureFormat::eD32_SFLOAT;

	shadowPipeline_ = rhi_->createGraphicsPipeline(pipelineDesc);

	return shadowPipeline_ != mv::INVALID_HANDLE;
}

bool Engine::initializeEnvironment()
{
	if (!environment_.initialize(rhi_, environmentBaker_.isReady() ? &environmentBaker_ : nullptr))
		return false;

	const std::vector<mv::types::u32> vsCode = readShaderFile(useVulkan_ ? "skybox.vs.spv" : "skybox.vs.cso");
	const std::vector<mv::types::u32> psCode = readShaderFile(useVulkan_ ? "skybox.ps.spv" : "skybox.ps.cso");

	if (vsCode.empty() || psCode.empty())
		return false;

	mv::rhi::ShaderDesc vsDesc{};
	vsDesc.stage = mv::rhi::EShaderType::eVertex;
	vsDesc.bytecode = vsCode.data();
	vsDesc.bytecodeSize = (mv::types::u32)(vsCode.size() * sizeof(mv::types::u32));
	vsDesc.entryPoint = "VSMain";

	mv::rhi::ShaderDesc psDesc{};
	psDesc.stage = mv::rhi::EShaderType::eFragment;
	psDesc.bytecode = psCode.data();
	psDesc.bytecodeSize = (mv::types::u32)(psCode.size() * sizeof(mv::types::u32));
	psDesc.entryPoint = "PSMain";

	mv::rhi::GraphicsPipelineDesc pipelineDesc{};
	pipelineDesc.vs = rhi_->createShader(vsDesc);
	pipelineDesc.ps = rhi_->createShader(psDesc);
	pipelineDesc.layoutHandle = materialSystem_.pipelineLayout();

	// A fullscreen triangle with no vertex buffer and no depth: it runs first and whatever
	// is drawn afterwards simply covers it.
	pipelineDesc.rasterizer.cullMode = mv::rhi::ECullMode::eNone;
	pipelineDesc.depth.depthTestEnable = false;
	pipelineDesc.depth.depthWriteEnable = false;

	// The scene renders into the post-process chain, not the backbuffer.
	// Two targets: shaded colour and screen-space velocity.
	pipelineDesc.colorFormats.push_back(mv::post::kChainFormat);
	pipelineDesc.colorFormats.push_back(kVelocityFormat);

	skyboxPipeline_ = rhi_->createGraphicsPipeline(pipelineDesc);

	return skyboxPipeline_ != mv::INVALID_HANDLE;
}

void Engine::applyPendingResize()
{
	// Minimising sends a size of zero, which nothing can be built at. The pending size is
	// left in place so the restore is picked up.
	if (pendingWidth_ == 0 || pendingHeight_ == 0)
		return;

	if (pendingWidth_ == width_ && pendingHeight_ == height_)
		return;

	// Everything below replaces resources the GPU may still be reading, and rewrites
	// descriptors that frames in flight may still be pointing at. Nothing here is safe
	// until the device is idle.
	rhi_->waitIdle();

	rhi_->resize(pendingWidth_, pendingHeight_);

	width_ = pendingWidth_;
	height_ = pendingHeight_;

	// The visibility buffer is named by one descriptor in a bind group that also holds the
	// geometry buffers, so the texture is replaced and that one slot re-pointed rather than
	// the whole group rebuilt.
	{
		rhi_->freeImage(visibilityTexture_);

		mv::rhi::TextureDesc visibilityDesc{};
		visibilityDesc.width = width_;
		visibilityDesc.height = height_;
		visibilityDesc.depth = 1;
		visibilityDesc.usage = mv::rhi::ETextureUsage::eColorAttachment | mv::rhi::ETextureUsage::eSampled;
		visibilityDesc.mipLevels = 1;
		visibilityDesc.format = mv::rhi::ETextureFormat::eR32G32_UINT;
		visibilityDesc.memoryType = mv::rhi::EMemoryType::eDeviceLocalImage;

		visibilityTexture_ = rhi_->createTexture(visibilityDesc);

		rhi_->updateBindGroupTexture(vbResourceGroup_, 3, 0, visibilityTexture_);
	}

	{
		rhi_->freeImage(velocityTexture_);

		mv::rhi::TextureDesc velocityDesc{};
		velocityDesc.width = width_;
		velocityDesc.height = height_;
		velocityDesc.depth = 1;
		velocityDesc.usage = mv::rhi::ETextureUsage::eColorAttachment | mv::rhi::ETextureUsage::eSampled;
		velocityDesc.mipLevels = 1;
		velocityDesc.format = kVelocityFormat;
		velocityDesc.memoryType = mv::rhi::EMemoryType::eDeviceLocalImage;

		velocityTexture_ = rhi_->createTexture(velocityDesc);

		// Before the chain rebuilds, because the temporal pass names it in its bind groups.
		if (taa_)
		{
			taa_->setVelocityTexture(velocityTexture_);
		}
	}

	{
		rhi_->freeImage(sceneDepthTexture_);

		mv::rhi::TextureDesc depthDesc{};
		depthDesc.width = width_;
		depthDesc.height = height_;
		depthDesc.depth = 1;
		depthDesc.usage = mv::rhi::ETextureUsage::eDepthStencilAttachment | mv::rhi::ETextureUsage::eSampled;
		depthDesc.mipLevels = 1;
		depthDesc.format = mv::rhi::ETextureFormat::eD32_SFLOAT;
		depthDesc.memoryType = mv::rhi::EMemoryType::eDeviceLocalImage;

		sceneDepthTexture_ = rhi_->createTexture(depthDesc);
	}

	postStack_.resize(width_, height_);
	clouds_.resize(width_, height_);
	water_.resize(width_, height_);

	// The depth target is a graph transient, so it follows width_ and height_ on its own the
	// next time the graph is built.
}

bool Engine::initializePostProcess()
{
	mv::post::PostProcessStack::Desc desc{};
	desc.width = width_;
	desc.height = height_;
	desc.backbufferFormat = rhi_->backbufferFormat();
	desc.sceneLayout = materialSystem_.sceneLayout();
	desc.bindlessLayout = materialSystem_.bindlessLayout();
	desc.loadShader = [this](const char* name)
	{
		return readShaderFile((std::string(name) + (useVulkan_ ? ".spv" : ".cso")).c_str());
	};

	if (!postStack_.initialize(rhi_, desc))
		return false;

	// Order is the whole point of a chain. Everything before the tone map works on
	// half-float radiance; everything after works on what a display can show.
	{
		auto taa = std::make_unique<mv::post::TemporalAntiAliasing>();
		taa_ = taa.get();
		taa_->setVelocityTexture(velocityTexture_);
		postStack_.add(std::move(taa));
	}

	// Bloom belongs before the tone map, because what it looks for is values above one and
	// the tone map is precisely what removes those.
	postStack_.add(std::make_unique<mv::post::Bloom>());

	postStack_.add(std::make_unique<mv::post::Tonemap>());

	// And these after, because both judge the image the way an eye would: FXAA decides
	// what an edge is from perceived luminance, and a vignette darkens display values.
	postStack_.add(std::make_unique<mv::post::Fxaa>());
	postStack_.add(std::make_unique<mv::post::LensDistortion>());

	{
		// An effect whose shaders failed to load is dropped rather than added, so the count
		// is worth stating: a short chain is the symptom.
		std::string message = "Post process chain:";
		for (const auto& effect : postStack_.effects())
		{
			message += " ";
			message += effect->name();
			message += " ->";
		}
		message += " backbuffer\n";

		OutputDebugStringA(message.c_str());
	}

	return true;
}

bool Engine::initializeImGui()
{
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGui::StyleColorsDark();

	// No imgui.ini: a stale saved layout would keep sizing windows for whatever the UI
	// looked like on an earlier run.
	ImGui::GetIO().IniFilename = nullptr;

	// Platform/input only. Drawing goes through our own RHI-based renderer so both
	// backends share one rendering path.
	if (!ImGui_ImplWin32_Init(hwnd_))
		return false;

	const std::vector<mv::types::u32> vsCode = readShaderFile(useVulkan_ ? "imgui.vs.spv" : "imgui.vs.cso");
	const std::vector<mv::types::u32> psCode = readShaderFile(useVulkan_ ? "imgui.ps.spv" : "imgui.ps.cso");

	if (vsCode.empty() || psCode.empty())
		return false;

	const mv::ui::ImGuiRenderer::ShaderCode vs{ vsCode.data(), (mv::types::u32)(vsCode.size() * sizeof(mv::types::u32)) };
	const mv::ui::ImGuiRenderer::ShaderCode ps{ psCode.data(), (mv::types::u32)(psCode.size() * sizeof(mv::types::u32)) };

	// The UI is recorded into the same render pass as the model, so it has to declare the
	// same depth format even though it never depth tests.
	// No depth: the UI is drawn in its own pass on the backbuffer, after the post-process
	// chain has finished with it.
	return imguiRenderer_.initialize(rhi_, vs, ps, mv::rhi::ETextureFormat::eUndefined);
}

void Engine::deinitializeImGui()
{
	if (!ImGui::GetCurrentContext())
		return;

	imguiRenderer_.deinitialize();

	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();
}

void Engine::frameModel()
{
	const mv::math::Vec3 center
	{
		(scene_->boundsMin.x + scene_->boundsMax.x) * 0.5f,
		(scene_->boundsMin.y + scene_->boundsMax.y) * 0.5f,
		(scene_->boundsMin.z + scene_->boundsMax.z) * 0.5f,
	};

	const mv::math::Vec3 extent = scene_->boundsMax - scene_->boundsMin;
	const float radius = std::sqrt(mv::math::dot(extent, extent)) * 0.5f;

	// Back off along +Z looking at the centre, and scale the near/far planes and the move
	// speed to the model so this works for anything from a helmet to a cathedral.
	cameraPosition_ = { center.x, center.y, center.z + radius * 0.8f };
	cameraYaw_ = 0.0f;
	cameraPitch_ = 0.0f;

	cameraSpeed_ = radius * 0.4f;
	cameraNear_ = radius * 0.005f;
	cameraFar_ = radius * 10.0f;
}

void Engine::surveyTerrain()
{
	const mv::math::Vec3 extent = scene_->boundsMax - scene_->boundsMin;
	const float radius = std::sqrt(mv::math::dot(extent, extent)) * 0.5f;

	// Outside one corner and well above the peaks, aimed at a point a little above the
	// middle of the map. Framing the bounding sphere the way frameModel does would put the
	// camera so far out that the relief flattens; this keeps it close enough to read.
	const mv::math::Vec3 target{ 0.0f, terrain_.desc().heightScale * 0.35f, 0.0f };

	cameraPosition_ =
	{
		-extent.x * 0.62f,
		terrain_.desc().heightScale * 2.1f,
		-extent.z * 0.62f,
	};

	const mv::math::Vec3 toTarget = target - cameraPosition_;
	const float horizontal = std::sqrt(toTarget.x * toTarget.x + toTarget.z * toTarget.z);

	// Inverting the forward vector the camera builds from these: x is sin(yaw) and z is
	// -cos(yaw), so the yaw that looks along (x, z) is atan2(x, -z).
	cameraYaw_ = std::atan2(toTarget.x, -toTarget.z);
	cameraPitch_ = std::atan2(toTarget.y, horizontal);

	cameraSpeed_ = radius * 0.25f;

	// The far plane has to clear the diagonal from a corner, or the far side of the
	// terrain is clipped away exactly where the view is most worth having. The near plane
	// is scaled with it rather than pinned: a quarter of a metre against a far plane ten
	// kilometres out spends almost the whole depth buffer on the first few metres, and
	// what is left is not enough to keep a distant ridge from fighting itself.
	cameraFar_ = radius * 8.0f;
	cameraNear_ = (std::max)(0.25f, cameraFar_ * 0.0002f);

	// The shadow distance is not touched here: activateScene already scales it to the
	// scene, and a second policy in the survey view is how the two ended up disagreeing --
	// this one asked for six hundred metres and activateScene clamped it back to four
	// hundred, and neither reached the far shore.
}

void Engine::activateScene()
{
	// The buffers about to be unbound are the ones frames in flight are still reading.
	rhi_->waitIdle();

	scene_ = terrainScene_ ? &terrain_.model() : &model_;

	std::vector<DrawInfo> draws;
	draws.reserve(scene_->primitives.size());

	for (const auto& primitive : scene_->primitives)
	{
		draws.push_back({ primitive.firstIndex, primitive.material, { 0, 0 } });
	}

	if (draws.size() > kMaxDraws)
		draws.resize(kMaxDraws);

	rhi_->writeBuffer(drawInfoBuffer_, draws.data(), draws.size() * sizeof(DrawInfo), 0);

	// Bindings 0 and 1 are what the resolve pass refetches vertices and indices through.
	// The visibility buffer holds nothing but ids, so these have to name the same geometry
	// the id pass rasterised or every triangle resolves to the wrong surface.
	rhi_->updateBindGroupBuffer(vbResourceGroup_, 0, scene_->vertexBuffer, 0, sizeof(mv::asset::ModelVertex), scene_->vertexCount);
	rhi_->updateBindGroupBuffer(vbResourceGroup_, 1, scene_->indexBuffer, 0, sizeof(mv::types::u32), scene_->indexCount);

	// Everything derived from the scene's size: the cascade fit, the camera speed and the
	// near and far planes all follow the bounds.
	const mv::math::Vec3 extent = scene_->boundsMax - scene_->boundsMin;
	const float radius = std::sqrt(mv::math::dot(extent, extent)) * 0.5f;

	// 2.4 radii is what it takes to reach the far corner of the map from a camera standing
	// just outside the near one, which is where the survey view puts it. Uncapped: the old
	// 400 m ceiling was tuned for Sponza-sized scenes, and on a two-kilometre terrain it
	// quietly turned the sun shadows off for everything past the camera's feet -- the
	// mountains lit as though nothing ever stood between them and the sun.
	shadowDistance_ = radius * 2.4f;
	shadowMap_.setDistance(shadowDistance_);

	// The history describes a scene that is no longer on screen, and reprojecting into it
	// would smear the old one across the new one for as long as it takes to converge.
	if (taa_ != nullptr)
		taa_->reset();
}

void Engine::rebuildTerrain()
{
	// The terrain's buffers and baked maps are about to be replaced.
	rhi_->waitIdle();

	terrain_.build(terrainNoise_, terrainDesc_);

	uploadGrassHeightField();

	if (terrainScene_)
		activateScene();
}

void Engine::uploadGrassHeightField()
{
	if (!grass_.isReady())
		return;

	const auto& heightmap = terrain_.heightmap();

	if (heightmap.heights.empty() || heightmap.width == 0)
		return;

	const mv::types::u32 resolution = heightmap.width;
	const mv::types::u64 size = (mv::types::u64)resolution * resolution * sizeof(float);

	// Both call sites idle the device to replace the terrain, so replacing this buffer
	// and re-pointing the grass descriptor at it is safe here too.
	if (resolution != grassHeightResolution_)
	{
		if (grassHeightBuffer_ != mv::INVALID_HANDLE)
			rhi_->releaseBuffer(grassHeightBuffer_);

		mv::rhi::BufferDesc desc{};
		desc.size = size;
		desc.usage = mv::rhi::EBufferUsage::eStorage;
		desc.memoryType = mv::rhi::EMemoryType::eHostVisibleBuffer;

		grassHeightBuffer_ = rhi_->createBuffer(desc);
		grassHeightResolution_ = resolution;
	}

	rhi_->writeBuffer(grassHeightBuffer_, heightmap.heights.data(), size, 0);

	grass_.setHeightField(grassHeightBuffer_, resolution, terrainDesc_.worldSize, terrainDesc_.heightScale);

	// The physics world gets the same heights as a Bullet heightfield body, referencing
	// the heightmap's own storage -- recreated here precisely because a rebuild may have
	// moved that storage.
	if (physics_.isReady())
	{
		physics_.setHeightField(heightmap.heights.data(), resolution, terrainDesc_.worldSize, terrainDesc_.heightScale);

		// The ray-only water plane rides along: rebuilt whenever the terrain is, and
		// again from the water tab when only the level moves.
		physics_.setWaterPlane(waterParams_.level);
	}

	// The gameplay ground reads the same heightmap through its own sampler, so the
	// character walks exactly the surface the mesh shows. The pointer stays valid across
	// rebuilds -- the heightmap is a member of terrain_, only its contents change.
	{
		const mv::terrain::Heightmap* map = &terrain_.heightmap();
		const float worldSize = terrainDesc_.worldSize;
		const float heightScale = terrainDesc_.heightScale;
		const float quads = (float)(resolution > 1 ? resolution - 1 : 1);

		groundField_.set(
			[map, worldSize, heightScale, quads](float x, float z)
			{
				// Triangle-exact, not bilinear: the render mesh interpolates its
				// vertex heights linearly across two triangles per cell, split on
				// the (i+1,j)-(i,j+1) diagonal (see terrain_mesh.hlsl). A bilinear
				// patch agrees with that surface only on the cell edges; mid-cell it
				// bows away by enough to sink a fox's paws. So: the four corner
				// heights through the same sampler the mesh vertices used, then the
				// same plane the rasteriser walks.
				const float gx = std::clamp((x / worldSize + 0.5f) * quads, 0.0f, quads);
				const float gz = std::clamp((z / worldSize + 0.5f) * quads, 0.0f, quads);

				const float cellX = (std::min)(std::floor(gx), quads - 1.0f);
				const float cellZ = (std::min)(std::floor(gz), quads - 1.0f);

				const float fx = gx - cellX;
				const float fz = gz - cellZ;

				const float h00 = map->sample(cellX / quads, cellZ / quads);
				const float h10 = map->sample((cellX + 1.0f) / quads, cellZ / quads);
				const float h01 = map->sample(cellX / quads, (cellZ + 1.0f) / quads);
				const float h11 = map->sample((cellX + 1.0f) / quads, (cellZ + 1.0f) / quads);

				const float h = (fx + fz <= 1.0f)
					? h00 + (h10 - h00) * fx + (h01 - h00) * fz
					: h11 + (h01 - h11) * (1.0f - fx) + (h10 - h11) * (1.0f - fz);

				return h * heightScale;
			},
			worldSize / quads);
	}
}

void Engine::enterScene()
{
	const mv::math::Vec3 center
	{
		(scene_->boundsMin.x + scene_->boundsMax.x) * 0.5f,
		(scene_->boundsMin.y + scene_->boundsMax.y) * 0.5f,
		(scene_->boundsMin.z + scene_->boundsMax.z) * 0.5f,
	};

	const mv::math::Vec3 extent = scene_->boundsMax - scene_->boundsMin;
	const float radius = std::sqrt(mv::math::dot(extent, extent)) * 0.5f;

	// Eye height a little above the floor, looking down whichever horizontal axis is
	// longer, which for a hall is along its length.
	float eyeHeight = scene_->boundsMin.y + extent.y * 0.2f;

	// On the terrain that floor is often the lake bed, and a fifth of the way up from it is
	// under water. Standing on the shore is both the view worth having and the one that is
	// not immediately confusing.
	if (terrainScene_ && waterParams_.enabled && eyeHeight < waterParams_.level)
		eyeHeight = waterParams_.level + extent.y * 0.05f;

	cameraPosition_ = { center.x, eyeHeight, center.z };
	cameraPitch_ = 0.0f;
	cameraYaw_ = (extent.x > extent.z) ? 1.5708f : 0.0f;

	cameraSpeed_ = radius * 0.4f;
	cameraNear_ = radius * 0.005f;
	cameraFar_ = radius * 10.0f;
}

void Engine::startPlay()
{
	if (!terrainScene_ || !groundField_.valid())
		return;

	if (!gameState_.transition(mv::game::EGameState::ePlaying))
		return;

	// Entering from the title: drop the character to the ground under the camera.
	// A teleport, not a fall from wherever the fly camera happened to hover.
	// Resuming from pause keeps the capsule exactly where it stood.
	mv::math::Vec3 feet = cameraPosition_;
	feet.y = groundField_.heightAt(feet.x, feet.z);

	player_.teleport(feet, physics_);
	prevPlayerPosition_ = feet;
	prevGrounded_ = false;
	footstepDistance_ = 0.0f;
	jumpQueued_ = false;

	// The fox re-anchors ahead of wherever play begins, so the one animated thing
	// in the world is in front of the player, not a hike away.
	foxAnchor_ = {
		feet.x + std::sin(cameraYaw_) * 10.0f,
		0.0f,
		feet.z - std::cos(cameraYaw_) * 10.0f };

	// The carvable region is laid over the terrain around the first play spawn:
	// a grid of chunks, each initialised from the heightfield with a small lift so
	// the voxel skin sits just proud of the terrain and wins the depth test. Once
	// only -- the edits are the player's work, and re-entering must not erase them.
	if (sculptGpu_.isReady() && !sculptPlaced_)
	{
		using mv::voxel::SculptVolume;

		const float chunkExtent = (float)SculptVolume::kCells * SculptVolume::kCellSize;
		const mv::types::u32 chunkCount = kSculptChunksPerSide * kSculptChunksPerSide;

		sculptChunks_.resize(chunkCount);
		sculptChunkDirty_.assign(chunkCount, false);
		sculptSlotStates_.assign(chunkCount, {});

		for (mv::types::u32 i = 0; i < chunkCount; i++)
			sculptGpu_.addChunk();

		// The initial window fills synchronously -- a one-time cost on entering
		// play. From here on the streaming below refills one chunk per frame.
		const mv::types::s32 playerCellX = (mv::types::s32)std::floor(feet.x / chunkExtent);
		const mv::types::s32 playerCellZ = (mv::types::s32)std::floor(feet.z / chunkExtent);

		const mv::types::s32 reach = (mv::types::s32)kSculptChunksPerSide / 2;

		for (mv::types::s32 dz = -reach; dz <= reach; dz++)
		{
			for (mv::types::s32 dx = -reach; dx <= reach; dx++)
			{
				const mv::types::s32 cx = playerCellX + dx;
				const mv::types::s32 cz = playerCellZ + dz;

				refillSculptSlot(sculptSlotOf(cx, cz, kSculptChunksPerSide), cx, cz);
			}
		}

		sculptPlaced_ = true;

		char message[128];
		sprintf_s(message, "Sculpt: %u streaming chunks, window %.0f m\n",
			chunkCount, (float)kSculptChunksPerSide * chunkExtent);
		OutputDebugStringA(message);
	}
}

void Engine::refillSculptSlot(mv::types::u32 slot, mv::types::s32 cx, mv::types::s32 cz)
{
	using mv::voxel::SculptVolume;

	const float chunkExtent = (float)SculptVolume::kCells * SculptVolume::kCellSize;

	SculptSlotState& state = sculptSlotStates_[slot];
	SculptVolume& chunk = sculptChunks_[slot];

	if (!chunk.isReady())
		chunk.initializeData();

	// The outgoing cell's edits are stashed before the density is overwritten, so
	// a carved cave survives leaving and re-entering the window.
	if (state.everEdited)
	{
		sculptSavedDensity_[sculptCellKey(state.cx, state.cz)].assign(
			chunk.densityData(), chunk.densityData() + chunk.cornerCount());
	}

	const float originX = (float)cx * chunkExtent;
	const float originZ = (float)cz * chunkExtent;

	const auto ground = [this](float x, float z) { return groundField_.heightAt(x, z); };

	// The chunk's vertical window has to hold the ground across its footprint:
	// sampled at the corners and centre, opened a few metres down for digging and
	// the rest up for building. Deterministic, so a restored cell lands exactly
	// where it was saved from.
	float lowest = 1e9f;
	float highest = -1e9f;

	const float samples[5][2] = {
		{ originX, originZ },
		{ originX + chunkExtent, originZ },
		{ originX, originZ + chunkExtent },
		{ originX + chunkExtent, originZ + chunkExtent },
		{ originX + chunkExtent * 0.5f, originZ + chunkExtent * 0.5f } };

	for (const auto& s : samples)
	{
		const float h = ground(s[0], s[1]);
		lowest = (std::min)(lowest, h);
		highest = (std::max)(highest, h);
	}

	const float originY = (std::max)(lowest - 6.0f, highest + 4.5f - chunkExtent);
	const mv::math::Vec3 origin{ originX, originY, originZ };

	const auto saved = sculptSavedDensity_.find(sculptCellKey(cx, cz));

	if (saved != sculptSavedDensity_.end())
	{
		chunk.restoreDensity(origin, saved->second);
		state.everEdited = true;
	}
	else
	{
		// A 2.5 m rock cap over the terrain rather than a thin skin: thick enough
		// that digging opens real tunnels before the buried terrain mesh (which
		// cannot be masked) shows through as a floor.
		chunk.placeFromGround(origin, 2.5f, ground);
		state.everEdited = false;
	}

	sculptGpu_.updateDensity(
		slot,
		chunk.densityData(), chunk.cornerCount(), chunk.origin(),
		SculptVolume::kCellSize, SculptVolume::kCells);

	physics_.setSculptMesh(
		slot,
		chunk.trianglePositions(),
		chunk.triangleVertexCount(),
		SculptVolume::positionStride());

	sculptChunkDirty_[slot] = false;
	state.cx = cx;
	state.cz = cz;
}

void Engine::exitToTitle()
{
	gameState_.transition(mv::game::EGameState::eTitle);
}

void Engine::buildHud()
{
	hudRenderer_.begin(width_, height_);

	// begin() cleared the queue, so bailing here draws a clean nothing.
	if (!hudEnabled_ || !hudRenderer_.isReady())
		return;

	const float w = (float)width_;
	const float h = (float)height_;
	const float stateTime = gameState_.timeInState();

	// Colours are 0xAABBGGRR, R lowest -- the byte order the vertex attribute reads.
	constexpr mv::types::u32 kWhite = 0xFFFFFFFFu;
	constexpr mv::types::u32 kShadow = 0x90000000u;

	const auto centeredText = [&](float y, float scale, mv::types::u32 color, const char* string)
	{
		const float x = (w - mv::hud::HudRenderer::textWidth(scale, string)) * 0.5f;

		hudRenderer_.text(x + scale, y + scale, scale, kShadow, string);
		hudRenderer_.text(x, y, scale, color, string);
	};

	if (gameState_.is(mv::game::EGameState::eTitle))
	{
		centeredText(h * 0.2f, 7.0f, kWhite, "PROJ001");
		centeredText(h * 0.2f + 64.0f, 2.0f, 0xFF80E8FFu, "A RENDERER LEARNS TO BE A GAME");

		if (terrainScene_ && groundField_.valid())
		{
			// The classic pulse: a prompt that breathes reads as waiting, one that
			// stands still reads as broken.
			const float pulse = 0.55f + 0.45f * std::sin(stateTime * 3.0f);
			const mv::types::u32 alpha = (mv::types::u32)(pulse * 255.0f) << 24;

			centeredText(h * 0.62f, 3.0f, alpha | 0x00FFFFFFu, "PRESS ENTER TO PLAY");
		}
		else
		{
			centeredText(h * 0.62f, 2.0f, 0xA0FFFFFFu, "ENTER THE TERRAIN SCENE TO PLAY");
		}
	}
	else if (gameState_.is(mv::game::EGameState::ePlaying))
	{
		// The crosshair: two strokes with a breathing gap, cheaper to read against
		// any background than a dot.
		const float cx = w * 0.5f;
		const float cy = h * 0.5f;

		hudRenderer_.rect(cx - 10.0f, cy - 1.0f, 7.0f, 2.0f, 0xC8FFFFFFu);
		hudRenderer_.rect(cx + 3.0f, cy - 1.0f, 7.0f, 2.0f, 0xC8FFFFFFu);
		hudRenderer_.rect(cx - 1.0f, cy - 10.0f, 2.0f, 7.0f, 0xC8FFFFFFu);
		hudRenderer_.rect(cx - 1.0f, cy + 3.0f, 2.0f, 7.0f, 0xC8FFFFFFu);

		// What the crosshair is on, right where the eye already is.
		if (aimValid_)
		{
			const char* kindNames[] = { "", "TERRAIN", "WATER", "PROP", "CHARACTER", "SCULPT" };
			const mv::types::u32 kind = (mv::types::u32)aimHit_.kind;

			if (kind >= 1 && kind <= 5)
			{
				char label[64];
				sprintf_s(label, "%s %.0fM", kindNames[kind], aimHit_.distance);

				centeredText(cy + 22.0f, 2.0f, 0xB4FFFFFFu, label);
			}
		}

		// The hint bar, on a quiet backdrop so it survives bright terrain.
		const char* hint = "WASD MOVE  SPACE JUMP  E HELMET  Q CRATE  F DIG  G BUILD  ESC PAUSE";
		const float hintScale = 2.0f;
		const float hintWidth = mv::hud::HudRenderer::textWidth(hintScale, hint);

		hudRenderer_.rect((w - hintWidth) * 0.5f - 12.0f, h - 46.0f, hintWidth + 24.0f, 30.0f, 0x50000000u);
		centeredText(h - 39.0f, hintScale, 0xE6FFFFFFu, hint);
	}
	else if (gameState_.is(mv::game::EGameState::ePaused))
	{
		// The world dims but stays visible: a pause is a held breath, not a cut.
		hudRenderer_.rect(0.0f, 0.0f, w, h, 0x82000000u);

		centeredText(h * 0.4f, 6.0f, kWhite, "PAUSED");
		centeredText(h * 0.4f + 70.0f, 2.0f, 0xC8FFFFFFu, "ENTER RESUME    ESC TITLE");
	}
}

void Engine::updateCamera(float deltaTime)
{
	ImGuiIO& io = ImGui::GetIO();

	audioClock_ += deltaTime;
	gameState_.update(deltaTime);
	gameInput_.update(GetForegroundWindow() == hwnd_ && !io.WantCaptureKeyboard);

	// --- paused: the world holds still ------------------------------------------
	//
	// No look, no walk, no physics: the fixed clock simply is not ticked, so on
	// resume the simulation continues from the same step it left. Only the two
	// menu keys are listened to.
	if (gameState_.is(mv::game::EGameState::ePaused))
	{
		if (gameInput_.pressed(mv::game::EKey::eConfirm))
			gameState_.transition(mv::game::EGameState::ePlaying);
		else if (gameInput_.pressed(mv::game::EKey::ePause))
			exitToTitle();

		return;
	}

	// --- title: Enter starts the game -------------------------------------------
	if (gameState_.is(mv::game::EGameState::eTitle) &&
		gameInput_.pressed(mv::game::EKey::eConfirm))
	{
		startPlay();
	}

	// The sculpt waves ride the same clock discipline as the fox: alive in title
	// and play, frozen with everything else in pause.
	if (sculptAnimate_)
		sculptTime_ += deltaTime;

	// The fox trots its circle in title and play alike, and freezes with everything
	// else in pause (the early return above). Ground speed follows the clip, so the
	// feet and the world agree about how fast the world slides by.
	if (foxLoaded_)
	{
		foxAnimator_.update(deltaTime * foxSpeedScale_);

		const std::string clip = foxAnimator_.clipName(foxAnimator_.currentClip());
		const mv::types::f32 groundSpeed =
			clip == "Run" ? 3.6f :
			clip == "Walk" ? 1.1f : 0.0f;

		const mv::types::f32 radius = 5.0f;

		foxAngle_ += groundSpeed * foxSpeedScale_ * deltaTime / radius;

		foxPosition_ = {
			foxAnchor_.x + radius * std::cos(foxAngle_),
			0.0f,
			foxAnchor_.z + radius * std::sin(foxAngle_) };

		if (groundField_.valid())
		{
			// The ground sampler is triangle-exact against the render mesh, so the
			// only correction left is the model's own: feet authored below origin.
			foxPosition_.y = groundField_.heightAt(foxPosition_.x, foxPosition_.z)
				- foxModel_.boundsMin.y * foxScale_;
		}

		// Facing the tangent of the circle; the +pi is the fox's authored forward.
		foxYaw_ = foxAngle_ + 3.14159265f;
	}

	// Look only while the right button is held, so the cursor stays usable for the UI.
	// WantCaptureMouse keeps a drag that started on a window from moving the camera.
	if (ImGui::IsMouseDown(ImGuiMouseButton_Right) && !io.WantCaptureMouse)
	{
		const float sensitivity = 0.003f;
		cameraYaw_ += io.MouseDelta.x * sensitivity;
		cameraPitch_ -= io.MouseDelta.y * sensitivity;

		// Stop just short of straight up or down, where the view basis degenerates.
		const float limit = 1.55f;
		cameraPitch_ = (cameraPitch_ > limit) ? limit : cameraPitch_;
		cameraPitch_ = (cameraPitch_ < -limit) ? -limit : cameraPitch_;
	}

	// --- playing: the camera is a character on the ground -----------------------
	//
	// The look above still applies -- eyes are eyes -- but translation goes through the
	// character controller at the fixed rate, and the camera renders the interpolated
	// eye. The game modules read the keyboard themselves; ImGui's capture flag rides
	// along as the focus gate so typing into a widget does not walk the player away.
	if (gameState_.is(mv::game::EGameState::ePlaying))
	{
		if (!terrainScene_ || !groundField_.valid())
		{
			exitToTitle();
		}
		else
		{
			if (gameInput_.pressed(mv::game::EKey::ePause))
			{
				gameState_.transition(mv::game::EGameState::ePaused);
				return;
			}

			// The wanted direction from the camera's yaw: W walks where the player looks,
			// flattened -- looking at the sky is not a request to leave the ground. The
			// same convention as the fly camera below, z negated: getting that sign wrong
			// walks backwards at yaw zero and mirrors every diagonal.
			const mv::math::Vec3 walkForward{ std::sin(cameraYaw_), 0.0f, -std::cos(cameraYaw_) };
			const mv::math::Vec3 walkRight{ std::cos(cameraYaw_), 0.0f, std::sin(cameraYaw_) };

			mv::math::Vec3 move{ 0.0f, 0.0f, 0.0f };

			if (gameInput_.held(mv::game::EKey::eForward)) move = move + walkForward;
			if (gameInput_.held(mv::game::EKey::eBack)) move = move - walkForward;
			if (gameInput_.held(mv::game::EKey::eRight)) move = move + walkRight;
			if (gameInput_.held(mv::game::EKey::eLeft)) move = move - walkRight;

			const bool run = gameInput_.held(mv::game::EKey::eRun);

			// The jump edge is latched, not read in place: most frames at high frame
			// rates drain no fixed step at all, and an edge consumed by no step is a
			// jump that silently never happened. The sound fires on the edge -- the
			// effort is now even if the physics of it waits for the next step.
			if (gameInput_.pressed(mv::game::EKey::eJump) && !jumpQueued_)
			{
				jumpQueued_ = true;

				if (player_.grounded())
					audio_.play(soundJump_, 0.6f);
			}

			gameClock_.tick(deltaTime);

			while (gameClock_.step())
			{
				// Steer before the step, read back after it: the capsule only knows
				// where it ended up once the world has had its say.
				player_.update(gameClock_.fixedDelta(), move, run, jumpQueued_, physics_, playerParams_);
				physics_.step(gameClock_.fixedDelta());
				player_.sync(physics_);

				jumpQueued_ = false;
			}

			// The frame's collisions, turned into sound: strongest contact point,
			// volume from the impulse, a per-body cooldown so a box grinding down a
			// slope knocks occasionally instead of buzzing.
			contactEvents_.clear();
			physics_.takeContactEvents(contactEvents_);

			for (const mv::game::ContactEvent& event : contactEvents_)
			{
				const mv::types::u32 key =
					event.bodyA != mv::game::kInvalidBody ? event.bodyA : event.bodyB;

				if (key == mv::game::kInvalidBody)
					continue;

				const auto it = impactCooldowns_.find(key);

				if (it != impactCooldowns_.end() && audioClock_ - it->second < 0.15f)
					continue;

				impactCooldowns_[key] = audioClock_;

				const float volume = (std::min)(event.impulse / 18.0f, 1.0f);
				const float pitch = 0.85f + 0.3f * (float)(std::rand() % 100) / 100.0f;

				audio_.play3d(soundImpact_, event.position, volume, pitch);
			}

			// Physics owns the transforms of everything it simulates; gameplay reads
			// them back so the UI and any logic see where things actually are. The
			// water crossing rides the same read-back: the old position is still in
			// the transform when the new one arrives, and a downward crossing of the
			// surface is a splash.
			world_.forEach([this](mv::game::EntityHandle, mv::game::Entity& entity)
			{
				if (entity.physicsBody == mv::game::kInvalidBody)
					return;

				mv::math::Mat4 matrix;
				if (physics_.bodyMatrix(entity.physicsBody, matrix))
				{
					const mv::math::Vec3 next{ matrix.m[12], matrix.m[13], matrix.m[14] };

					if (waterParams_.enabled &&
						entity.transform.position.y >= waterParams_.level &&
						next.y < waterParams_.level)
					{
						audio_.play3d(
							soundSplash_,
							{ next.x, waterParams_.level, next.z },
							0.9f,
							0.9f + 0.2f * (float)(std::rand() % 100) / 100.0f);
					}

					entity.transform.position = next;
				}
			});

			// Feet: a step sound every stride's worth of ground actually covered, and
			// the landing on the airborne-to-grounded edge. Distance, not time -- a
			// run steps faster because it moves faster, for free.
			{
				const mv::math::Vec3 playerPosition = player_.position();

				if (player_.grounded())
				{
					const mv::types::f32 dx = playerPosition.x - prevPlayerPosition_.x;
					const mv::types::f32 dz = playerPosition.z - prevPlayerPosition_.z;

					footstepDistance_ += std::sqrt(dx * dx + dz * dz);

					if (footstepDistance_ > 2.2f)
					{
						footstepDistance_ = 0.0f;
						audio_.play(soundFootstep_, 0.45f,
							0.85f + 0.3f * (float)(std::rand() % 100) / 100.0f);
					}

					if (!prevGrounded_)
						audio_.play(soundLand_, 0.7f);
				}
				else
				{
					footstepDistance_ = 0.0f;
				}

				prevGrounded_ = player_.grounded();
				prevPlayerPosition_ = playerPosition;
			}

			cameraPosition_ = player_.eyePosition(gameClock_.alpha(), playerParams_);

			// The world's copy of the player follows the controller, which is the shape
			// everything else will use: gameplay writes transforms, rendering reads them.
			if (mv::game::Entity* entity = world_.get(playerEntity_))
			{
				entity->transform.position = player_.position();
				entity->transform.rotation.y = cameraYaw_;
			}

			const mv::math::Vec3 look = mv::math::normalize({
				std::cos(cameraPitch_) * std::sin(cameraYaw_),
				std::sin(cameraPitch_),
				-std::cos(cameraPitch_) * std::cos(cameraYaw_) });

			audio_.setListener(cameraPosition_, look);

			// The aim ray, every frame: one rayTest against everything at once --
			// terrain, thrown props, the water plane -- where the old heightfield
			// bisection could only ever answer for the ground.
			aimValid_ = physics_.isReady() &&
				physics_.raycast(cameraPosition_, look, 500.0f, aimHit_, true);

			// Carving and building, at the aim point: F digs, G adds. Held rather
			// than pressed, on a small cooldown -- sculpting is strokes, not clicks.
			sculptCooldown_ = (std::max)(sculptCooldown_ - deltaTime, 0.0f);

			const bool dig = gameInput_.held(mv::game::EKey::eDig);
			const bool build = gameInput_.held(mv::game::EKey::eBuild);

			if ((dig || build) && sculptCooldown_ <= 0.0f &&
				aimValid_ && aimHit_.distance < 30.0f && sculptPlaced_)
			{
				// One stroke, every chunk it overlaps: applyBrush self-clips, so
				// asking all of them costs nothing and keeps the shared boundary
				// corners identical on both sides of every seam -- which is the
				// whole crack-free story. The hot path still never remeshes on the
				// CPU; each touched chunk re-marches on the GPU this frame.
				const mv::types::f32 strength = dig ? -1.4f : 1.4f;
				bool anyChanged = false;

				for (mv::types::u32 i = 0; i < (mv::types::u32)sculptChunks_.size(); i++)
				{
					if (sculptChunks_[i].applyBrush(aimHit_.position, 1.7f, strength))
					{
						sculptGpu_.queueBrush(i, aimHit_.position, 1.7f, strength);
						sculptChunkDirty_[i] = true;
						sculptSlotStates_[i].everEdited = true;
						anyChanged = true;
					}
				}

				if (anyChanged)
				{
					sculptPhysicsDirty_ = true;
					sculptLastEditTime_ = audioClock_;

					audio_.play3d(soundImpact_, aimHit_.position, 0.3f,
						1.15f + 0.2f * (float)(std::rand() % 100) / 100.0f);

					sculptCooldown_ = 0.12f;
				}
			}

			// The deferred half: once the stroke has paused for a beat, rebuild the
			// dirty chunks' CPU meshes and hand them to Bullet -- usually one or
			// two chunks, not the world.
			if (sculptPhysicsDirty_ && audioClock_ - sculptLastEditTime_ > 0.30f)
			{
				for (mv::types::u32 i = 0; i < (mv::types::u32)sculptChunks_.size(); i++)
				{
					if (!sculptChunkDirty_[i])
						continue;

					sculptChunks_[i].rebuildMesh();

					physics_.setSculptMesh(
						i,
						sculptChunks_[i].trianglePositions(),
						sculptChunks_[i].triangleVertexCount(),
						mv::voxel::SculptVolume::positionStride());

					sculptChunkDirty_[i] = false;
				}

				sculptPhysicsDirty_ = false;
			}

			// The streaming window: cells the player's chunk neighbourhood wants but
			// no slot holds are queued, and one is refilled per frame -- a boundary
			// crossing costs five chunks spread over five frames, not one hitch.
			if (sculptPlaced_)
			{
				const float chunkExtent =
					(float)mv::voxel::SculptVolume::kCells * mv::voxel::SculptVolume::kCellSize;

				const mv::math::Vec3 playerPosition = player_.position();

				const mv::types::s32 playerCellX = (mv::types::s32)std::floor(playerPosition.x / chunkExtent);
				const mv::types::s32 playerCellZ = (mv::types::s32)std::floor(playerPosition.z / chunkExtent);

				const mv::types::s32 reach = (mv::types::s32)kSculptChunksPerSide / 2;

				for (mv::types::s32 dz = -reach; dz <= reach; dz++)
				{
					for (mv::types::s32 dx = -reach; dx <= reach; dx++)
					{
						const mv::types::s32 cx = playerCellX + dx;
						const mv::types::s32 cz = playerCellZ + dz;

						const mv::types::u32 slot = sculptSlotOf(cx, cz, kSculptChunksPerSide);

						if (sculptSlotStates_[slot].cx == cx && sculptSlotStates_[slot].cz == cz)
							continue;

						bool queued = false;

						for (const auto& pending : sculptRefillQueue_)
							queued |= pending.first == cx && pending.second == cz;

						if (!queued)
							sculptRefillQueue_.push_back({ cx, cz });
					}
				}

				if (!sculptRefillQueue_.empty())
				{
					const auto cell = sculptRefillQueue_.front();
					sculptRefillQueue_.erase(sculptRefillQueue_.begin());

					// Only if it is still wanted: the player may have turned around
					// while this cell waited its turn.
					const bool wanted =
						std::abs(cell.first - playerCellX) <= reach &&
						std::abs(cell.second - playerCellZ) <= reach;

					const mv::types::u32 slot = sculptSlotOf(cell.first, cell.second, kSculptChunksPerSide);

					if (wanted &&
						(sculptSlotStates_[slot].cx != cell.first || sculptSlotStates_[slot].cz != cell.second))
					{
						refillSculptSlot(slot, cell.first, cell.second);
					}
				}
			}

			// Interact: throw a prop. The whole module chain in one keypress -- input
			// edge, a Bullet rigid body, a world entity bound to it, and the prop pass
			// draws it with the body's own matrix from this frame on. E throws the
			// helmet as a convex hull of its own mesh; Q throws the crate as a box.
			const bool throwHelmet = gameInput_.pressed(mv::game::EKey::eInteract);
			const bool throwBox = gameInput_.pressed(mv::game::EKey::eAltInteract);

			if ((throwHelmet || throwBox) &&
				propRenderer_.isReady() && physics_.isReady())
			{
				const mv::math::Vec3 spawn = cameraPosition_ + look * 1.5f;
				const mv::math::Vec3 velocity = look * 10.0f + mv::math::Vec3{ 0.0f, 2.5f, 0.0f };

				mv::game::BodyHandle body = mv::game::kInvalidBody;
				mv::types::u32 primitive = 0xFFFFFFFF;
				const char* name = "";

				if (throwHelmet && !helmetHullPoints_.empty())
				{
					body = physics_.addConvexHull(
						spawn,
						helmetHullPoints_.data(),
						(mv::types::u32)(helmetHullPoints_.size() / 3),
						3 * sizeof(mv::types::f32),
						3.0f,
						velocity);
					primitive = kPropHelmet;
					name = "Helmet";
				}
				else if (throwBox)
				{
					body = physics_.addBox(spawn, boxHalfExtents_, 2.0f, velocity);
					primitive = kPropBox;
					name = "Crate";
				}

				if (body != mv::game::kInvalidBody)
				{
					const mv::game::EntityHandle handle = world_.create(name);

					if (mv::game::Entity* prop = world_.get(handle))
					{
						prop->transform.position = spawn;
						prop->primitive = primitive;
						prop->physicsBody = body;
					}
				}
			}

			return;
		}
	}

	if (io.WantCaptureKeyboard)
		return;

	// Cycling the debug view from the keyboard beats hunting for the combo while flying.
	// Not Tab: ImGui claims that for focus navigation and would swallow it.
	if (ImGui::IsKeyPressed(ImGuiKey_V, false))
	{
		debugMode_ = (debugMode_ + 1) % kDebugModeCount;
	}

	// The A/B that matters for virtual texturing: with every page resident the image must
	// not change, and flipping it from the keyboard makes the difference easy to see.
	if (ImGui::IsKeyPressed(ImGuiKey_B, false))
	{
		vtEnabled_ = !vtEnabled_;
	}

	if (ImGui::IsKeyPressed(ImGuiKey_N, false))
	{
		shadowEnabled_ = !shadowEnabled_;
	}

	const mv::math::Vec3 forward = mv::math::normalize({
		std::cos(cameraPitch_) * std::sin(cameraYaw_),
		std::sin(cameraPitch_),
		-std::cos(cameraPitch_) * std::cos(cameraYaw_) });

	audio_.setListener(cameraPosition_, forward);

	const mv::math::Vec3 right = mv::math::normalize(mv::math::cross(forward, { 0.0f, 1.0f, 0.0f }));

	float speed = cameraSpeed_ * deltaTime;
	if (ImGui::IsKeyDown(ImGuiMod_Shift)) speed *= 5.0f;

	mv::math::Vec3 move{};
	if (ImGui::IsKeyDown(ImGuiKey_W)) move = move + forward;
	if (ImGui::IsKeyDown(ImGuiKey_S)) move = move - forward;
	if (ImGui::IsKeyDown(ImGuiKey_D)) move = move + right;
	if (ImGui::IsKeyDown(ImGuiKey_A)) move = move - right;
	if (ImGui::IsKeyDown(ImGuiKey_E)) move.y += 1.0f;
	if (ImGui::IsKeyDown(ImGuiKey_Q)) move.y -= 1.0f;

	if (mv::math::dot(move, move) > 0.0f)
	{
		cameraPosition_ = cameraPosition_ + mv::math::normalize(move) * speed;
	}
}

bool Engine::initializeSceneResources()
{
	// Physics first: it owns no GPU resources and everything later may want to hand it
	// colliders. The character capsule exists from the start; it only moves in play
	// mode, and the terrain body arrives before anything steps it.
	physics_.initialize();
	physics_.createCharacter(
		{ 0.0f, 400.0f, 0.0f },
		playerParams_.radius,
		playerParams_.height,
		playerParams_.stepHeight,
		playerParams_.gravity,
		playerParams_.maxSlopeNormalY);

	// Audio next, for the same reason: no GPU in sight, and the gameplay below wants
	// handles to hand out. The "assets" are synthesised right here -- see sound_synth.
	if (audio_.initialize())
	{
		const auto registerSound = [this](const std::vector<mv::types::f32>& samples)
		{
			return audio_.create(samples.data(), (mv::types::u32)samples.size());
		};

		soundImpact_ = registerSound(mv::game::synth::impact());
		soundSplash_ = registerSound(mv::game::synth::splash());
		soundFootstep_ = registerSound(mv::game::synth::footstep());
		soundJump_ = registerSound(mv::game::synth::jump());
		soundLand_ = registerSound(mv::game::synth::land());

		OutputDebugStringA("Audio: XAudio2, 5 synthesised sounds\n");
	}
	else
	{
		OutputDebugStringA("Audio: initialize FAILED (no output device?)\n");
	}

	// --- compute ---------------------------------------------------------------

	// First, because the loaders below want it. A failure here is not fatal: everything
	// that uses it falls back to the CPU path it had before.
	{
		const std::vector<mv::types::u32> mipgenCs = readShaderFile(useVulkan_ ? "mipgen.cs.spv" : "mipgen.cs.cso");

		if (!mipgenCs.empty())
		{
			mipGenerator_.initialize(rhi_, mipgenCs.data(), (mv::types::u32)(mipgenCs.size() * sizeof(mv::types::u32)));
		}

		OutputDebugStringA(mipGenerator_.isReady()
			? "Mip chains: compute\n"
			: "Mip chains: CPU (mipgen shader missing)\n");

		const std::vector<mv::types::u32> fillCs = readShaderFile(useVulkan_ ? "bufferfill.cs.spv" : "bufferfill.cs.cso");

		if (!fillCs.empty())
		{
			feedbackClear_.initialize(rhi_, fillCs.data(), (mv::types::u32)(fillCs.size() * sizeof(mv::types::u32)));
		}

		// The four terrain passes. All or nothing: a half-built chain would leave the
		// heightmap on the GPU and the mesh expecting it on the CPU.
		const std::vector<mv::types::u32> heightCs = readShaderFile(useVulkan_ ? "terrain_height.cs.spv" : "terrain_height.cs.cso");
		const std::vector<mv::types::u32> normaliseCs = readShaderFile(useVulkan_ ? "terrain_normalise.cs.spv" : "terrain_normalise.cs.cso");
		const std::vector<mv::types::u32> meshCs = readShaderFile(useVulkan_ ? "terrain_mesh.cs.spv" : "terrain_mesh.cs.cso");
		const std::vector<mv::types::u32> bakeCs = readShaderFile(useVulkan_ ? "terrain_bake.cs.spv" : "terrain_bake.cs.cso");

		if (!heightCs.empty() && !normaliseCs.empty() && !meshCs.empty() && !bakeCs.empty() && !fillCs.empty())
		{
			mv::compute::TerrainBuilder::Shaders shaders{};
			shaders.height = heightCs.data();       shaders.heightSize = (mv::types::u32)(heightCs.size() * sizeof(mv::types::u32));
			shaders.normalise = normaliseCs.data(); shaders.normaliseSize = (mv::types::u32)(normaliseCs.size() * sizeof(mv::types::u32));
			shaders.mesh = meshCs.data();           shaders.meshSize = (mv::types::u32)(meshCs.size() * sizeof(mv::types::u32));
			shaders.bake = bakeCs.data();           shaders.bakeSize = (mv::types::u32)(bakeCs.size() * sizeof(mv::types::u32));
			shaders.fill = fillCs.data();           shaders.fillSize = (mv::types::u32)(fillCs.size() * sizeof(mv::types::u32));

			terrainBuilder_.initialize(rhi_, shaders);
		}

		const std::vector<mv::types::u32> skyCs = readShaderFile(useVulkan_ ? "env_sky.cs.spv" : "env_sky.cs.cso");
		const std::vector<mv::types::u32> prefilterCs = readShaderFile(useVulkan_ ? "env_prefilter.cs.spv" : "env_prefilter.cs.cso");

		if (!skyCs.empty() && !prefilterCs.empty())
		{
			mv::compute::EnvironmentBaker::Shaders shaders{};
			shaders.sky = skyCs.data();             shaders.skySize = (mv::types::u32)(skyCs.size() * sizeof(mv::types::u32));
			shaders.prefilter = prefilterCs.data(); shaders.prefilterSize = (mv::types::u32)(prefilterCs.size() * sizeof(mv::types::u32));

			environmentBaker_.initialize(rhi_, shaders);
		}

		// --- clouds --------------------------------------------------------------

		const std::vector<mv::types::u32> cloudShapeCs = readShaderFile(useVulkan_ ? "cloud_shape.cs.spv" : "cloud_shape.cs.cso");
		const std::vector<mv::types::u32> cloudDetailCs = readShaderFile(useVulkan_ ? "cloud_detail.cs.spv" : "cloud_detail.cs.cso");
		const std::vector<mv::types::u32> cloudWeatherCs = readShaderFile(useVulkan_ ? "cloud_weather.cs.spv" : "cloud_weather.cs.cso");
		const std::vector<mv::types::u32> cloudMarchCs = readShaderFile(useVulkan_ ? "cloud_march.cs.spv" : "cloud_march.cs.cso");
		const std::vector<mv::types::u32> cloudShadowCs = readShaderFile(useVulkan_ ? "cloud_shadow.cs.spv" : "cloud_shadow.cs.cso");
		const std::vector<mv::types::u32> cloudCompositeVs = readShaderFile(useVulkan_ ? "cloud_composite.vs.spv" : "cloud_composite.vs.cso");
		const std::vector<mv::types::u32> cloudCompositePs = readShaderFile(useVulkan_ ? "cloud_composite.ps.spv" : "cloud_composite.ps.cso");

		if (!cloudShapeCs.empty() && !cloudDetailCs.empty() && !cloudWeatherCs.empty() &&
			!cloudMarchCs.empty() && !cloudShadowCs.empty() &&
			!cloudCompositeVs.empty() && !cloudCompositePs.empty())
		{
			mv::clouds::CloudRenderer::Shaders shaders{};
			shaders.shape = cloudShapeCs.data();          shaders.shapeSize = (mv::types::u32)(cloudShapeCs.size() * sizeof(mv::types::u32));
			shaders.detail = cloudDetailCs.data();        shaders.detailSize = (mv::types::u32)(cloudDetailCs.size() * sizeof(mv::types::u32));
			shaders.weather = cloudWeatherCs.data();      shaders.weatherSize = (mv::types::u32)(cloudWeatherCs.size() * sizeof(mv::types::u32));
			shaders.march = cloudMarchCs.data();          shaders.marchSize = (mv::types::u32)(cloudMarchCs.size() * sizeof(mv::types::u32));
			shaders.shadow = cloudShadowCs.data();        shaders.shadowSize = (mv::types::u32)(cloudShadowCs.size() * sizeof(mv::types::u32));
			shaders.compositeVs = cloudCompositeVs.data(); shaders.compositeVsSize = (mv::types::u32)(cloudCompositeVs.size() * sizeof(mv::types::u32));
			shaders.compositePs = cloudCompositePs.data(); shaders.compositePsSize = (mv::types::u32)(cloudCompositePs.size() * sizeof(mv::types::u32));

			if (clouds_.initialize(rhi_, shaders, mv::post::kChainFormat))
			{
				clouds_.resize(width_, height_);
				clouds_.bake(cloudParams_);

				char message[128];
				sprintf_s(message, "Clouds: %u^3 shape, %u^3 detail, %u^2 weather, baked in %.0f ms\n",
					mv::clouds::kShapeSize, mv::clouds::kDetailSize, mv::clouds::kWeatherSize,
					clouds_.lastBakeMilliseconds());

				OutputDebugStringA(message);
			}
		}

		// --- water ---------------------------------------------------------------

		const std::vector<mv::types::u32> waterVs = readShaderFile(useVulkan_ ? "water.vs.spv" : "water.vs.cso");
		const std::vector<mv::types::u32> waterPs = readShaderFile(useVulkan_ ? "water.ps.spv" : "water.ps.cso");
		const std::vector<mv::types::u32> waterSsrCs = readShaderFile(useVulkan_ ? "water_ssr.cs.spv" : "water_ssr.cs.cso");

		if (!waterVs.empty() && !waterPs.empty() && !waterSsrCs.empty())
		{
			mv::water::WaterSurface::Shaders shaders{};
			shaders.vs = waterVs.data(); shaders.vsSize = (mv::types::u32)(waterVs.size() * sizeof(mv::types::u32));
			shaders.ps = waterPs.data(); shaders.psSize = (mv::types::u32)(waterPs.size() * sizeof(mv::types::u32));
			shaders.ssr = waterSsrCs.data(); shaders.ssrSize = (mv::types::u32)(waterSsrCs.size() * sizeof(mv::types::u32));

			if (water_.initialize(rhi_, shaders, mv::post::kChainFormat))
			{
				water_.resize(width_, height_);

				// The waterline and the terrain's sand band are the same line, so the shore
				// material follows the surface rather than being placed near it.
				waterParams_.level = terrainDesc_.waterHeight;

				OutputDebugStringA("Water: analytic plane, gradient-noise waves, SSR + cube reflection\n");
			}
		}

	}

	// --- velocity target ------------------------------------------------------

	mv::rhi::TextureDesc velocityDesc{};
	velocityDesc.width = width_;
	velocityDesc.height = height_;
	velocityDesc.depth = 1;
	velocityDesc.usage = mv::rhi::ETextureUsage::eColorAttachment | mv::rhi::ETextureUsage::eSampled;
	velocityDesc.mipLevels = 1;
	velocityDesc.format = kVelocityFormat;
	velocityDesc.memoryType = mv::rhi::EMemoryType::eDeviceLocalImage;

	velocityTexture_ = rhi_->createTexture(velocityDesc);

	// Owned here rather than left to the graph. A graph transient is handed back at its
	// last use and re-acquired the next frame, so its handle alternates between the frames
	// in flight -- and anything naming it in a descriptor would have to rewrite that
	// descriptor every frame, while frames still reading it are in flight.
	mv::rhi::TextureDesc depthDesc{};
	depthDesc.width = width_;
	depthDesc.height = height_;
	depthDesc.depth = 1;
	depthDesc.usage = mv::rhi::ETextureUsage::eDepthStencilAttachment | mv::rhi::ETextureUsage::eSampled;
	depthDesc.mipLevels = 1;
	depthDesc.format = mv::rhi::ETextureFormat::eD32_SFLOAT;
	depthDesc.memoryType = mv::rhi::EMemoryType::eDeviceLocalImage;

	sceneDepthTexture_ = rhi_->createTexture(depthDesc);

	// --- material system ------------------------------------------------------

	const std::vector<mv::types::u32> vsCode = readShaderFile(useVulkan_ ? "model.vs.spv" : "model.vs.cso");
	const std::vector<mv::types::u32> psCode = readShaderFile(useVulkan_ ? "model.ps.spv" : "model.ps.cso");

	if (vsCode.empty() || psCode.empty())
		return false;

	const mv::material::MaterialSystem::ShaderCode vs{ vsCode.data(), (mv::types::u32)(vsCode.size() * sizeof(mv::types::u32)) };
	const mv::material::MaterialSystem::ShaderCode ps{ psCode.data(), (mv::types::u32)(psCode.size() * sizeof(mv::types::u32)) };

	if (!materialSystem_.initialize(rhi_, vs, ps, { mv::post::kChainFormat, kVelocityFormat }, mv::rhi::ETextureFormat::eD32_SFLOAT))
		return false;

	// --- fog ------------------------------------------------------------------

	// After the material system, not beside the other effect loaders: the shaft march
	// borrows the scene and bindless layouts, and those do not exist until the material
	// system has built them. Initialising from an INVALID_HANDLE layout is how this block
	// spent an afternoon silently disabled.
	{
		const std::vector<mv::types::u32> fogVs = readShaderFile(useVulkan_ ? "fog.vs.spv" : "fog.vs.cso");
		const std::vector<mv::types::u32> fogPs = readShaderFile(useVulkan_ ? "fog.ps.spv" : "fog.ps.cso");

		if (!fogVs.empty() && !fogPs.empty())
		{
			mv::fog::HeightFog::Shaders shaders{};
			shaders.vs = fogVs.data(); shaders.vsSize = (mv::types::u32)(fogVs.size() * sizeof(mv::types::u32));
			shaders.ps = fogPs.data(); shaders.psSize = (mv::types::u32)(fogPs.size() * sizeof(mv::types::u32));

			if (fog_.initialize(rhi_, shaders, mv::post::kChainFormat,
				materialSystem_.sceneLayout(), materialSystem_.bindlessLayout()))
			{
				OutputDebugStringA("Fog: exponential height profile, marched light shafts\n");
			}
			else
			{
				OutputDebugStringA("Fog: initialize FAILED\n");
			}
		}
	}

	// --- grass ----------------------------------------------------------------

	{
		const std::vector<mv::types::u32> grassVs = readShaderFile(useVulkan_ ? "grass.vs.spv" : "grass.vs.cso");
		const std::vector<mv::types::u32> grassPs = readShaderFile(useVulkan_ ? "grass.ps.spv" : "grass.ps.cso");
		const std::vector<mv::types::u32> grassCullCs = readShaderFile(useVulkan_ ? "grass_cull.cs.spv" : "grass_cull.cs.cso");
		const std::vector<mv::types::u32> grassResetCs = readShaderFile(useVulkan_ ? "grass_reset.cs.spv" : "grass_reset.cs.cso");

		if (!grassVs.empty() && !grassPs.empty() && !grassCullCs.empty() && !grassResetCs.empty())
		{
			mv::grass::GrassField::Shaders shaders{};
			shaders.vs = grassVs.data(); shaders.vsSize = (mv::types::u32)(grassVs.size() * sizeof(mv::types::u32));
			shaders.ps = grassPs.data(); shaders.psSize = (mv::types::u32)(grassPs.size() * sizeof(mv::types::u32));
			shaders.cull = grassCullCs.data(); shaders.cullSize = (mv::types::u32)(grassCullCs.size() * sizeof(mv::types::u32));
			shaders.reset = grassResetCs.data(); shaders.resetSize = (mv::types::u32)(grassResetCs.size() * sizeof(mv::types::u32));

			// The same targets the geometry passes write: colour, velocity, depth.
			if (grass_.initialize(rhi_, shaders,
				{ mv::post::kChainFormat, kVelocityFormat },
				mv::rhi::ETextureFormat::eD32_SFLOAT,
				materialSystem_.sceneLayout(), materialSystem_.bindlessLayout()))
			{
				OutputDebugStringA("Grass: GPU frustum cull, indirect draw\n");
			}
			else
			{
				OutputDebugStringA("Grass: initialize FAILED\n");
			}
		}
	}

	// --- virtual textures -----------------------------------------------------

	// Before the scene bind groups, because they carry its two buffers and the pages
	// themselves are not needed until the model is loaded.
	if (!virtualTextures_.initialize(rhi_, materialSystem_))
		return false;

	// --- per-frame scene buffers ----------------------------------------------

	const mv::types::u32 framesInFlight = rhi_->framesInFlight();
	sceneBuffers_.resize(framesInFlight);
	sceneBindGroups_.resize(framesInFlight);

	for (mv::types::u32 i = 0; i < framesInFlight; i++)
	{
		mv::rhi::BufferDesc sceneDesc{};
		// Large enough for the four cascade matrices and the nine irradiance coefficients.
		// It must not be smaller than SceneConstants: writeBuffer does not bounds check, and
		// on Vulkan the overflow lands in whatever buffer was suballocated next.
		sceneDesc.size = kSceneConstantsSize;
		sceneDesc.usage = mv::rhi::EBufferUsage::eUniform;
		sceneDesc.memoryType = mv::rhi::EMemoryType::eHostVisibleBuffer;

		sceneBuffers_[i] = rhi_->createBuffer(sceneDesc);

		mv::rhi::BindGroupDesc groupDesc{};
		groupDesc.layout = materialSystem_.sceneLayout();
		groupDesc.uniformBuffers.push_back({ .binding = 0, .buffer = sceneBuffers_[i], .offset = 0, .range = kSceneConstantsSize });
		groupDesc.storageBuffers.push_back({
			.binding = 1, .buffer = virtualTextures_.infoBuffer(), .offset = 0,
			.stride = sizeof(mv::vt::GpuVirtualTextureInfo), .count = mv::vt::kMaxVirtualTextures });
		groupDesc.storageBuffers.push_back({
			.binding = 2, .buffer = virtualTextures_.pageTableBuffer(), .offset = 0,
			.stride = sizeof(mv::types::u32), .count = mv::vt::kMaxPageTableEntries });

		// Binding 3, the shadow atlas, is filled in once the map exists. The comparison
		// sampler can be set now: it depends on nothing.
		groupDesc.samplers.push_back({
			.binding = 4,
			.sampler = {
				.filter = mv::rhi::EFilterMode::eLinear,
				.address = mv::rhi::EAddressMode::eClampToEdge,
				.compareEnable = true,
				.compareOp = mv::rhi::ECompareOp::eLessEqual },
			.arrayIndex = 0 });

		sceneBindGroups_[i] = rhi_->createBindGroup(groupDesc);
	}

	// --- model ----------------------------------------------------------------

	char modulePath[MAX_PATH]{};
	GetModuleFileNameA(nullptr, modulePath, MAX_PATH);
	std::string exeDir = modulePath;
	const size_t slash = exeDir.find_last_of('\\');
	exeDir = (slash == std::string::npos) ? std::string() : exeDir.substr(0, slash + 1);

	// The virtual texture system slices pages out of the decoded source, long after the
	// upload, so the loader has to keep them rather than freeing them per texture.
	gltfLoader_.setRetainSources(true);

	if (!gltfLoader_.load(rhi_, materialSystem_, exeDir + modelPath_, model_))
	{
		OutputDebugStringA("Failed to load model\n");
		return false;
	}

	for (const auto& source : gltfLoader_.textureSources())
	{
		virtualTextures_.add(source);
	}

	virtualTextures_.finalize();
	gltfLoader_.releaseSources();

	// --- props ----------------------------------------------------------------

	// The models entities stand in the world as. Loaded after the virtual textures are
	// finalised for the same reason the terrain is: an unregistered bindless slot reads a
	// zero level count, which is exactly the sample-directly path.
	gltfLoader_.setRetainSources(false);

	// Geometry retained on the CPU: the physics shapes are measured off the render
	// meshes themselves -- the helmet's convex hull from its vertices, the box's half
	// extents from its bounds -- instead of hand-typed numbers drifting out of sync.
	gltfLoader_.setRetainGeometry(true);

	const bool helmetLoaded = gltfLoader_.load(
		rhi_, materialSystem_, exeDir + "assets\\models\\DamagedHelmet.glb", propAssets_[kPropHelmet].model);

	const bool boxLoaded = gltfLoader_.load(
		rhi_, materialSystem_, exeDir + "assets\\models\\BoxTextured.glb", propAssets_[kPropBox].model);

	gltfLoader_.setRetainGeometry(false);

	for (PropAsset& asset : propAssets_)
	{
		asset.center = (asset.model.boundsMin + asset.model.boundsMax) * 0.5f;
	}

	if (helmetLoaded)
	{
		// The hull points are centred here, once: the body's origin is the shape's
		// middle, and the draw matrix undoes the same centre, so the two stay honest.
		const auto& vertices = propAssets_[kPropHelmet].model.cpuVertices;
		const mv::math::Vec3 center = propAssets_[kPropHelmet].center;

		helmetHullPoints_.reserve(vertices.size() * 3);

		for (const auto& vertex : vertices)
		{
			helmetHullPoints_.push_back(vertex.position[0] - center.x);
			helmetHullPoints_.push_back(vertex.position[1] - center.y);
			helmetHullPoints_.push_back(vertex.position[2] - center.z);
		}

		propAssets_[kPropHelmet].model.cpuVertices.clear();
		propAssets_[kPropHelmet].model.cpuVertices.shrink_to_fit();
	}

	if (boxLoaded)
	{
		const mv::asset::Model& box = propAssets_[kPropBox].model;

		boxHalfExtents_ = (box.boundsMax - box.boundsMin) * 0.5f;

		propAssets_[kPropBox].model.cpuVertices.clear();
		propAssets_[kPropBox].model.cpuVertices.shrink_to_fit();
	}

	if (helmetLoaded || boxLoaded)
	{
		const std::vector<mv::types::u32> propVs = readShaderFile(useVulkan_ ? "prop.vs.spv" : "prop.vs.cso");
		const std::vector<mv::types::u32> propPs = readShaderFile(useVulkan_ ? "prop.ps.spv" : "prop.ps.cso");

		if (!propVs.empty() && !propPs.empty())
		{
			mv::props::PropRenderer::Shaders shaders{};
			shaders.vs = propVs.data(); shaders.vsSize = (mv::types::u32)(propVs.size() * sizeof(mv::types::u32));
			shaders.ps = propPs.data(); shaders.psSize = (mv::types::u32)(propPs.size() * sizeof(mv::types::u32));

			if (propRenderer_.initialize(rhi_, shaders,
				{ mv::post::kChainFormat, kVelocityFormat },
				mv::rhi::ETextureFormat::eD32_SFLOAT,
				materialSystem_.sceneLayout(), materialSystem_.bindlessLayout()))
			{
				OutputDebugStringA("Props: entity-placed models, forward path with a model matrix\n");
			}
			else
			{
				OutputDebugStringA("Props: initialize FAILED\n");
			}
		}
	}
	else
	{
		OutputDebugStringA("Props: prop models missing, placement disabled\n");
	}

	// The physics wireframe overlay: same scene set as everything else, its own tiny
	// line pipeline. Optional in the truest sense -- a failure here just means no ink.
	{
		const std::vector<mv::types::u32> lineVs = readShaderFile(useVulkan_ ? "debug_line.vs.spv" : "debug_line.vs.cso");
		const std::vector<mv::types::u32> linePs = readShaderFile(useVulkan_ ? "debug_line.ps.spv" : "debug_line.ps.cso");

		if (!lineVs.empty() && !linePs.empty())
		{
			mv::debugdraw::DebugLineRenderer::Shaders shaders{};
			shaders.vs = lineVs.data(); shaders.vsSize = (mv::types::u32)(lineVs.size() * sizeof(mv::types::u32));
			shaders.ps = linePs.data(); shaders.psSize = (mv::types::u32)(linePs.size() * sizeof(mv::types::u32));

			if (physicsDebugRenderer_.initialize(rhi_, shaders,
				mv::post::kChainFormat,
				mv::rhi::ETextureFormat::eD32_SFLOAT,
				materialSystem_.sceneLayout(), materialSystem_.bindlessLayout(),
				rhi_->framesInFlight()))
			{
				OutputDebugStringA("Debug lines: physics wireframe overlay\n");
			}
			else
			{
				OutputDebugStringA("Debug lines: initialize FAILED\n");
			}
		}
	}

	// The player's screen. On the backbuffer, like ImGui, because it belongs to the
	// finished image -- tone mapping the crosshair would be a category error.
	{
		const std::vector<mv::types::u32> hudVs = readShaderFile(useVulkan_ ? "hud.vs.spv" : "hud.vs.cso");
		const std::vector<mv::types::u32> hudPs = readShaderFile(useVulkan_ ? "hud.ps.spv" : "hud.ps.cso");

		if (!hudVs.empty() && !hudPs.empty())
		{
			mv::hud::HudRenderer::Shaders shaders{};
			shaders.vs = hudVs.data(); shaders.vsSize = (mv::types::u32)(hudVs.size() * sizeof(mv::types::u32));
			shaders.ps = hudPs.data(); shaders.psSize = (mv::types::u32)(hudPs.size() * sizeof(mv::types::u32));

			if (hudRenderer_.initialize(rhi_, shaders, rhi_->backbufferFormat(), rhi_->framesInFlight()))
			{
				OutputDebugStringA("HUD: 8x8 pixel font atlas, one alpha-blended pass\n");
			}
			else
			{
				OutputDebugStringA("HUD: initialize FAILED\n");
			}
		}
	}

	// The carvable ground: the GPU side (pipelines, tables) comes up here; the
	// chunks themselves are laid over the terrain on the first entry into play.
	{
		OutputDebugStringA("Sculpt: chunked marching cubes world\n");

		// The GPU mesh path on top: same density, compute march, indirect draw.
		const std::vector<mv::types::u32> mcCs = readShaderFile(useVulkan_ ? "sculpt_mc.cs.spv" : "sculpt_mc.cs.cso");
		const std::vector<mv::types::u32> resetCs = readShaderFile(useVulkan_ ? "sculpt_reset.cs.spv" : "sculpt_reset.cs.cso");
		const std::vector<mv::types::u32> deformCs = readShaderFile(useVulkan_ ? "sculpt_deform.cs.spv" : "sculpt_deform.cs.cso");
		const std::vector<mv::types::u32> brushCs = readShaderFile(useVulkan_ ? "sculpt_brush.cs.spv" : "sculpt_brush.cs.cso");
		const std::vector<mv::types::u32> drawVs = readShaderFile(useVulkan_ ? "sculpt_draw.vs.spv" : "sculpt_draw.vs.cso");
		const std::vector<mv::types::u32> drawPs = readShaderFile(useVulkan_ ? "sculpt_draw.ps.spv" : "sculpt_draw.ps.cso");

		if (!mcCs.empty() && !resetCs.empty() && !deformCs.empty() && !brushCs.empty() &&
			!drawVs.empty() && !drawPs.empty())
		{
			mv::voxel::SculptGpu::Shaders shaders{};
			shaders.mc = mcCs.data(); shaders.mcSize = (mv::types::u32)(mcCs.size() * sizeof(mv::types::u32));
			shaders.reset = resetCs.data(); shaders.resetSize = (mv::types::u32)(resetCs.size() * sizeof(mv::types::u32));
			shaders.deform = deformCs.data(); shaders.deformSize = (mv::types::u32)(deformCs.size() * sizeof(mv::types::u32));
			shaders.brush = brushCs.data(); shaders.brushSize = (mv::types::u32)(brushCs.size() * sizeof(mv::types::u32));
			shaders.vs = drawVs.data(); shaders.vsSize = (mv::types::u32)(drawVs.size() * sizeof(mv::types::u32));
			shaders.ps = drawPs.data(); shaders.psSize = (mv::types::u32)(drawPs.size() * sizeof(mv::types::u32));

			constexpr mv::types::u32 kChunkCorners =
				(mv::voxel::SculptVolume::kCells + 1) * (mv::voxel::SculptVolume::kCells + 1) *
				(mv::voxel::SculptVolume::kCells + 1);

			if (sculptGpu_.initialize(rhi_, shaders,
				{ mv::post::kChainFormat, kVelocityFormat },
				mv::rhi::ETextureFormat::eD32_SFLOAT,
				materialSystem_.sceneLayout(), materialSystem_.bindlessLayout(),
				mv::voxel::mcEdgeTable(), mv::voxel::mcTriTable(),
				kChunkCorners))
			{
				// One material for every chunk: bare rock, double-sided for tunnels.
				mv::material::MaterialDesc materialDesc{};
				materialDesc.constants.baseColorFactor[0] = 0.42f;
				materialDesc.constants.baseColorFactor[1] = 0.36f;
				materialDesc.constants.baseColorFactor[2] = 0.30f;
				materialDesc.constants.baseColorFactor[3] = 1.0f;
				materialDesc.constants.metallicFactor = 0.0f;
				materialDesc.constants.roughnessFactor = 0.95f;
				materialDesc.renderState.doubleSided = true;

				sculptGpu_.setMaterial(materialSystem_.createMaterial(materialDesc));

				OutputDebugStringA("Sculpt GPU: compute march + indirect draw ready\n");
			}
			else
			{
				OutputDebugStringA("Sculpt GPU: initialize FAILED\n");
			}
		}
	}

	// The fox: skin, skeleton and clips through the skinned loader, drawn by the
	// prop pass's deforming sibling. (Fox model CC-BY 4.0, PixelMannen / tomkranis.)
	if (gltfLoader_.loadSkinned(rhi_, materialSystem_, exeDir + "assets\\models\\Fox.glb", foxModel_))
	{
		const std::vector<mv::types::u32> skinVs = readShaderFile(useVulkan_ ? "skinned_prop.vs.spv" : "skinned_prop.vs.cso");
		const std::vector<mv::types::u32> skinPs = readShaderFile(useVulkan_ ? "skinned_prop.ps.spv" : "skinned_prop.ps.cso");

		if (!skinVs.empty() && !skinPs.empty())
		{
			mv::props::SkinnedPropRenderer::Shaders shaders{};
			shaders.vs = skinVs.data(); shaders.vsSize = (mv::types::u32)(skinVs.size() * sizeof(mv::types::u32));
			shaders.ps = skinPs.data(); shaders.psSize = (mv::types::u32)(skinPs.size() * sizeof(mv::types::u32));

			if (skinnedPropRenderer_.initialize(rhi_, shaders,
				{ mv::post::kChainFormat, kVelocityFormat },
				mv::rhi::ETextureFormat::eD32_SFLOAT,
				materialSystem_.sceneLayout(), materialSystem_.bindlessLayout(),
				rhi_->framesInFlight()))
			{
				foxAnimator_.bind(&foxModel_);

				// The fox is authored in centimetres; scale it to stand like a fox.
				const mv::types::f32 restHeight = foxModel_.boundsMax.y - foxModel_.boundsMin.y;
				foxScale_ = restHeight > 0.0f ? 1.0f / restHeight : 1.0f;

				// Walk by default; the survey idle is index 0 in this file.
				for (mv::types::u32 c = 0; c < foxAnimator_.clipCount(); c++)
				{
					if (std::string(foxAnimator_.clipName(c)) == "Walk")
						foxAnimator_.play(c);
				}

				foxLoaded_ = true;

				char message[128];
				sprintf_s(message, "Skinned props: fox, %zu joints, %zu clips\n",
					foxModel_.joints.size(), foxModel_.clips.size());
				OutputDebugStringA(message);
			}
			else
			{
				OutputDebugStringA("Skinned props: initialize FAILED\n");
			}
		}
	}
	else
	{
		OutputDebugStringA("Skinned props: Fox.glb missing or unskinned, disabled\n");
	}

	{
		const auto& stats = virtualTextures_.stats();

		char message[256];
		sprintf_s(message,
			"Virtual textures: %u textures, %u pages, %u atlases (%llu MB), %u dropped\n",
			stats.virtualTextureCount, stats.pageCount, stats.atlasCount,
			stats.atlasBytes / (1024ull * 1024ull), stats.droppedPages);

		OutputDebugStringA(message);
	}

	// --- terrain ---------------------------------------------------------------

	// Built after the model so its baked maps land in bindless slots above Sponza's, and
	// after the virtual texture system has been finalised: the terrain's maps are not
	// virtualised, and an unregistered slot reads a zero level count, which is exactly the
	// "sample the texture directly" path the shader already has.
	if (!terrain_.initialize(
			rhi_,
			&materialSystem_,
			mipGenerator_.isReady() ? &mipGenerator_ : nullptr,
			terrainBuilder_.isReady() ? &terrainBuilder_ : nullptr))
	{
		return false;
	}

	terrain_.build(terrainNoise_, terrainDesc_);

	uploadGrassHeightField();

	// The one entity the demo has. Registered here so the world is never empty and the
	// play-mode plumbing has something real to keep in sync.
	if (!world_.alive(playerEntity_))
		playerEntity_ = world_.create("Player");

	{
		char message[192];
		sprintf_s(message,
			"Terrain: %u x %u vertices, %u triangles, %u^2 maps, built on %s in %.0f ms\n",
			terrainDesc_.resolution, terrainDesc_.resolution,
			terrain_.triangleCount(), terrainDesc_.textureSize,
			terrain_.builtOnGpu() ? "GPU" : "CPU",
			terrain_.lastBuildMilliseconds());

		OutputDebugStringA(message);
	}

	scene_ = terrainScene_ ? &terrain_.model() : &model_;

	if (terrainScene_)
	{
		surveyTerrain();
	}
	else
	{
		// Sponza is an interior, so start inside it.
		enterScene();
	}

	if (!initializeVisibilityBuffer())
		return false;

	if (!initializeShadows())
		return false;

	if (!initializeEnvironment())
		return false;

	if (!initializePostProcess())
		return false;

	// Fills in the per-draw table and points the resolve pass at the active scene's
	// geometry. Deferred to here so the shadow and temporal state it also touches exists.
	activateScene();

	// These textures exist only now, and the scene bind groups were built before the model
	// was loaded, so their bindings are filled in here rather than at creation.
	for (mv::types::u32 i = 0; i < framesInFlight; i++)
	{
		rhi_->updateBindGroupTexture(sceneBindGroups_[i], 3, 0, shadowMap_.texture());
		rhi_->updateBindGroupTexture(sceneBindGroups_[i], 5, 0, environment_.cubemap());

		// Binding 6 has to name something whether or not the clouds came up, because the
		// shading pass references it statically. A material system texture stands in when they
		// did not; white is full transmittance, which is the right answer for no clouds even if
		// the strength constant already made it moot.
		rhi_->updateBindGroupTexture(sceneBindGroups_[i], 6, 0,
			clouds_.shadowTexture() != mv::INVALID_HANDLE ? clouds_.shadowTexture() : materialSystem_.whiteTexture());
	}

	// A no-op at zero, but it means a non-zero default is honoured without having to
	// touch the slider.
	materialSystem_.setForcedBaseMip((mv::types::u32)forcedBaseMip_);

	return true;
}

bool Engine::initializeVisibilityBuffer()
{
	// --- targets and per-draw table -------------------------------------------

	mv::rhi::TextureDesc vbDesc{};
	vbDesc.width = width_;
	vbDesc.height = height_;
	vbDesc.depth = 1;
	vbDesc.usage = mv::rhi::ETextureUsage::eColorAttachment | mv::rhi::ETextureUsage::eSampled;
	vbDesc.mipLevels = 1;
	vbDesc.format = mv::rhi::ETextureFormat::eR32G32_UINT;
	vbDesc.memoryType = mv::rhi::EMemoryType::eDeviceLocalImage;

	visibilityTexture_ = rhi_->createTexture(vbDesc);

	mv::rhi::BufferDesc drawInfoDesc{};
	drawInfoDesc.size = (mv::types::u64)kMaxDraws * sizeof(DrawInfo);
	drawInfoDesc.usage = mv::rhi::EBufferUsage::eStorage;
	drawInfoDesc.memoryType = mv::rhi::EMemoryType::eHostVisibleBuffer;

	drawInfoBuffer_ = rhi_->createBuffer(drawInfoDesc);

	// --- set 2: the geometry the resolve pass refetches ------------------------

	mv::rhi::BindGroupLayoutDesc resourceLayoutDesc{};
	for (mv::types::u32 binding = 0; binding < 3; binding++)
	{
		resourceLayoutDesc.bindings.push_back({
			.binding = binding, .count = 1,
			.type = mv::rhi::EDescriptorType::eStorageBuffer,
			.stages = mv::rhi::EShaderStage::eFragment });
	}
	resourceLayoutDesc.bindings.push_back({
		.binding = 3, .count = 1,
		.type = mv::rhi::EDescriptorType::eSampledImage,
		.stages = mv::rhi::EShaderStage::eFragment });
	resourceLayoutDesc.bindings.push_back({
		.binding = 4, .count = 1,
		.type = mv::rhi::EDescriptorType::eStorageBufferReadWrite,
		.stages = mv::rhi::EShaderStage::eFragment });

	vbResourceLayout_ = rhi_->createBindGroupLayout(resourceLayoutDesc);

	// --- streaming feedback ----------------------------------------------------

	// Sized for the whole bindless array so a texture registered later still has a slot.
	const mv::types::u32 feedbackCount = 4096;
	const mv::types::u64 feedbackSize = (mv::types::u64)feedbackCount * sizeof(mv::types::u32);

	mv::rhi::BufferDesc feedbackDesc{};
	feedbackDesc.size = feedbackSize;
	feedbackDesc.usage = mv::rhi::EBufferUsage::eStorageReadWrite | mv::rhi::EBufferUsage::eTransferSrc | mv::rhi::EBufferUsage::eTransferDst;
	feedbackDesc.memoryType = mv::rhi::EMemoryType::eDeviceLocalBuffer;

	feedbackBuffer_ = rhi_->createBuffer(feedbackDesc);

	mv::rhi::BufferDesc readbackDesc{};
	readbackDesc.size = feedbackSize;
	readbackDesc.usage = mv::rhi::EBufferUsage::eTransferDst;
	readbackDesc.memoryType = mv::rhi::EMemoryType::eReadback;

	feedbackReadback_ = rhi_->createBuffer(readbackDesc);

	// The buffer is only ever InterlockedMin'd into, so it has to start at the maximum.
	// The upload is the first frame's initial state; from then on a compute dispatch at
	// the top of the resolve pass resets it, which is what makes the readback describe
	// this frame rather than accumulating every mip the camera has ever asked for.
	{
		const std::vector<mv::types::u32> initial(feedbackCount, 0xFFFFFFFFu);
		rhi_->uploadBuffer(feedbackBuffer_, initial.data(), feedbackSize);
	}

	feedbackClear_.setTarget(feedbackBuffer_, feedbackCount);

	OutputDebugStringA(feedbackClear_.isReady()
		? "Streaming feedback: cleared per frame by compute\n"
		: "Streaming feedback: cumulative (bufferfill shader missing)\n");

	mv::rhi::BindGroupDesc resourceGroupDesc{};
	resourceGroupDesc.layout = vbResourceLayout_;
	resourceGroupDesc.storageBuffers.push_back({ .binding = 0, .buffer = scene_->vertexBuffer, .offset = 0, .stride = sizeof(mv::asset::ModelVertex), .count = scene_->vertexCount });
	resourceGroupDesc.storageBuffers.push_back({ .binding = 1, .buffer = scene_->indexBuffer, .offset = 0, .stride = sizeof(mv::types::u32), .count = scene_->indexCount });
	resourceGroupDesc.storageBuffers.push_back({ .binding = 2, .buffer = drawInfoBuffer_, .offset = 0, .stride = sizeof(DrawInfo), .count = kMaxDraws });
	resourceGroupDesc.sampledTextures.push_back({ .binding = 3, .texture = visibilityTexture_ });
	resourceGroupDesc.storageBuffers.push_back({ .binding = 4, .buffer = feedbackBuffer_, .offset = 0, .stride = sizeof(mv::types::u32), .count = feedbackCount });

	vbResourceGroup_ = rhi_->createBindGroup(resourceGroupDesc);

	// --- id pass ---------------------------------------------------------------

	const std::vector<mv::types::u32> vbVs = readShaderFile(useVulkan_ ? "vb.vs.spv" : "vb.vs.cso");
	const std::vector<mv::types::u32> vbPs = readShaderFile(useVulkan_ ? "vb.ps.spv" : "vb.ps.cso");
	if (vbVs.empty() || vbPs.empty())
		return false;

	mv::rhi::ShaderDesc vbVsDesc{ mv::rhi::EShaderType::eVertex, vbVs.data(), (mv::types::u32)(vbVs.size() * sizeof(mv::types::u32)), "VSMain" };
	mv::rhi::ShaderDesc vbPsDesc{ mv::rhi::EShaderType::eFragment, vbPs.data(), (mv::types::u32)(vbPs.size() * sizeof(mv::types::u32)), "PSMain" };

	mv::material::MaterialPipelineCache::Desc vbCacheDesc{};
	vbCacheDesc.vs = rhi_->createShader(vbVsDesc);
	vbCacheDesc.ps = rhi_->createShader(vbPsDesc);
	// The id pass takes the same sets and push constants as the forward path.
	vbCacheDesc.layout = materialSystem_.pipelineLayout();
	// One target: the id pass writes ids and nothing else.
	vbCacheDesc.colorFormats = { mv::rhi::ETextureFormat::eR32G32_UINT };
	vbCacheDesc.depthFormat = mv::rhi::ETextureFormat::eD32_SFLOAT;

	vbCacheDesc.vertexLayout.bindings.push_back({ .binding = 0, .stride = sizeof(mv::asset::ModelVertex), .perInstance = false });
	vbCacheDesc.vertexLayout.attributes.push_back({
		.location = 0, .semanticName = "POSITION", .semanticIndex = 0,
		.binding = 0, .format = mv::rhi::EVertexFormat::eFloat3, .offset = offsetof(mv::asset::ModelVertex, position) });
	vbCacheDesc.vertexLayout.attributes.push_back({
		.location = 1, .semanticName = "NORMAL", .semanticIndex = 0,
		.binding = 0, .format = mv::rhi::EVertexFormat::eFloat3, .offset = offsetof(mv::asset::ModelVertex, normal) });
	vbCacheDesc.vertexLayout.attributes.push_back({
		.location = 2, .semanticName = "TEXCOORD", .semanticIndex = 0,
		.binding = 0, .format = mv::rhi::EVertexFormat::eFloat2, .offset = offsetof(mv::asset::ModelVertex, uv) });

	vbPipelineCache_.initialize(rhi_, vbCacheDesc);

	// --- resolve pass ----------------------------------------------------------

	const std::vector<mv::types::u32> shadeVs = readShaderFile(useVulkan_ ? "vb_shade.vs.spv" : "vb_shade.vs.cso");
	const std::vector<mv::types::u32> shadePs = readShaderFile(useVulkan_ ? "vb_shade.ps.spv" : "vb_shade.ps.cso");
	if (shadeVs.empty() || shadePs.empty())
		return false;

	mv::rhi::PipelineLayoutDesc shadeLayoutDesc{};
	shadeLayoutDesc.bindGroups.push_back(materialSystem_.sceneLayout());
	shadeLayoutDesc.bindGroups.push_back(materialSystem_.bindlessLayout());
	shadeLayoutDesc.bindGroups.push_back(vbResourceLayout_);
	vbShadePipelineLayout_ = rhi_->createPipelineLayout(shadeLayoutDesc);

	mv::rhi::ShaderDesc shadeVsDesc{ mv::rhi::EShaderType::eVertex, shadeVs.data(), (mv::types::u32)(shadeVs.size() * sizeof(mv::types::u32)), "VSMain" };
	mv::rhi::ShaderDesc shadePsDesc{ mv::rhi::EShaderType::eFragment, shadePs.data(), (mv::types::u32)(shadePs.size() * sizeof(mv::types::u32)), "PSMain" };

	mv::rhi::GraphicsPipelineDesc shadeDesc{};
	shadeDesc.vs = rhi_->createShader(shadeVsDesc);
	shadeDesc.ps = rhi_->createShader(shadePsDesc);
	shadeDesc.layoutHandle = vbShadePipelineLayout_;
	shadeDesc.topology = mv::rhi::EPrimitiveTopology::eTriangleList;
	shadeDesc.rasterizer.cullMode = mv::rhi::ECullMode::eNone;
	shadeDesc.colorFormats.push_back(mv::post::kChainFormat);
	shadeDesc.colorFormats.push_back(kVelocityFormat);
	// The fullscreen triangle carries no geometry of its own and must not be depth tested
	// against the pass that produced the ids.
	shadeDesc.depthFormat = mv::rhi::ETextureFormat::eUndefined;
	shadeDesc.depth.depthTestEnable = false;
	shadeDesc.depth.depthWriteEnable = false;

	vbShadePipeline_ = rhi_->createGraphicsPipeline(shadeDesc);

	return true;
}

void Engine::deinitializeWindow()
{
	if (hwnd_)
	{
		DestroyWindow(hwnd_);
		hwnd_ = nullptr;
	}
}

void Engine::deinitializeRHI()
{
	if (rhi_)
	{
		rhi_->deinitialize();
		rhi_ = nullptr;
	}
}

void Engine::tick()
{
	// Before anything is recorded, so the frame that follows sees one consistent size.
	applyPendingResize();

	// A minimised window has no surface to present to. Drawing anyway means acquiring
	// from a zero-sized swap chain every frame, which is both wasted work and a stream of
	// out-of-date acquires to recover from. Nothing is presenting to pace the loop while
	// this is the case, so it has to pace itself.
	if (pendingWidth_ == 0 || pendingHeight_ == 0)
	{
		Sleep(16);
		return;
	}

	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();

	ImGui::SetNextWindowPos(ImVec2(20.0f, 20.0f), ImGuiCond_FirstUseEver);
	ImGui::Begin("RHI");

	// What stays visible whatever is being tuned: the backend, the frame rate and the
	// camera. Everything else lives in a tab -- eight sections open at once had turned the
	// panel into a scroll hunt, and a tab bar is the ImGui idiom for "one of these at a
	// time is what you are working on".
	ImGui::Text("Backend: %s", useVulkan_ ? "Vulkan 1.4" : "D3D12");
	ImGui::Text("%.1f FPS (%.3f ms/frame)", ImGui::GetIO().Framerate, 1000.0f / ImGui::GetIO().Framerate);
	ImGui::Separator();
	ImGui::TextUnformatted("RMB look, WASD move, Q/E down/up, Shift fast, V debug, B virtual textures");
	ImGui::Text("Pos: %.1f %.1f %.1f", cameraPosition_.x, cameraPosition_.y, cameraPosition_.z);
	ImGui::SliderFloat("Speed", &cameraSpeed_, 0.05f, 50.0f, "%.2f", ImGuiSliderFlags_Logarithmic);
	if (ImGui::Button("Frame model")) frameModel();
	ImGui::SameLine();
	if (ImGui::Button("Enter scene")) enterScene();
	if (terrainScene_)
	{
		ImGui::SameLine();
		if (ImGui::Button("Survey")) surveyTerrain();
	}
	ImGui::Separator();

	// One thing to know about widgets in tabs: only the selected tab's widgets run at all.
	// The dirty-flag idiom the sections use ("rebuild once the slider is let go") still
	// works, because a flag set on one visit is spent on the next -- it just cannot be
	// spent while its tab is hidden, which for a rebuild trigger is the right behaviour
	// anyway.
	if (ImGui::BeginTabBar("sections"))
	{

	if (ImGui::BeginTabItem("Scene"))
	{
		ImGui::Text("Frames in flight: %u", rhi_->framesInFlight());
		ImGui::Text("Primitives: %d", (int)scene_->primitives.size());
		ImGui::Text("Materials:  %u", materialSystem_.materialCount());
		ImGui::Text("Bindless textures: %u", materialSystem_.textureCount());
		ImGui::Separator();
		ImGui::Checkbox("Visibility buffer", &useVisibilityBuffer_);
		ImGui::TextDisabled(useVisibilityBuffer_ ? "id pass + fullscreen resolve" : "forward shading");

		ImGui::BeginDisabled(!useVisibilityBuffer_);
		ImGui::Combo("Debug", &debugMode_, kDebugModeNames, kDebugModeCount);
		ImGui::EndDisabled();
		ImGui::Separator();
		ImGui::SliderFloat("Light", &lightIntensity_, 0.0f, 10.0f);
		ImGui::SliderFloat("Ambient", &ambientIntensity_, 0.0f, 1.0f);

		ImGui::SeparatorText("Play");

		ImGui::BeginDisabled(!terrainScene_ || !groundField_.valid());

		bool playing = !gameState_.is(mv::game::EGameState::eTitle);

		if (ImGui::Checkbox("Play mode", &playing))
		{
			// The checkbox is a view of the state machine, not a second authority:
			// it requests the same transitions the keyboard does.
			if (playing)
				startPlay();
			else
				exitToTitle();
		}

		ImGui::SameLine();
		ImGui::TextDisabled(
			gameState_.is(mv::game::EGameState::ePaused) ? "(paused)" :
			gameState_.is(mv::game::EGameState::ePlaying) ? "(playing)" : "(title)");

		ImGui::Checkbox("HUD", &hudEnabled_);

		ImGui::EndDisabled();

		ImGui::TextDisabled("WASD walk, Shift run, Space jump, E helmet, Q crate, RMB look, ESC exit");

		if (const mv::game::Entity* entity = world_.get(playerEntity_))
		{
			ImGui::TextDisabled("%s: %.1f %.1f %.1f  (entities: %u, bodies: %u)%s",
				entity->name,
				entity->transform.position.x,
				entity->transform.position.y,
				entity->transform.position.z,
				world_.aliveCount(),
				physics_.bodyCount(),
				player_.grounded() ? "  on ground" : "  airborne");
		}

		// What the crosshair ray landed on, straight out of Bullet's rayTest: the
		// proof the one call sees terrain, water and props alike.
		if (gameState_.is(mv::game::EGameState::ePlaying))
		{
			if (aimValid_)
			{
				const char* kindNames[] = { "nothing", "terrain", "water", "prop", "character", "sculpt" };

				ImGui::TextDisabled("Aim: %s  %.1f m", kindNames[(mv::types::u32)aimHit_.kind], aimHit_.distance);
			}
			else
			{
				ImGui::TextDisabled("Aim: sky");
			}
		}

		ImGui::SliderFloat("Eye height", &playerParams_.eyeHeight, 1.0f, 4.0f, "%.1f m");

		if (ImGui::Checkbox("Physics debug draw", &physicsDebugDraw_) && physicsDebugDraw_ &&
			!physicsDebugRenderer_.isReady())
		{
			physicsDebugDraw_ = false;
		}

		if (sculptGpu_.isReady())
		{
			// Turning the waves off queues one static march so the carved ground
			// settles back exactly where the GPU densities say it is.
			if (ImGui::Checkbox("Waves", &sculptAnimate_) && !sculptAnimate_ && sculptPlaced_)
			{
				sculptGpu_.queueRemeshAll();
			}

			if (sculptAnimate_)
			{
				ImGui::SliderFloat("Wave amplitude", &sculptWaveAmplitude_, 0.0f, 2.0f, "%.2f m");
				ImGui::SliderFloat("Wave length", &sculptWaveLength_, 2.0f, 20.0f, "%.1f m");
				ImGui::SliderFloat("Wave speed", &sculptWaveSpeed_, 0.0f, 5.0f);
			}
		}

		if (physicsDebugDraw_)
			ImGui::TextDisabled("%u lines", (mv::types::u32)physicsDebugLines_.size());

		if (audio_.isReady())
		{
			float volume = audio_.masterVolume();

			if (ImGui::SliderFloat("Master volume", &volume, 0.0f, 1.0f))
				audio_.setMasterVolume(volume);

			ImGui::TextDisabled("audio: %u voices, %u plays", audio_.activeVoices(), audio_.totalPlays());
		}

		if (foxLoaded_)
		{
			ImGui::SeparatorText("Fox");

			const mv::types::u32 current = foxAnimator_.currentClip();

			if (ImGui::BeginCombo("Clip", foxAnimator_.clipName(current)))
			{
				for (mv::types::u32 c = 0; c < foxAnimator_.clipCount(); c++)
				{
					if (ImGui::Selectable(foxAnimator_.clipName(c), c == current))
						foxAnimator_.play(c);
				}

				ImGui::EndCombo();
			}

			ImGui::SliderFloat("Anim speed", &foxSpeedScale_, 0.0f, 2.0f);

			ImGui::TextDisabled("%zu joints, at %.0f %.0f %.0f",
				foxModel_.joints.size(), foxPosition_.x, foxPosition_.y, foxPosition_.z);
		}

		ImGui::EndTabItem();
	}

	if (ImGui::BeginTabItem("Clouds"))
	{
		ImGui::BeginDisabled(!clouds_.isReady());
		ImGui::Checkbox("Volumetric clouds", &cloudsEnabled_);
		ImGui::EndDisabled();

		if (!clouds_.isReady())
		{
			ImGui::TextDisabled("shaders missing");
		}
		else
		{
			ImGui::TextDisabled("%u^3 shape + %u^3 detail volumes, march at %ux%u, baked in %.0f ms",
				mv::clouds::kShapeSize, mv::clouds::kDetailSize,
				clouds_.lowResWidth(), clouds_.lowResHeight(),
				clouds_.lastBakeMilliseconds());

			// Only coverage and the seed change what is baked; everything else is read by
			// the march and takes effect immediately.
			bool& dirty = cloudsDirty_;

			ImGui::SeparatorText("Weather");

			dirty |= ImGui::SliderFloat("Coverage", &cloudParams_.coverage, 0.0f, 1.0f);

			int seed = (int)cloudParams_.seed;
			if (ImGui::InputInt("Cloud seed", &seed))
			{
				cloudParams_.seed = (mv::types::u32)seed;
				dirty = true;
			}

			ImGui::SameLine();
			if (ImGui::Button("Roll##clouds"))
			{
				cloudParams_.seed = cloudParams_.seed * 1664525u + 1013904223u;
				dirty = true;
			}

			ImGui::SliderFloat("Wind speed", &cloudParams_.windSpeed, 0.0f, 200.0f);

			ImGui::SeparatorText("Layer");

			ImGui::SliderFloat("Bottom", &cloudParams_.layerBottom, 200.0f, 6000.0f, "%.0f m");
			ImGui::SliderFloat("Top", &cloudParams_.layerTop, 400.0f, 12000.0f, "%.0f m");
			ImGui::SliderFloat("Shape scale", &cloudParams_.shapeScale, 2000.0f, 40000.0f, "%.0f m");
			ImGui::SliderFloat("Detail scale", &cloudParams_.detailScale, 100.0f, 4000.0f, "%.0f m");
			ImGui::SliderFloat("Weather scale", &cloudParams_.weatherScale, 5000.0f, 200000.0f, "%.0f m");

			// A smaller planet bends the layer down to the horizon sooner, which is what
			// keeps a slab from running to infinity in a scene a few hundred metres across.
			ImGui::SliderFloat("Planet radius", &cloudParams_.planetRadius, 20000.0f, 6360000.0f, "%.0f m", ImGuiSliderFlags_Logarithmic);

			ImGui::SeparatorText("Scattering");

			ImGui::SliderFloat("Density", &cloudParams_.densityScale, 0.0f, 4.0f);
			ImGui::SliderFloat("Extinction", &cloudParams_.extinction, 0.001f, 0.5f, "%.3f", ImGuiSliderFlags_Logarithmic);
			ImGui::SliderFloat("Detail erosion", &cloudParams_.detailStrength, 0.0f, 1.0f);
			ImGui::SliderFloat("Forward lobe", &cloudParams_.forwardScattering, 0.0f, 0.99f);
			ImGui::SliderFloat("Backward lobe", &cloudParams_.backwardScattering, 0.0f, 0.99f);
			ImGui::SliderFloat("Lobe blend", &cloudParams_.scatterBlend, 0.0f, 1.0f);
			ImGui::SliderFloat("Ambient", &cloudParams_.ambientStrength, 0.0f, 2.0f);

			ImGui::SeparatorText("Onto the scene");

			ImGui::SliderFloat("Shadow strength", &cloudParams_.shadowStrength, 0.0f, 1.0f);
			ImGui::SliderFloat("Shadow extent", &cloudParams_.shadowExtent, 500.0f, 40000.0f, "%.0f m", ImGuiSliderFlags_Logarithmic);

			int shadowSteps = (int)cloudParams_.shadowSteps;
			if (ImGui::SliderInt("Shadow steps", &shadowSteps, 4, 64))
				cloudParams_.shadowSteps = (mv::types::u32)shadowSteps;

			// Worth saying out loud, because the first reaction to a correctly implemented
			// cloud shadow here is that it is not working. The terrain is 240 metres across
			// and one cloud is twelve thousand, so the whole of it sits under a fiftieth of
			// a single cloud: the shadow is real, but it is a uniform dimming that takes
			// minutes to sweep past rather than a shape crossing the ground. Shrinking the
			// shape and weather scales is what brings the shapes down to the scene's size.
			ImGui::TextDisabled("Cloud shapes are %.0fx the terrain: expect uniform dimming",
				cloudParams_.shapeScale / (std::max)(terrainDesc_.worldSize, 1.0f));

			ImGui::SeparatorText("Cost");

			int viewSteps = (int)cloudParams_.viewSteps;
			if (ImGui::SliderInt("View steps", &viewSteps, 8, 256))
				cloudParams_.viewSteps = (mv::types::u32)viewSteps;

			int lightSteps = (int)cloudParams_.lightSteps;
			if (ImGui::SliderInt("Light steps", &lightSteps, 1, 16))
				cloudParams_.lightSteps = (mv::types::u32)lightSteps;

			ImGui::SliderFloat("Max distance", &cloudParams_.maxDistance, 5000.0f, 300000.0f, "%.0f m", ImGuiSliderFlags_Logarithmic);

			if (ImGui::Button("Rebake volumes") || (dirty && !ImGui::IsAnyItemActive()))
			{
				cloudsDirty_ = false;

				rhi_->waitIdle();
				clouds_.bake(cloudParams_);

				// The cube carries the layer now, so a different layer is a different cube.
				environmentDirty_ = true;
			}
		}

		ImGui::EndTabItem();
	}

	if (ImGui::BeginTabItem("Terrain"))
	{
		if (ImGui::Checkbox("Terrain scene", &terrainScene_))
		{
			activateScene();

			if (terrainScene_)
			{
				surveyTerrain();
			}
			else
			{
				enterScene();
			}
		}

		ImGui::TextDisabled("%u triangles, %u^2 maps, built on %s in %.0f ms",
			terrain_.triangleCount(), terrain_.desc().textureSize,
			terrain_.builtOnGpu() ? "GPU" : "CPU",
			terrain_.lastBuildMilliseconds());

		// Edits accumulate into a flag rather than rebuilding as they happen: a rebuild is
		// a device wait and a hundred milliseconds of generation, which is not something to
		// do on every frame of a slider drag. It is spent when the mouse comes back up.
		bool& dirty = terrainDirty_;

		ImGui::SeparatorText("Noise");

		const char* const basisNames[] = { "Value", "Perlin", "Simplex", "Worley" };
		int basis = (int)terrainNoise_.basis;
		if (ImGui::Combo("Basis", &basis, basisNames, 4))
		{
			terrainNoise_.basis = (mv::noise::EBasis)basis;
			dirty = true;
		}

		const char* const fractalNames[] = { "Single", "fBm", "Ridged", "Billow" };
		int fractal = (int)terrainNoise_.fractal;
		if (ImGui::Combo("Fractal", &fractal, fractalNames, 4))
		{
			terrainNoise_.fractal = (mv::noise::EFractal)fractal;
			dirty = true;
		}

		int octaves = (int)terrainNoise_.octaves;
		if (ImGui::SliderInt("Octaves", &octaves, 1, 10))
		{
			terrainNoise_.octaves = (mv::types::u32)octaves;
			dirty = true;
		}

		dirty |= ImGui::SliderFloat("Frequency", &terrainNoise_.frequency, 0.5f, 16.0f);
		dirty |= ImGui::SliderFloat("Lacunarity", &terrainNoise_.lacunarity, 1.5f, 3.0f);
		dirty |= ImGui::SliderFloat("Gain", &terrainNoise_.gain, 0.2f, 0.8f);
		dirty |= ImGui::SliderFloat("Warp", &terrainNoise_.warpStrength, 0.0f, 0.6f);
		dirty |= ImGui::SliderFloat("Warp frequency", &terrainNoise_.warpFrequency, 0.5f, 8.0f);

		int seed = (int)terrainNoise_.seed;
		if (ImGui::InputInt("Seed", &seed))
		{
			terrainNoise_.seed = (mv::types::u32)seed;
			dirty = true;
		}

		ImGui::SameLine();
		if (ImGui::Button("Roll"))
		{
			terrainNoise_.seed = terrainNoise_.seed * 1664525u + 1013904223u;
			dirty = true;
		}

		ImGui::SeparatorText("Shape");

		int resolution = (int)terrainDesc_.resolution;
		if (ImGui::SliderInt("Resolution", &resolution, 65, 1025))
		{
			// Keeping it to 2^n + 1 means the quads divide evenly and the vertex spacing
			// stays uniform, which matters because the normals are differenced from it.
			int power = 64;
			while (power < resolution - 1) power *= 2;

			terrainDesc_.resolution = (mv::types::u32)power + 1;
			dirty = true;
		}

		dirty |= ImGui::SliderFloat("World size", &terrainDesc_.worldSize, 50.0f, 8000.0f, "%.0f m", ImGuiSliderFlags_Logarithmic);
		dirty |= ImGui::SliderFloat("Height", &terrainDesc_.heightScale, 2.0f, 1000.0f, "%.0f m", ImGuiSliderFlags_Logarithmic);

		ImGui::SeparatorText("Surface");

		dirty |= ImGui::SliderFloat("Rock height", &terrainDesc_.rockHeight, 0.0f, 1.0f);
		dirty |= ImGui::SliderFloat("Snow height", &terrainDesc_.snowHeight, 0.0f, 1.0f);
		dirty |= ImGui::SliderFloat("Rock slope", &terrainDesc_.rockSlope, 0.0f, 1.0f);
		// Drives the sand band in the baked material. The water surface follows it rather
		// than the other way round, so there is one waterline and not two.
		if (ImGui::SliderFloat("Shore height", &terrainDesc_.waterHeight, 0.0f, terrainDesc_.heightScale, "%.0f m"))
		{
			waterParams_.level = terrainDesc_.waterHeight;
			dirty = true;
		}

		if (ImGui::Button("Regenerate") || (dirty && !ImGui::IsAnyItemActive()))
		{
			terrainDirty_ = false;
			rebuildTerrain();
		}

		ImGui::SeparatorText("Grass");

		ImGui::Checkbox("Grass", &grassParams_.enabled);

		ImGui::SliderFloat("Grass radius", &grassParams_.radius, 20.0f, 400.0f, "%.0f m");

		int bladesPerSide = (int)grassParams_.bladesPerSide;
		if (ImGui::SliderInt("Grass grid", &bladesPerSide, 50, 600))
			grassParams_.bladesPerSide = (mv::types::u32)bladesPerSide;

		ImGui::SliderFloat("Blade height", &grassParams_.bladeHeight, 0.1f, 2.0f, "%.2f m");
		ImGui::SliderFloat("Blade width", &grassParams_.bladeWidth, 0.01f, 0.3f, "%.2f m");
		ImGui::SliderFloat("Grass density", &grassParams_.density, 0.0f, 1.0f);
		ImGui::SliderFloat("Grass wind", &grassParams_.windStrength, 0.0f, 1.0f);

		ImGui::ColorEdit3("Root colour", &grassParams_.rootColor.x);
		ImGui::ColorEdit3("Tip colour", &grassParams_.tipColor.x);

		ImGui::EndTabItem();
	}

	if (ImGui::BeginTabItem("Water"))
	{
		ImGui::BeginDisabled(!water_.isReady());

		ImGui::Checkbox("Water surface", &waterParams_.enabled);

		if (!water_.isReady())
			ImGui::TextDisabled("shaders missing");
		else
			ImGui::TextDisabled("analytic plane, four octaves of drifting gradient noise");

		if (ImGui::SliderFloat("Level", &waterParams_.level, 0.0f, terrainDesc_.heightScale, "%.0f m"))
		{
			// The sand band in the baked material is the same line, so moving the surface
			// moves the shore. A rebuild, which is why this is not free to drag.
			terrainDesc_.waterHeight = waterParams_.level;
			terrainDirty_ = true;
		}

		ImGui::SeparatorText("Waves");

		ImGui::SliderFloat("Wave scale", &waterParams_.waveScale, 2.0f, 120.0f, "%.0f m");
		ImGui::SliderFloat("Wave height", &waterParams_.waveHeight, 0.0f, 1.0f);
		ImGui::SliderFloat("Wave speed", &waterParams_.waveSpeed, 0.0f, 4.0f);
		ImGui::SliderFloat("Roughness", &waterParams_.roughness, 0.01f, 0.4f);

		ImGui::SeparatorText("Body");

		ImGui::ColorEdit3("Scatter", &waterParams_.scatterColor.x);
		ImGui::SliderFloat3("Extinction", &waterParams_.extinction.x, 0.0f, 0.5f, "%.3f /m");
		ImGui::SliderFloat("Shore fade", &waterParams_.shoreFade, 0.1f, 40.0f, "%.1f m");

		ImGui::SeparatorText("Surface");

		ImGui::SliderFloat("Reflection", &waterParams_.reflectionStrength, 0.0f, 1.0f);

		// The screen-space part of it: where the reflected ray lands on something visible,
		// the scene's own colour replaces the cube. Zero falls back to cube-only.
		ImGui::SliderFloat("SSR", &waterParams_.ssrStrength, 0.0f, 1.0f);

		ImGui::SliderFloat("Sun glint", &waterParams_.specularStrength, 0.0f, 60.0f);

		ImGui::EndDisabled();

		ImGui::EndTabItem();
	}

	if (ImGui::BeginTabItem("Post"))
	{
		ImGui::TextDisabled("scene target is %s, chain runs in half float",
			"R16G16B16A16_SFLOAT");

		postStack_.ui();

		ImGui::EndTabItem();
	}

	if (ImGui::BeginTabItem("Sky"))
	{
		ImGui::Checkbox("Skybox", &skyboxEnabled_);
		ImGui::SliderFloat("IBL intensity", &iblIntensity_, 0.0f, 4.0f);

		// Moving the sun invalidates the sky, its coefficients and its prefiltered chain
		// all at once, which is the point of driving them from one vector.
		//
		// The rebuild waits for the slider to be released rather than following the drag:
		// a bake costs a couple of seconds here, and running one per frame while dragging
		// would freeze the UI it is being dragged in.
		bool sunMoved = false;

		ImGui::SliderFloat("Sun azimuth", &sunAzimuth_, 0.0f, 6.283f);
		sunMoved |= ImGui::IsItemDeactivatedAfterEdit();

		ImGui::SliderFloat("Sun elevation", &sunElevation_, -0.2f, 1.55f);
		sunMoved |= ImGui::IsItemDeactivatedAfterEdit();

		ImGui::SliderFloat("Turbidity", &skyParams_.turbidity, 1.5f, 10.0f);
		sunMoved |= ImGui::IsItemDeactivatedAfterEdit();

		ImGui::SliderFloat("Sun intensity", &skyParams_.sunIntensity, 1.0f, 60.0f);
		sunMoved |= ImGui::IsItemDeactivatedAfterEdit();

		if (sunMoved)
		{
			environmentDirty_ = true;
		}

		ImGui::TextDisabled("bake: %.1f ms on %s  (%ux%u cube, %u mips)",
			environment_.lastBakeMilliseconds(),
			environment_.bakedOnGpu() ? "GPU" : "CPU",
			mv::env::kCubeFaceSize, mv::env::kCubeFaceSize, mv::env::kMipCount);

		// The nine numbers the whole diffuse response is reconstructed from. Band 0 is the
		// average radiance over the sphere, band 1 its directional tilt.
		if (ImGui::TreeNode("SH coefficients"))
		{
			const float* sh = environment_.shCoefficients();
			static const char* kNames[] = { "L00", "L1-1", "L10", "L11", "L2-2", "L2-1", "L20", "L21", "L22" };

			for (mv::types::u32 i = 0; i < mv::env::kShCoefficientCount; i++)
			{
				ImGui::Text("%-5s % .4f % .4f % .4f", kNames[i], sh[i * 3 + 0], sh[i * 3 + 1], sh[i * 3 + 2]);
			}

			ImGui::TreePop();
		}

		ImGui::SeparatorText("Fog");

		ImGui::Checkbox("Height fog", &fogParams_.enabled);

		// Small numbers on purpose: at 0.0005 a ridge two kilometres out keeps about a
		// third of its contrast, which reads as distance rather than as weather.
		ImGui::SliderFloat("Fog density", &fogParams_.density, 0.00005f, 0.005f, "%.5f /m", ImGuiSliderFlags_Logarithmic);
		ImGui::SliderFloat("Fog falloff", &fogParams_.heightFalloff, 0.0005f, 0.05f, "%.4f /m", ImGuiSliderFlags_Logarithmic);
		ImGui::SliderFloat("Fog start", &fogParams_.startDistance, 0.0f, 2000.0f, "%.0f m");
		ImGui::SliderFloat("Fog opacity cap", &fogParams_.maxOpacity, 0.0f, 1.0f);

		ImGui::SeparatorText("Light shafts");

		ImGui::SliderFloat("Shaft intensity", &fogParams_.shaftIntensity, 0.0f, 3.0f);
		ImGui::SliderFloat("Shaft anisotropy", &fogParams_.shaftAnisotropy, 0.0f, 0.95f);
		ImGui::SliderFloat("Shaft distance", &fogParams_.shaftDistance, 200.0f, 6000.0f, "%.0f m");

		int shaftSteps = (int)fogParams_.shaftSteps;
		if (ImGui::SliderInt("Shaft steps", &shaftSteps, 0, 64))
			fogParams_.shaftSteps = (mv::types::u32)shaftSteps;

		ImGui::EndTabItem();
	}

	if (ImGui::BeginTabItem("Shadows"))
	{
		ImGui::Checkbox("Cascaded shadows", &shadowEnabled_);
		ImGui::TextDisabled("%d cascades, %ux%u atlas",
			mv::shadow::kMaxCascades, mv::shadow::kAtlasSize, mv::shadow::kAtlasSize);

		// 1 is purely logarithmic, which is what makes the projected texel size roughly
		// constant; dragging towards 0 hands the near cascades their resolution back to
		// the far ones and the difference is obvious in the cascade debug view.
		ImGui::SliderFloat("Split lambda", &shadowLambda_, 0.0f, 1.0f);
		ImGui::SliderFloat("Shadow distance", &shadowDistance_, 5.0f, 8000.0f, "%.0f m", ImGuiSliderFlags_Logarithmic);
		ImGui::SliderFloat("Split near", &shadowNearDistance_, 0.05f, 5.0f);
		ImGui::SliderFloat("Depth bias", &shadowDepthBias_, 0.0f, 0.01f, "%.5f");
		ImGui::SliderFloat("Normal bias", &shadowNormalBias_, 0.0f, 8.0f);
		ImGui::SliderInt("PCF radius", &shadowPcfRadius_, 0, 3);
		ImGui::SliderFloat("Cascade blend", &shadowCascadeBlend_, 0.0f, 0.5f);

		for (mv::types::u32 i = 0; i < mv::shadow::kMaxCascades; i++)
		{
			const auto& cascade = shadowMap_.cascade(i);
			ImGui::TextDisabled("  cascade %u: to %.1f m, %.3f m/texel", i, cascade.splitDepth, cascade.texelWorldSize);
		}

		ImGui::EndTabItem();
	}

	if (ImGui::BeginTabItem("Streaming"))
	{
		const auto& vtStats = virtualTextures_.stats();

		// The one control that matters here: with every page resident, turning this off has
		// to leave the image unchanged, because the page table is resolving to exactly the
		// texels the direct path would have sampled.
		ImGui::Checkbox("Virtual textures", &vtEnabled_);
		ImGui::TextDisabled("%u textures, %u pages, %u atlases (%llu MB)",
			vtStats.virtualTextureCount, vtStats.pageCount, vtStats.atlasCount,
			(unsigned long long)(vtStats.atlasBytes / (1024ull * 1024ull)));

		// Refining for an elongated footprint costs resident pages, which is why this is a
		// trade rather than a setting to turn up.
		ImGui::SliderFloat("VT anisotropy", &vtMaxAnisotropy_, 1.0f, 16.0f, "%.0f:1");

		if (vtStats.droppedPages > 0)
		{
			ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1.0f), "%u pages did not fit", vtStats.droppedPages);
		}

		ImGui::Separator();
		if (ImGui::SliderInt("Forced base mip", &forcedBaseMip_, 0, 6))
		{
			// Exercises the mip-range views a streaming system will use to hide levels that
			// have not arrived yet.
			rhi_->waitIdle();
			materialSystem_.setForcedBaseMip((mv::types::u32)forcedBaseMip_);
		}

		if (useVisibilityBuffer_)
		{
			ImGui::SeparatorText("Feedback");

			// Two frames stale by construction, which is the point: reading it never stalls.
			if (const mv::types::u32* feedback = static_cast<const mv::types::u32*>(rhi_->mapBuffer(feedbackReadback_)))
			{
				const mv::types::u32 count = materialSystem_.textureCount();
				mv::types::u32 requested = 0;

				for (mv::types::u32 i = 0; i < count; i++)
				{
					if (feedback[i] != 0xFFFFFFFFu) requested++;
				}

				ImGui::Text("Textures seen on screen: %u / %u", requested, count);

				ImGui::BeginChild("feedback", ImVec2(0.0f, 140.0f), ImGuiChildFlags_Borders);
				for (mv::types::u32 i = 0; i < count; i++)
				{
					if (feedback[i] == 0xFFFFFFFFu) continue;

					ImGui::Text("texture %3u -> mip %u", i, feedback[i]);
				}
				ImGui::EndChild();

				rhi_->unmapBuffer(feedbackReadback_);
			}
		}

		ImGui::EndTabItem();
	}

	ImGui::EndTabBar();
	}

	ImGui::End();

	ImGui::Render();

	updateCamera(ImGui::GetIO().DeltaTime);

	// The layer drifts on its own clock, so a frame the camera did not move still advances
	// the weather.
	clouds_.advance(ImGui::GetIO().DeltaTime);
	water_.advance(ImGui::GetIO().DeltaTime, waterParams_);
	grass_.advance(ImGui::GetIO().DeltaTime, grassParams_);

	mv::rhi::FrameContext context = rhi_->beginFrame();

	// The pass lambda takes its own rg::Context parameter, which shadows this one.
	const mv::types::u32 frameIndex = context.currentFrameIndex;

	const mv::math::Vec3 eye = cameraPosition_;
	const mv::math::Vec3 forward = mv::math::normalize({
		std::cos(cameraPitch_) * std::sin(cameraYaw_),
		std::sin(cameraPitch_),
		-std::cos(cameraPitch_) * std::cos(cameraYaw_) });

	// A walking eye needs a walking near plane. The fly camera's near is scaled to
	// the scene -- metres, so a ten-kilometre depth range stays usable -- but at
	// boot height that same near clips the slope right in front of the boots and
	// shows the terrain's underside. On foot the planes swap to human scale, the
	// far pulled in with the near so the depth budget is not spent on the first
	// step. Every pass that linearises depth reads these, not the fly values.
	const bool onFoot = !gameState_.is(mv::game::EGameState::eTitle);

	activeNear_ = onFoot ? 0.25f : cameraNear_;
	activeFar_ = onFoot ? (std::min)(cameraFar_, 4000.0f) : cameraFar_;

	const mv::math::Mat4 view = mv::math::lookAtRH(eye, eye + forward, { 0.0f, 1.0f, 0.0f });
	mv::math::Mat4 projection = mv::math::perspectiveRH(
		1.0472f, // 60 degrees
		(float)width_ / (float)height_,
		activeNear_,
		activeFar_);

	// Row-vector convention: view is applied first, then the projection.
	//
	// The unjittered pair is what reprojection uses. Mixing the two would make the history
	// lookup chase the sub-pixel offset instead of the surface.
	const mv::math::Mat4 viewProjNoJitter = view * projection;

	// Temporal anti-aliasing offsets the camera by a fraction of a pixel each frame. It
	// goes into the projection rather than the viewport so that everything rasterised this
	// frame moves together, and it is expressed as a shear on the z row because that is
	// what adds a constant offset in normalised device coordinates at any depth.
	const mv::math::Vec3 jitter = taa_ ? taa_->jitter(frameCounter_, width_, height_) : mv::math::Vec3{};

	projection.m[8] -= jitter.x;
	projection.m[9] -= jitter.y;

	const mv::math::Mat4 viewProj = view * projection;

	// The four side planes of the view frustum, for the grass cull. In the row-vector
	// convention a clip coordinate is dot(position, column), so the planes fall out of
	// column sums: left = col3 + col0, right = col3 - col0, and likewise for y. The four
	// side planes meet at the eye, so together they also reject everything behind the
	// camera, and the far plane is redundant against a field that ends at its own radius.
	float grassPlanes[4][4];

	for (int i = 0; i < 4; i++)
	{
		const int axis = i / 2;
		const float sign = (i % 2 == 0) ? 1.0f : -1.0f;

		float p[4];
		for (int r = 0; r < 4; r++)
			p[r] = viewProj.m[r * 4 + 3] + sign * viewProj.m[r * 4 + axis];

		const float length = std::sqrt(p[0] * p[0] + p[1] * p[1] + p[2] * p[2]);

		for (int c = 0; c < 4; c++)
			grassPlanes[i][c] = p[c] / (std::max)(length, 1e-6f);
	}

	// Mirrors the SceneConstants cbuffer in model.hlsl.
	struct SceneConstants
	{
		float viewProj[16];
		float cameraPosition[3];  mv::types::u32 debugMode;
		float lightDirection[3];  mv::types::u32 vtEnabled;
		float lightColor[3];      float ambientIntensity;
		float cameraForward[3];   mv::types::u32 shadowEnabled;

		float cascadeViewProj[mv::shadow::kMaxCascades][16];
		float cascadeSplits[4];
		float cascadeTexelSize[4];

		float shadowDepthBias;
		float shadowNormalBias;
		mv::types::s32 shadowPcfRadius;
		float iblIntensity;

		float viewportSize[2];
		float jitterUv[2];

		float shadowCascadeBlend;
		float forcedBaseMip;
		float vtMaxAnisotropy;
		float cloudShadowStrength;

		float cloudShadowOrigin[3];
		float cloudShadowInvExtent;

		float cloudShadowRight[3];
		float scenePad0;

		float cloudShadowUp[3];
		float scenePad1;

		// Padded to float4 each: a cbuffer array element is always sixteen bytes.
		float shCoefficients[mv::env::kShCoefficientCount][4];

		float prevViewProj[16];
	} sceneConstants{};

	// The buffer these go into is sized by hand, and writeBuffer does not bounds check.
	// Growing this struct past it silently corrupts whatever was suballocated next, which
	// on D3D12 is padding and on Vulkan is another live buffer.
	static_assert(sizeof(SceneConstants) <= kSceneConstantsSize, "SceneConstants outgrew its buffer");

	// Only the visibility buffer resolve honours this; the forward path has no ids to show.
	sceneConstants.debugMode = useVisibilityBuffer_ ? (mv::types::u32)debugMode_ : 0;
	sceneConstants.vtEnabled = vtEnabled_ ? 1u : 0u;

	memcpy(sceneConstants.viewProj, viewProj.m, sizeof(sceneConstants.viewProj));

	// The PBR view vector needs the eye position, which the baked-in world space vertices
	// cannot supply on their own.
	sceneConstants.cameraPosition[0] = eye.x;
	sceneConstants.cameraPosition[1] = eye.y;
	sceneConstants.cameraPosition[2] = eye.z;

	// The sun, from its azimuth and elevation. This one vector drives the sky, the shadow
	// cascades and the irradiance, so moving it moves all three together.
	const mv::math::Vec3 lightDirection = mv::math::normalize({
		-std::cos(sunElevation_) * std::sin(sunAzimuth_),
		-std::sin(sunElevation_),
		-std::cos(sunElevation_) * std::cos(sunAzimuth_) });

	// Baking sweeps six faces and their prefiltered chain, so it waits for the sun to
	// actually move rather than running every frame.
	if (environmentDirty_)
	{
		skyParams_.lightDirection = lightDirection;

		// The cloud layer goes into the cube too, so the irradiance coefficients and the
		// prefiltered reflections both carry it. Without this a surface under an overcast
		// sky is lit by, and mirrors back, a clear blue one.
		//
		// Frozen at the moment of the bake rather than following the wind: sweeping six
		// faces and their chain costs tens of milliseconds, so it happens when the sun or
		// the cloud parameters move. The layer drifts at metres per second across a scene
		// whose reflections are a blurred mip chain, which is the one place that does not
		// show.
		mv::compute::EnvironmentBaker::CloudLayer cloudLayer{};

		if (cloudsEnabled_ && clouds_.isReady())
		{
			const mv::math::Vec3 wind = clouds_.windOffset(cloudParams_);

			cloudLayer.shape = clouds_.shapeVolume();
			cloudLayer.detail = clouds_.detailVolume();
			cloudLayer.weather = clouds_.weatherMap();

			cloudLayer.windOffset[0] = wind.x;
			cloudLayer.windOffset[1] = wind.y;
			cloudLayer.windOffset[2] = wind.z;
			cloudLayer.coverageScale = 1.0f / (std::max)(cloudParams_.weatherScale, 1.0f);

			cloudLayer.planetRadius = cloudParams_.planetRadius;
			cloudLayer.layerBottom = cloudParams_.layerBottom;
			cloudLayer.layerTop = cloudParams_.layerTop;
			cloudLayer.shapeScale = cloudParams_.shapeScale;

			cloudLayer.detailScale = cloudParams_.detailScale;
			cloudLayer.detailStrength = cloudParams_.detailStrength;
			cloudLayer.densityScale = cloudParams_.densityScale;
			cloudLayer.extinction = cloudParams_.extinction;

			cloudLayer.sunColor[0] = lightIntensity_;
			cloudLayer.sunColor[1] = lightIntensity_;
			cloudLayer.sunColor[2] = lightIntensity_;
			cloudLayer.ambientStrength = cloudParams_.ambientStrength;

			cloudLayer.forwardScattering = cloudParams_.forwardScattering;
			cloudLayer.backwardScattering = cloudParams_.backwardScattering;
			cloudLayer.scatterBlend = cloudParams_.scatterBlend;

			// A third of the view march's steps. Every texel of this is about to be summed
			// into nine coefficients and blurred into a mip chain, neither of which can
			// show the difference.
			cloudLayer.steps = 32;
		}

		environment_.bake(skyParams_, cloudLayer);
		environmentDirty_ = false;

		const float* sh = environment_.shCoefficients();

		char message[512];
		sprintf_s(message,
			"Environment baked on %s in %.1f ms\n"
			"  L00  % .4f % .4f % .4f\n"
			"  L1-1 % .4f % .4f % .4f\n"
			"  L10  % .4f % .4f % .4f\n"
			"  L11  % .4f % .4f % .4f\n",
			environment_.bakedOnGpu() ? "GPU" : "CPU",
			environment_.lastBakeMilliseconds(),
			sh[0], sh[1], sh[2],
			sh[3], sh[4], sh[5],
			sh[6], sh[7], sh[8],
			sh[9], sh[10], sh[11]);

		OutputDebugStringA(message);
	}
	sceneConstants.lightDirection[0] = lightDirection.x;
	sceneConstants.lightDirection[1] = lightDirection.y;
	sceneConstants.lightDirection[2] = lightDirection.z;

	sceneConstants.lightColor[0] = lightIntensity_;
	sceneConstants.lightColor[1] = lightIntensity_;
	sceneConstants.lightColor[2] = lightIntensity_;
	sceneConstants.ambientIntensity = ambientIntensity_;

	// --- shadow cascades ------------------------------------------------------

	{
		mv::shadow::CameraView cameraView{};
		cameraView.position = eye;
		cameraView.forward = forward;
		cameraView.right = mv::math::normalize(mv::math::cross(forward, { 0.0f, 1.0f, 0.0f }));
		cameraView.up = mv::math::cross(cameraView.right, forward);
		cameraView.fovY = 1.0472f;
		cameraView.aspect = (float)width_ / (float)height_;
		cameraView.nearZ = activeNear_;

		shadowMap_.setLambda(shadowLambda_);
		shadowMap_.setDistance(shadowDistance_);
		shadowMap_.setNearDistance(shadowNearDistance_);
		shadowMap_.update(cameraView, lightDirection, scene_->boundsMin, scene_->boundsMax);

		sceneConstants.cameraForward[0] = forward.x;
		sceneConstants.cameraForward[1] = forward.y;
		sceneConstants.cameraForward[2] = forward.z;
		sceneConstants.shadowEnabled = shadowEnabled_ ? 1u : 0u;

		for (mv::types::u32 i = 0; i < mv::shadow::kMaxCascades; i++)
		{
			const mv::shadow::Cascade& cascade = shadowMap_.cascade(i);

			memcpy(sceneConstants.cascadeViewProj[i], cascade.viewProj.m, sizeof(cascade.viewProj.m));
			sceneConstants.cascadeSplits[i] = cascade.splitDepth;
			sceneConstants.cascadeTexelSize[i] = cascade.texelWorldSize;
		}

		sceneConstants.shadowDepthBias = shadowDepthBias_;
		sceneConstants.shadowNormalBias = shadowNormalBias_;
		sceneConstants.shadowPcfRadius = shadowPcfRadius_;
		sceneConstants.shadowCascadeBlend = shadowCascadeBlend_;
	}

	// Uniform across every texture, because that is all setForcedBaseMip does. A real
	// streaming system would vary it per texture and this would have to be a buffer.
	sceneConstants.forcedBaseMip = (float)forcedBaseMip_;
	sceneConstants.vtMaxAnisotropy = vtMaxAnisotropy_;

	// --- cloud shadows --------------------------------------------------------

	// Where the map ended up last time it was baked. It is baked at the top of this frame,
	// before any shading pass runs, so by the time the constants reach a shader these are
	// this frame's values.
	if (cloudsEnabled_ && clouds_.isReady() && cloudParams_.shadowStrength > 0.0f)
	{
		sceneConstants.cloudShadowStrength = cloudParams_.shadowStrength;

		const mv::math::Vec3& shadowOrigin = clouds_.shadowOrigin();
		const mv::math::Vec3& shadowRight = clouds_.shadowRight();
		const mv::math::Vec3& shadowUp = clouds_.shadowUp();

		sceneConstants.cloudShadowOrigin[0] = shadowOrigin.x;
		sceneConstants.cloudShadowOrigin[1] = shadowOrigin.y;
		sceneConstants.cloudShadowOrigin[2] = shadowOrigin.z;
		sceneConstants.cloudShadowInvExtent = 1.0f / (std::max)(clouds_.shadowExtent(), 1.0f);

		sceneConstants.cloudShadowRight[0] = shadowRight.x;
		sceneConstants.cloudShadowRight[1] = shadowRight.y;
		sceneConstants.cloudShadowRight[2] = shadowRight.z;

		sceneConstants.cloudShadowUp[0] = shadowUp.x;
		sceneConstants.cloudShadowUp[1] = shadowUp.y;
		sceneConstants.cloudShadowUp[2] = shadowUp.z;
	}
	else
	{
		sceneConstants.cloudShadowStrength = 0.0f;
	}

	// --- image based lighting -------------------------------------------------

	sceneConstants.iblIntensity = iblIntensity_;
	sceneConstants.viewportSize[0] = (float)width_;
	sceneConstants.viewportSize[1] = (float)height_;

	{
		const float* sh = environment_.shCoefficients();
		for (mv::types::u32 i = 0; i < mv::env::kShCoefficientCount; i++)
		{
			sceneConstants.shCoefficients[i][0] = sh[i * 3 + 0];
			sceneConstants.shCoefficients[i][1] = sh[i * 3 + 1];
			sceneConstants.shCoefficients[i][2] = sh[i * 3 + 2];
			sceneConstants.shCoefficients[i][3] = 0.0f;
		}
	}

	// --- temporal reprojection ------------------------------------------------

	{
		memcpy(sceneConstants.prevViewProj, prevViewProj_.m, sizeof(sceneConstants.prevViewProj));

		// The jitter in UV, so the geometry passes can subtract it back out when they write
		// velocity. NDC to UV halves it and flips Y.
		sceneConstants.jitterUv[0] = jitter.x * 0.5f;
		sceneConstants.jitterUv[1] = jitter.y * -0.5f;
	}

	rhi_->writeBuffer(sceneBuffers_[frameIndex], &sceneConstants, sizeof(sceneConstants), 0);

	mv::rg::RenderGraph rg(rhi_);

	mv::rg::RGTextureHandle backbuffer = rg.importTexture("Backbuffer", context.backbuffer, mv::rg::ERGTextureUsage::Undefined);

	// The geometry passes render here rather than to the backbuffer, so the chain has
	// linear radiance to work with instead of a display image.
	mv::rg::RGTextureHandle sceneColor =
		rg.importTexture("Scene Color", postStack_.sceneColor(), mv::rg::ERGTextureUsage::ShaderRead);

	// Imported rather than a graph transient. A transient is handed back at its last use
	// and re-acquired next frame, so its handle alternates between the frames in flight;
	// the cloud pass names the depth buffer in a descriptor, and a descriptor that has to
	// be rewritten every frame is one that frames still reading it are racing against.
	//
	// Shader-read is where a sampled depth texture rests: it is the state the RHI creates
	// one in, and the state the geometry pass transitions away from and back to.
	mv::rg::RGTextureHandle sceneDepth =
		rg.importTexture("Scene Depth", sceneDepthTexture_, mv::rg::ERGTextureUsage::ShaderRead);

	mv::rg::RGTextureHandle sceneVelocity =
		rg.importTexture("Scene Velocity", velocityTexture_, mv::rg::ERGTextureUsage::ShaderRead);

	// The tail both render paths share: the post-process chain turns the scene target into
	// the backbuffer, and the UI is drawn on top of the finished image so that no effect
	// tone maps or anti-aliases it.
	auto addPostAndUiPasses = [&]()
	{

		// Grass first of the late passes. It is geometry -- it writes colour, velocity and
		// depth like the resolve did -- so it has to be in the depth buffer before the
		// clouds march it, the water tests it and the fog hazes it, all of which then
		// treat it as scenery without ever hearing about it.
		if (terrainScene_ && grassParams_.enabled && grass_.isReady())
		{
			rg.addPass(
				{
					.name = "Grass",
					.setup = [&](mv::rg::Builder& builder)
					{
						builder.accessTexture(sceneColor, mv::rg::ERGTextureUsage::ColorAttachment);
						builder.accessTexture(sceneVelocity, mv::rg::ERGTextureUsage::ColorAttachment);
						builder.accessTexture(sceneDepth, mv::rg::ERGTextureUsage::DepthAttachment);
					},
					.execute = [&](mv::rg::Context& context)
					{
						mv::rhi::IRHI* rhi = context.rhi();
						const mv::rhi::CommandBufferHandle cmd = context.cmd();

						// The cull is a dispatch, so it runs before the render pass the
						// draw sits inside -- the same shape as the cloud march.
						grass_.recordCull(
							cmd,
							grassParams_,
							eye,
							terrainDesc_.rockHeight,
							terrainDesc_.rockSlope,
							waterParams_.level,
							grassPlanes);

						mv::rhi::RenderPassColorTarget color{};
						color.texture = context.getTexture(sceneColor);
						color.clear = false;

						mv::rhi::RenderPassColorTarget velocity{};
						velocity.texture = context.getTexture(sceneVelocity);
						velocity.clear = false;

						mv::rhi::RenderPassDesc passDesc{};
						passDesc.colorTargets.push_back(color);
						passDesc.colorTargets.push_back(velocity);
						passDesc.depthTarget.texture = context.getTexture(sceneDepth);
						passDesc.depthTarget.clear = false;

						rhi->beginRenderPass(cmd, passDesc);

						rhi->setViewport(cmd, 0.0f, 0.0f, (float)width_, (float)height_);
						rhi->setScissor(cmd, 0, 0, width_, height_);

						grass_.recordDraw(
							cmd,
							grassParams_,
							sceneBindGroups_[frameIndex],
							materialSystem_.bindlessBindGroup());

						rhi->endRenderPass(cmd);
					}
				}
			);
		}

		// Props: the world's entities, drawn as forward geometry right after the grass.
		// The pass exists whenever anything asks to be drawn -- the entities decide, not
		// the scene.
		{
			bool anyProp = false;

			world_.forEach([&](mv::game::EntityHandle, mv::game::Entity& entity)
			{
				anyProp |= entity.primitive != 0xFFFFFFFF;
			});

			const bool anySculpt = sculptPlaced_ && sculptGpu_.isReady();

			if ((anyProp || anySculpt) && propRenderer_.isReady())
			{
				rg.addPass(
					{
						.name = "Props",
						.setup = [&](mv::rg::Builder& builder)
						{
							builder.accessTexture(sceneColor, mv::rg::ERGTextureUsage::ColorAttachment);
							builder.accessTexture(sceneVelocity, mv::rg::ERGTextureUsage::ColorAttachment);
							builder.accessTexture(sceneDepth, mv::rg::ERGTextureUsage::DepthAttachment);
						},
						.execute = [&](mv::rg::Context& context)
						{
							mv::rhi::IRHI* rhi = context.rhi();
							const mv::rhi::CommandBufferHandle cmd = context.cmd();

							// The marches are dispatches, so they run before the render
							// pass their draws sit inside -- the grass cull's shape.
							// Animating re-marches every chunk each frame; still, only
							// the chunks with edits pending.
							if (sculptAnimate_ && sculptGpu_.isReady() && sculptPlaced_)
							{
								sculptGpu_.recordAnimate(
									cmd, sculptTime_,
									sculptWaveAmplitude_, sculptWaveLength_, sculptWaveSpeed_);
							}
							else if (sculptGpu_.anyPending())
							{
								sculptGpu_.recordRemesh(cmd);
							}

							mv::rhi::RenderPassColorTarget color{};
							color.texture = context.getTexture(sceneColor);
							color.clear = false;

							mv::rhi::RenderPassColorTarget velocity{};
							velocity.texture = context.getTexture(sceneVelocity);
							velocity.clear = false;

							mv::rhi::RenderPassDesc passDesc{};
							passDesc.colorTargets.push_back(color);
							passDesc.colorTargets.push_back(velocity);
							passDesc.depthTarget.texture = context.getTexture(sceneDepth);
							passDesc.depthTarget.clear = false;

							rhi->beginRenderPass(cmd, passDesc);

							rhi->setViewport(cmd, 0.0f, 0.0f, (float)width_, (float)height_);
							rhi->setScissor(cmd, 0, 0, width_, height_);

							world_.forEach([&](mv::game::EntityHandle, mv::game::Entity& entity)
							{
								if (entity.primitive >= (mv::types::u32)std::size(propAssets_))
									return;

								const PropAsset& asset = propAssets_[entity.primitive];

								if (asset.model.indexCount == 0)
									return;

								// Physics-driven entities render with the body's own
								// matrix -- full tumble, not just position -- and the
								// rest with their transform.
								mv::math::Mat4 matrix;

								if (entity.physicsBody == mv::game::kInvalidBody ||
									!physics_.bodyMatrix(entity.physicsBody, matrix))
								{
									matrix = entity.transform.matrix();
								}

								// The physics shape is centred on the body's origin; the
								// mesh is wherever the file put it. Fold the bounds centre
								// out first so both agree on the middle.
								mv::math::Mat4 offset = mv::math::Mat4::identity();
								offset.m[12] = -asset.center.x;
								offset.m[13] = -asset.center.y;
								offset.m[14] = -asset.center.z;

								propRenderer_.record(
									cmd,
									asset.model,
									offset * matrix,
									sceneBindGroups_[frameIndex],
									materialSystem_.bindlessBindGroup());
							});

							// The carved ground: one indirect draw per meshed chunk,
							// vertices straight from the buffers the marches wrote.
							if (anySculpt)
							{
								sculptGpu_.recordDraw(
									cmd,
									sceneBindGroups_[frameIndex],
									materialSystem_.bindlessBindGroup());
							}

							rhi->endRenderPass(cmd);
						}
					}
				);
			}
		}

		// The fox: same targets as the props, one palette upload and one skinned
		// draw. The palette was rebuilt by the animator this frame on the CPU.
		if (foxLoaded_ && skinnedPropRenderer_.isReady())
		{
			skinnedPropRenderer_.setPalette(
				frameIndex,
				foxAnimator_.palette().data(),
				(mv::types::u32)foxAnimator_.palette().size());

			rg.addPass(
				{
					.name = "Skinned props",
					.setup = [&](mv::rg::Builder& builder)
					{
						builder.accessTexture(sceneColor, mv::rg::ERGTextureUsage::ColorAttachment);
						builder.accessTexture(sceneVelocity, mv::rg::ERGTextureUsage::ColorAttachment);
						builder.accessTexture(sceneDepth, mv::rg::ERGTextureUsage::DepthAttachment);
					},
					.execute = [&](mv::rg::Context& context)
					{
						mv::rhi::IRHI* rhi = context.rhi();
						const mv::rhi::CommandBufferHandle cmd = context.cmd();

						mv::rhi::RenderPassColorTarget color{};
						color.texture = context.getTexture(sceneColor);
						color.clear = false;

						mv::rhi::RenderPassColorTarget velocity{};
						velocity.texture = context.getTexture(sceneVelocity);
						velocity.clear = false;

						mv::rhi::RenderPassDesc passDesc{};
						passDesc.colorTargets.push_back(color);
						passDesc.colorTargets.push_back(velocity);
						passDesc.depthTarget.texture = context.getTexture(sceneDepth);
						passDesc.depthTarget.clear = false;

						rhi->beginRenderPass(cmd, passDesc);

						rhi->setViewport(cmd, 0.0f, 0.0f, (float)width_, (float)height_);
						rhi->setScissor(cmd, 0, 0, width_, height_);

						mv::game::Transform transform;
						transform.position = foxPosition_;
						transform.rotation.y = foxYaw_;
						transform.scale = { foxScale_, foxScale_, foxScale_ };

						skinnedPropRenderer_.record(
							cmd,
							foxModel_,
							transform.matrix(),
							frameIndex,
							sceneBindGroups_[frameIndex],
							materialSystem_.bindlessBindGroup());

						rhi->endRenderPass(cmd);
					}
				}
			);
		}

		// The physics wireframes, on request: collected from Bullet on the CPU right
		// here at graph-build time, drawn depth-tested over the geometry. What the
		// solver collides, not what the meshes show -- which is the entire point.
		if (physicsDebugDraw_ && physicsDebugRenderer_.isReady() && physics_.isReady())
		{
			physicsDebugLines_.clear();
			physics_.collectDebugLines(
				physicsDebugLines_,
				mv::debugdraw::DebugLineRenderer::kMaxVertices / 2);

			physicsDebugVertices_.clear();
			physicsDebugVertices_.reserve(physicsDebugLines_.size() * 2);

			for (const mv::game::DebugLine& line : physicsDebugLines_)
			{
				physicsDebugVertices_.push_back({
					{ line.from.x, line.from.y, line.from.z },
					{ line.color.x, line.color.y, line.color.z } });
				physicsDebugVertices_.push_back({
					{ line.to.x, line.to.y, line.to.z },
					{ line.color.x, line.color.y, line.color.z } });
			}

			physicsDebugRenderer_.upload(
				frameIndex,
				physicsDebugVertices_.data(),
				(mv::types::u32)physicsDebugVertices_.size());

			if (!physicsDebugVertices_.empty())
			{
				rg.addPass(
					{
						.name = "Physics debug",
						.setup = [&](mv::rg::Builder& builder)
						{
							builder.accessTexture(sceneColor, mv::rg::ERGTextureUsage::ColorAttachment);
							builder.accessTexture(sceneDepth, mv::rg::ERGTextureUsage::DepthAttachment);
						},
						.execute = [&](mv::rg::Context& context)
						{
							mv::rhi::IRHI* rhi = context.rhi();
							const mv::rhi::CommandBufferHandle cmd = context.cmd();

							mv::rhi::RenderPassColorTarget color{};
							color.texture = context.getTexture(sceneColor);
							color.clear = false;

							mv::rhi::RenderPassDesc passDesc{};
							passDesc.colorTargets.push_back(color);
							passDesc.depthTarget.texture = context.getTexture(sceneDepth);
							passDesc.depthTarget.clear = false;

							rhi->beginRenderPass(cmd, passDesc);

							rhi->setViewport(cmd, 0.0f, 0.0f, (float)width_, (float)height_);
							rhi->setScissor(cmd, 0, 0, width_, height_);

							physicsDebugRenderer_.record(
								cmd,
								frameIndex,
								sceneBindGroups_[frameIndex],
								materialSystem_.bindlessBindGroup());

							rhi->endRenderPass(cmd);
						}
					}
				);
			}
		}

		// Clouds sit between the geometry and the chain: they need the finished depth to
		// know where the terrain occludes them, and they have to be in the HDR target
		// before tone mapping so the sunlit edges bloom like everything else.
		auto addCloudsPass = [&]()
		{
			if (!cloudsEnabled_ || !clouds_.isReady())
				return;

			rg.addPass(
				{
					.name = "Clouds",
					.setup = [&](mv::rg::Builder& builder)
					{
						builder.accessTexture(sceneDepth, mv::rg::ERGTextureUsage::ShaderRead);
						builder.accessTexture(sceneColor, mv::rg::ERGTextureUsage::ColorAttachment);
					},
					.execute = [&](mv::rg::Context& context)
					{
						mv::rhi::IRHI* rhi = context.rhi();
						const mv::rhi::CommandBufferHandle cmd = context.cmd();

						const mv::rhi::TextureHandle depth = context.getTexture(sceneDepth);

						// The march is a dispatch, so it has to happen before the render
						// pass the composite draws inside.
						mv::clouds::CloudRenderer::View view{};
						view.position = eye;
						view.forward = forward;
						view.fovY = 1.0472f;
						view.nearZ = activeNear_;
						view.farZ = activeFar_;

						clouds_.recordMarch(
							cmd,
							cloudParams_,
							view,
							lightDirection,
							{ lightIntensity_, lightIntensity_, lightIntensity_ },
							depth);

						mv::rhi::RenderPassColorTarget target{};
						target.texture = context.getTexture(sceneColor);
						// Loaded, not cleared: the whole point is to blend into what the
						// geometry left there.
						target.clear = false;

						mv::rhi::RenderPassDesc passDesc{};
						passDesc.colorTargets.push_back(target);

						rhi->beginRenderPass(cmd, passDesc);

						rhi->setViewport(cmd, 0.0f, 0.0f, (float)width_, (float)height_);
						rhi->setScissor(cmd, 0, 0, width_, height_);

						clouds_.recordComposite(cmd, depth);

						rhi->endRenderPass(cmd);
					}
				}
			);
		};

		auto addWaterPass = [&]()
		{
			if (!waterParams_.enabled || !water_.isReady() || !terrainScene_)
				return;

			// The SSR march is its own graph pass rather than a dispatch at the top of the
			// water pass, because the two need the scene colour in different states: the
			// march samples it, the draw renders into it, and a pass boundary is where the
			// graph knows how to turn one into the other. Everything the reflection should
			// contain -- geometry, sky, clouds when they drew first -- is in the target by
			// now, so what the march copies out is what a mirror at the surface would see.
			rg.addPass(
				{
					.name = "Water SSR",
					.setup = [&](mv::rg::Builder& builder)
					{
						builder.accessTexture(sceneDepth, mv::rg::ERGTextureUsage::ShaderRead);
						builder.accessTexture(sceneColor, mv::rg::ERGTextureUsage::ShaderRead);
					},
					.execute = [&](mv::rg::Context& context)
					{
						mv::water::WaterSurface::View view{};
						view.position = eye;
						view.forward = forward;
						view.fovY = 1.0472f;
						view.nearZ = activeNear_;
						view.farZ = activeFar_;
						view.width = width_;
						view.height = height_;

						water_.recordSSR(
							context.cmd(),
							waterParams_,
							view,
							context.getTexture(sceneDepth),
							context.getTexture(sceneColor));
					}
				}
			);

			rg.addPass(
				{
					.name = "Water",
					.setup = [&](mv::rg::Builder& builder)
					{
						builder.accessTexture(sceneDepth, mv::rg::ERGTextureUsage::ShaderRead);
						builder.accessTexture(sceneColor, mv::rg::ERGTextureUsage::ColorAttachment);
					},
					.execute = [&](mv::rg::Context& context)
					{
						mv::rhi::IRHI* rhi = context.rhi();
						const mv::rhi::CommandBufferHandle cmd = context.cmd();

						mv::rhi::RenderPassColorTarget target{};
						target.texture = context.getTexture(sceneColor);
						target.clear = false;

						mv::rhi::RenderPassDesc passDesc{};
						passDesc.colorTargets.push_back(target);

						rhi->beginRenderPass(cmd, passDesc);

						rhi->setViewport(cmd, 0.0f, 0.0f, (float)width_, (float)height_);
						rhi->setScissor(cmd, 0, 0, width_, height_);

						mv::water::WaterSurface::View view{};
						view.position = eye;
						view.forward = forward;
						view.fovY = 1.0472f;
						view.nearZ = activeNear_;
						view.farZ = activeFar_;
						view.width = width_;
						view.height = height_;

						water_.record(
							cmd,
							waterParams_,
							view,
							lightDirection,
							lightIntensity_,
							iblIntensity_,
							context.getTexture(sceneDepth),
							environment_.cubemap());

						rhi->endRenderPass(cmd);
					}
				}
			);
		};

		// Which of the two draws first is decided by where the camera is, because neither
		// pass can see the other: the water is an analytic plane that writes no depth, and
		// the clouds only test the geometry's depth buffer.
		//
		// Below the cloud base the order is clouds first. Above the water the two never
		// overlap on screen -- a ray that reaches the water is heading down, and from under
		// the layer a downward ray leaves the shell without crossing it -- and under the
		// water everything is seen through it, clouds included, so the clouds have to be in
		// the target before the water's absorption is laid over them.
		//
		// Above the cloud base that order inverts itself: the layer can now sit between the
		// camera and the lake, and a lake drawn after the composite would punch its
		// rectangle straight through the clouds. Water first, clouds blended over it.
		if (eye.y >= cloudParams_.layerBottom)
		{
			addWaterPass();
			addCloudsPass();
		}
		else
		{
			addCloudsPass();
			addWaterPass();
		}

		// Fog is the last of the scene passes, which is what decides who fogs whom: the far
		// water hazes out with the far terrain -- its depth is the lake bed, the same
		// distance at fog scales -- and a cloud composited over a distant ridge picks up
		// the ridge's haze. Skipped underwater, where the water's own absorption is the
		// fog and a sky-coloured haze inside a lake would be neither.
		{
			const bool eyeUnderwater =
				terrainScene_ && waterParams_.enabled && eye.y < waterParams_.level;

			if (fogParams_.enabled && fog_.isReady() && !eyeUnderwater)
			{
				rg.addPass(
					{
						.name = "Height Fog",
						.setup = [&](mv::rg::Builder& builder)
						{
							builder.accessTexture(sceneDepth, mv::rg::ERGTextureUsage::ShaderRead);
							builder.accessTexture(sceneColor, mv::rg::ERGTextureUsage::ColorAttachment);
						},
						.execute = [&](mv::rg::Context& context)
						{
							mv::rhi::IRHI* rhi = context.rhi();
							const mv::rhi::CommandBufferHandle cmd = context.cmd();

							mv::rhi::RenderPassColorTarget target{};
							target.texture = context.getTexture(sceneColor);
							target.clear = false;

							mv::rhi::RenderPassDesc passDesc{};
							passDesc.colorTargets.push_back(target);

							rhi->beginRenderPass(cmd, passDesc);

							rhi->setViewport(cmd, 0.0f, 0.0f, (float)width_, (float)height_);
							rhi->setScissor(cmd, 0, 0, width_, height_);

							mv::fog::HeightFog::View view{};
							view.position = eye;
							view.forward = forward;
							view.fovY = 1.0472f;
							view.nearZ = activeNear_;
							view.farZ = activeFar_;
							view.width = width_;
							view.height = height_;

							// The scene set rides along for the cascade atlas and the cloud
							// shadow map the shaft march samples. The atlas is not declared
							// in this pass's setup: the resolve already left it in shader
							// read, which is the same standing assumption the post chain
							// makes when it binds this set.
							fog_.record(
								cmd,
								fogParams_,
								view,
								lightDirection,
								lightIntensity_,
								sceneBindGroups_[frameIndex],
								materialSystem_.bindlessBindGroup(),
								context.getTexture(sceneDepth));

							rhi->endRenderPass(cmd);
						}
					}
				);
			}
		}

		rg.addPass(
			{
				.name = "Post Process",
				.setup = [&](mv::rg::Builder& builder)
				{
					builder.accessTexture(sceneColor, mv::rg::ERGTextureUsage::ShaderRead);
					builder.accessTexture(sceneVelocity, mv::rg::ERGTextureUsage::ShaderRead);
					builder.accessTexture(backbuffer, mv::rg::ERGTextureUsage::ColorAttachment);
				},
				.execute = [&](mv::rg::Context& context)
				{
					mv::post::EffectContext effectContext{};
					effectContext.rhi = context.rhi();
					effectContext.cmd = context.cmd();
					effectContext.output = context.getTexture(backbuffer);
					effectContext.sceneBindGroup = sceneBindGroups_[frameIndex];
					effectContext.bindlessBindGroup = materialSystem_.bindlessBindGroup();
					effectContext.width = width_;
					effectContext.height = height_;
					effectContext.frameIndex = frameIndex;

					postStack_.execute(effectContext);
				}
			}
		);

		// The frame's HUD content, queued now and drawn in the UI pass below: the
		// player's layer under the developer's.
		buildHud();
		hudRenderer_.end(frameIndex);

		rg.addPass(
			{
				.name = "UI",
				.setup = [&](mv::rg::Builder& builder)
				{
					builder.accessTexture(backbuffer, mv::rg::ERGTextureUsage::ColorAttachment);
				},
				.execute = [&](mv::rg::Context& context)
				{
					mv::rhi::IRHI* rhi = context.rhi();
					const mv::rhi::CommandBufferHandle cmd = context.cmd();

					mv::rhi::RenderPassColorTarget target{};
					target.texture = context.getTexture(backbuffer);
					// The chain already wrote every pixel.
					target.clear = false;

					mv::rhi::RenderPassDesc passDesc{};
					passDesc.colorTargets.push_back(target);

					rhi->beginRenderPass(cmd, passDesc);

					rhi->setViewport(cmd, 0.0f, 0.0f, (float)width_, (float)height_);
					rhi->setScissor(cmd, 0, 0, width_, height_);

					hudRenderer_.record(cmd, frameIndex);
					imguiRenderer_.render(cmd, frameIndex);

					rhi->endRenderPass(cmd);
				}
			}
		);
	};

	// Imported in its resting state, which for a sampled depth target is shader-read: the
	// pass below writes it and everything after reads it, so it returns there every frame.
	mv::rg::RGTextureHandle shadowAtlas =
		rg.importTexture("Shadow Atlas", shadowMap_.texture(), mv::rg::ERGTextureUsage::ShaderRead);

	if (skyboxEnabled_)
	{
		rg.addPass(
			{
				.name = "Skybox",
				.setup = [&](mv::rg::Builder& builder)
				{
					builder.accessTexture(sceneColor, mv::rg::ERGTextureUsage::ColorAttachment);
					builder.accessTexture(sceneVelocity, mv::rg::ERGTextureUsage::ColorAttachment);
				},
				.execute = [&](mv::rg::Context& context)
				{
					mv::rhi::IRHI* rhi = context.rhi();
					const mv::rhi::CommandBufferHandle cmd = context.cmd();

					mv::rhi::RenderPassColorTarget target{};
					target.texture = context.getTexture(sceneColor);
					// Cleared here rather than loading undefined contents, even though every pixel
					// is about to be written.
					target.clear = true;

					mv::rhi::RenderPassDesc passDesc{};
					passDesc.colorTargets.push_back(target);

					// The sky moves too, by rotation alone, so it writes velocity as well.
					mv::rhi::RenderPassColorTarget velocityTarget{};
					velocityTarget.texture = context.getTexture(sceneVelocity);
					velocityTarget.clear = true;
					passDesc.colorTargets.push_back(velocityTarget);

					rhi->beginRenderPass(cmd, passDesc);

					rhi->setViewport(cmd, 0.0f, 0.0f, (float)width_, (float)height_);
					rhi->setScissor(cmd, 0, 0, width_, height_);

					const mv::rhi::PipelineLayoutHandle pipelineLayout = materialSystem_.pipelineLayout();

					rhi->bindGraphicsPipeline(cmd, skyboxPipeline_);
					rhi->bindBindGroup(cmd, pipelineLayout, 0, sceneBindGroups_[frameIndex]);
					rhi->bindBindGroup(cmd, pipelineLayout, 1, materialSystem_.bindlessBindGroup());

					rhi->draw(cmd, 3, 1, 0, 0);

					rhi->endRenderPass(cmd);
				}
			}
		);
	}

	if (cloudsEnabled_ && clouds_.isReady() && cloudParams_.shadowStrength > 0.0f)
	{
		rg.addPass(
			{
				.name = "Cloud Shadows",
				// Declares nothing: the map is owned by the cloud renderer and never enters
				// the graph, exactly as the march target does not. It reads the baked volumes
				// and writes one texture nothing else in the frame touches, so the only
				// ordering that matters is that it lands before the shading passes -- which
				// is what putting it here achieves.
				.setup = [&](mv::rg::Builder&) {},
				.execute = [&](mv::rg::Context& context)
				{
					// Centred on the camera. The map is kilometres wide and the scene is a
					// few hundred metres of it, so following the camera costs nothing and
					// means the extent never has to bound the world.
					clouds_.recordShadow(context.cmd(), cloudParams_, lightDirection, eye);
				}
			}
		);
	}

	rg.addPass(
		{
			.name = "Shadow Cascades",
			.setup = [&](mv::rg::Builder& builder)
			{
				builder.accessTexture(shadowAtlas, mv::rg::ERGTextureUsage::DepthAttachment);
			},
			.execute = [&](mv::rg::Context& context)
			{
				mv::rhi::IRHI* rhi = context.rhi();
				const mv::rhi::CommandBufferHandle cmd = context.cmd();

				// One pass over the whole atlas, cleared once. Each cascade is then a
				// viewport within it, which avoids four render passes and four barriers.
				mv::rhi::RenderPassDesc passDesc{};
				passDesc.depthTarget.texture = context.getTexture(shadowAtlas);
				passDesc.depthTarget.clear = true;
				passDesc.depthTarget.clearDepth = 1.0f;

				rhi->beginRenderPass(cmd, passDesc);

				const mv::rhi::PipelineLayoutHandle pipelineLayout = materialSystem_.pipelineLayout();

				rhi->bindGraphicsPipeline(cmd, shadowPipeline_);
				rhi->bindBindGroup(cmd, pipelineLayout, 0, sceneBindGroups_[frameIndex]);
				rhi->bindBindGroup(cmd, pipelineLayout, 1, materialSystem_.bindlessBindGroup());

				rhi->bindVertexBuffer(cmd, 0, scene_->vertexBuffer, sizeof(mv::asset::ModelVertex), 0);
				rhi->bindIndexBuffer(cmd, scene_->indexBuffer, mv::rhi::EIndexFormat::eUint32, 0);

				for (mv::types::u32 cascade = 0; cascade < mv::shadow::kMaxCascades; cascade++)
				{
					mv::types::u32 tileX = 0;
					mv::types::u32 tileY = 0;
					shadowMap_.tileOrigin(cascade, tileX, tileY);

					const float resolution = (float)mv::shadow::kCascadeResolution;
					rhi->setViewport(cmd, (float)tileX, (float)tileY, resolution, resolution);
					rhi->setScissor(cmd, (mv::types::s32)tileX, (mv::types::s32)tileY,
						mv::shadow::kCascadeResolution, mv::shadow::kCascadeResolution);

					for (mv::types::u32 i = 0; i < (mv::types::u32)scene_->primitives.size(); i++)
					{
						const auto& primitive = scene_->primitives[i];
						const mv::material::Material& material = materialSystem_.material(primitive.material);

						// Blended geometry has no single depth to write, so it casts nothing.
						if (material.renderState.alphaMode == mv::material::EAlphaMode::eBlend)
							continue;

						const mv::material::MaterialSystem::DrawConstants drawConstants{ i, primitive.material, cascade };
						rhi->pushConstants(cmd, pipelineLayout, &drawConstants, sizeof(drawConstants), 0);

						rhi->drawIndexed(cmd, primitive.indexCount, 1, primitive.firstIndex, 0, 0);
					}
				}

				rhi->endRenderPass(cmd);
			}
		}
	);

	if (useVisibilityBuffer_)
	{
		// Imported in its resting state, which is what the texture was created in, so the
		// barriers the graph emits line up on the first frame as well as later ones.
		mv::rg::RGTextureHandle visibility =
			rg.importTexture("Visibility", visibilityTexture_, mv::rg::ERGTextureUsage::ShaderRead);

		rg.addPass(
			{
				.name = "Visibility Pass",
				.setup = [&](mv::rg::Builder& builder)
				{
					builder.accessTexture(visibility, mv::rg::ERGTextureUsage::ColorAttachment);
					builder.accessTexture(sceneDepth, mv::rg::ERGTextureUsage::DepthAttachment);
				},
				.execute = [&](mv::rg::Context& context)
				{
					mv::rhi::IRHI* rhi = context.rhi();
					const mv::rhi::CommandBufferHandle cmd = context.cmd();

					mv::rhi::RenderPassColorTarget target{};
					target.texture = context.getTexture(visibility);
					target.clear = true;
					// Zero reads back as "no geometry"; the draw index is stored biased by
					// one so a float clear of zero is all that is needed for a UINT target.
					target.clearColor[0] = 0.0f;
					target.clearColor[1] = 0.0f;
					target.clearColor[2] = 0.0f;
					target.clearColor[3] = 0.0f;

					mv::rhi::RenderPassDesc passDesc{};
					passDesc.colorTargets.push_back(target);
					passDesc.depthTarget.texture = context.getTexture(sceneDepth);
					passDesc.depthTarget.clear = true;
					passDesc.depthTarget.clearDepth = 1.0f;

					rhi->beginRenderPass(cmd, passDesc);

					rhi->setViewport(cmd, 0.0f, 0.0f, (float)width_, (float)height_);
					rhi->setScissor(cmd, 0, 0, width_, height_);

					const mv::rhi::PipelineLayoutHandle pipelineLayout = materialSystem_.pipelineLayout();

					rhi->bindBindGroup(cmd, pipelineLayout, 0, sceneBindGroups_[frameIndex]);
					rhi->bindBindGroup(cmd, pipelineLayout, 1, materialSystem_.bindlessBindGroup());

					rhi->bindVertexBuffer(cmd, 0, scene_->vertexBuffer, sizeof(mv::asset::ModelVertex), 0);
					rhi->bindIndexBuffer(cmd, scene_->indexBuffer, mv::rhi::EIndexFormat::eUint32, 0);

					mv::rhi::PipelineHandle boundPipeline = mv::INVALID_HANDLE;

					for (mv::types::u32 i = 0; i < (mv::types::u32)scene_->primitives.size(); i++)
					{
						const auto& primitive = scene_->primitives[i];
						const mv::material::Material& material = materialSystem_.material(primitive.material);

						// A visibility buffer stores one surface per pixel, so blended
						// geometry has no place in it and would need a forward pass after.
						if (material.renderState.alphaMode == mv::material::EAlphaMode::eBlend)
							continue;

						const mv::rhi::PipelineHandle pipeline = vbPipelineCache_.get(
							{ mv::material::EAlphaMode::eOpaque, material.renderState.doubleSided });

						if (pipeline != boundPipeline)
						{
							rhi->bindGraphicsPipeline(cmd, pipeline);
							boundPipeline = pipeline;
						}

						const mv::material::MaterialSystem::DrawConstants drawConstants{ i, primitive.material };
						rhi->pushConstants(cmd, pipelineLayout, &drawConstants, sizeof(drawConstants), 0);

						rhi->drawIndexed(cmd, primitive.indexCount, 1, primitive.firstIndex, 0, 0);
					}

					rhi->endRenderPass(cmd);
				}
			}
		);

		rg.addPass(
			{
				.name = "Visibility Resolve",
				.setup = [&](mv::rg::Builder& builder)
				{
					// Declaring both usages is what makes the graph emit the render target
					// to shader read transition between the two passes.
					builder.accessTexture(visibility, mv::rg::ERGTextureUsage::ShaderRead);
					builder.accessTexture(sceneColor, mv::rg::ERGTextureUsage::ColorAttachment);
					builder.accessTexture(sceneVelocity, mv::rg::ERGTextureUsage::ColorAttachment);
					// Read by the resolve, which is what makes the graph put the atlas back
					// into shader-read after the cascade pass wrote it.
					builder.accessTexture(shadowAtlas, mv::rg::ERGTextureUsage::ShaderRead);
				},
				.execute = [&](mv::rg::Context& context)
				{
					mv::rhi::IRHI* rhi = context.rhi();
					const mv::rhi::CommandBufferHandle cmd = context.cmd();

					// Before the render pass, because a dispatch cannot be recorded inside
					// one. Resets every slot to the maximum so the InterlockedMin below
					// reports what this frame asked for rather than the running minimum
					// over the whole session.
					feedbackClear_.record(cmd, 0xFFFFFFFFu);

					mv::rhi::RenderPassColorTarget target{};
					target.texture = context.getTexture(sceneColor);
					// The skybox already covered every pixel when it ran.
					target.clear = !skyboxEnabled_;
					// Black, matching the value the texture was created with: a mismatched
					// clear gives up the fast path.
					target.clearColor[0] = 0.0f;
					target.clearColor[1] = 0.0f;
					target.clearColor[2] = 0.0f;
					target.clearColor[3] = 0.0f;

					mv::rhi::RenderPassDesc passDesc{};
					passDesc.colorTargets.push_back(target);

					mv::rhi::RenderPassColorTarget velocityTarget{};
					velocityTarget.texture = context.getTexture(sceneVelocity);
					velocityTarget.clear = !skyboxEnabled_;
					passDesc.colorTargets.push_back(velocityTarget);

					rhi->beginRenderPass(cmd, passDesc);

					rhi->setViewport(cmd, 0.0f, 0.0f, (float)width_, (float)height_);
					rhi->setScissor(cmd, 0, 0, width_, height_);

					rhi->bindGraphicsPipeline(cmd, vbShadePipeline_);
					rhi->bindBindGroup(cmd, vbShadePipelineLayout_, 0, sceneBindGroups_[frameIndex]);
					rhi->bindBindGroup(cmd, vbShadePipelineLayout_, 1, materialSystem_.bindlessBindGroup());
					rhi->bindBindGroup(cmd, vbShadePipelineLayout_, 2, vbResourceGroup_);

					// One oversized triangle covering the screen; no vertex buffer.
					rhi->draw(cmd, 3, 1, 0, 0);


					rhi->endRenderPass(cmd);

					// Pull the mip requests the pass just wrote into readback memory. What
					// the CPU maps is therefore always a couple of frames behind, which is
					// exactly what a streaming system wants: never stall on the GPU.
					rhi->copyBuffer(cmd, feedbackReadback_, feedbackBuffer_, 4096 * sizeof(mv::types::u32));
				}
			}
		);

		addPostAndUiPasses();

		rg.compile();
		rg.execute();

		prevViewProj_ = viewProjNoJitter;
		frameCounter_++;

		rhi_->endFrame();
		return;
	}

	rg.addPass(
		{
			.name = "Model Pass",
			.setup = [&](mv::rg::Builder& builder)
			{
				builder.accessTexture(sceneColor, mv::rg::ERGTextureUsage::ColorAttachment);
				builder.accessTexture(sceneVelocity, mv::rg::ERGTextureUsage::ColorAttachment);
				builder.accessTexture(sceneDepth, mv::rg::ERGTextureUsage::DepthAttachment);
				builder.accessTexture(shadowAtlas, mv::rg::ERGTextureUsage::ShaderRead);
			},
			.execute = [&](mv::rg::Context& context)
			{
				mv::rhi::IRHI* rhi = context.rhi();
				const mv::rhi::CommandBufferHandle cmd = context.cmd();

				mv::rhi::RenderPassColorTarget target{};
				target.texture = context.getTexture(sceneColor);
				// The skybox already covered every pixel when it ran.
				target.clear = !skyboxEnabled_;
				target.clearColor[0] = 0.0f;
				target.clearColor[1] = 0.0f;
				target.clearColor[2] = 0.0f;
				target.clearColor[3] = 0.0f;

				mv::rhi::RenderPassDesc passDesc{};
				passDesc.colorTargets.push_back(target);

				// The second target: where each of these pixels was on screen last frame.
				mv::rhi::RenderPassColorTarget velocityTarget{};
				velocityTarget.texture = context.getTexture(sceneVelocity);
				velocityTarget.clear = !skyboxEnabled_;
				passDesc.colorTargets.push_back(velocityTarget);
				passDesc.depthTarget.texture = context.getTexture(sceneDepth);
				passDesc.depthTarget.clear = true;
				passDesc.depthTarget.clearDepth = 1.0f;

				rhi->beginRenderPass(cmd, passDesc);

				rhi->setViewport(cmd, 0.0f, 0.0f, (float)width_, (float)height_);
				rhi->setScissor(cmd, 0, 0, width_, height_);

				const mv::rhi::PipelineLayoutHandle pipelineLayout = materialSystem_.pipelineLayout();

				// Neither set changes between draws now: the scene set is per frame and the
				// bindless set holds every texture and every material at once. A draw only
				// contributes its material index.
				rhi->bindBindGroup(cmd, pipelineLayout, 0, sceneBindGroups_[frameIndex]);
				rhi->bindBindGroup(cmd, pipelineLayout, 1, materialSystem_.bindlessBindGroup());

				// All primitives live in one pair of buffers now, so these are bound once
				// and each draw is just a range within them.
				rhi->bindVertexBuffer(cmd, 0, scene_->vertexBuffer, sizeof(mv::asset::ModelVertex), 0);
				rhi->bindIndexBuffer(cmd, scene_->indexBuffer, mv::rhi::EIndexFormat::eUint32, 0);

				// Primitives arrive sorted by material, so tracking the last pipeline is
				// enough to collapse the redundant binds.
				mv::rhi::PipelineHandle boundPipeline = mv::INVALID_HANDLE;

				for (const auto& primitive : scene_->primitives)
				{
					const mv::material::Material& material = materialSystem_.material(primitive.material);

					if (material.pipeline != boundPipeline)
					{
						rhi->bindGraphicsPipeline(cmd, material.pipeline);
						boundPipeline = material.pipeline;
					}

					const mv::material::MaterialSystem::DrawConstants drawConstants{ 0, primitive.material };
					rhi->pushConstants(cmd, pipelineLayout, &drawConstants, sizeof(drawConstants), 0);

					rhi->drawIndexed(cmd, primitive.indexCount, 1, primitive.firstIndex, 0, 0);
				}


				rhi->endRenderPass(cmd);
			}
		}
	);

	addPostAndUiPasses();

	rg.compile();

	rg.execute();

	prevViewProj_ = viewProjNoJitter;
	frameCounter_++;

	rhi_->endFrame();
}


extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

LRESULT CALLBACK WndProc(
    HWND hWnd,
    UINT msg,
    WPARAM wParam,
    LPARAM lParam)
{
    // ImGui gets first refusal on input so it can capture the mouse and keyboard while
    // a window is hovered or a widget is active.
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return 1;

    switch (msg)
    {
    case WM_NCCREATE:
        // The engine pointer was handed to CreateWindowEx; this is the first message that
        // carries it, and stashing it here is what lets later messages reach the instance.
        SetWindowLongPtr(hWnd, GWLP_USERDATA,
            (LONG_PTR)((CREATESTRUCT*)lParam)->lpCreateParams);
        return DefWindowProc(hWnd, msg, wParam, lParam);

    case WM_SIZE:
        if (Engine* engine = (Engine*)GetWindowLongPtr(hWnd, GWLP_USERDATA))
        {
            // Recorded rather than acted on: a drag sends a message per pixel, and a
            // minimise sends a size of zero. The frame loop picks up the last one.
            engine->requestResize(LOWORD(lParam), HIWORD(lParam));
        }
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;

    default:
        return DefWindowProc(hWnd, msg, wParam, lParam);
    }
}


