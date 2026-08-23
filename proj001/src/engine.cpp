
#include "engine.h"

#include "rg/render_graph.h"

#include "imgui.h"
#include "backends/imgui_impl_win32.h"

#include <fstream>
#include <vector>
#include <string>

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
	};

	constexpr int kDebugModeCount = (int)(sizeof(kDebugModeNames) / sizeof(kDebugModeNames[0]));

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
	deinitializeImGui();
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
	return imguiRenderer_.initialize(rhi_, vs, ps, mv::rhi::ETextureFormat::eD32_SFLOAT);
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
		(model_.boundsMin.x + model_.boundsMax.x) * 0.5f,
		(model_.boundsMin.y + model_.boundsMax.y) * 0.5f,
		(model_.boundsMin.z + model_.boundsMax.z) * 0.5f,
	};

	const mv::math::Vec3 extent = model_.boundsMax - model_.boundsMin;
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

void Engine::enterScene()
{
	const mv::math::Vec3 center
	{
		(model_.boundsMin.x + model_.boundsMax.x) * 0.5f,
		(model_.boundsMin.y + model_.boundsMax.y) * 0.5f,
		(model_.boundsMin.z + model_.boundsMax.z) * 0.5f,
	};

	const mv::math::Vec3 extent = model_.boundsMax - model_.boundsMin;
	const float radius = std::sqrt(mv::math::dot(extent, extent)) * 0.5f;

	// Eye height a little above the floor, looking down whichever horizontal axis is
	// longer, which for a hall is along its length.
	cameraPosition_ = { center.x, model_.boundsMin.y + extent.y * 0.2f, center.z };
	cameraPitch_ = 0.0f;
	cameraYaw_ = (extent.x > extent.z) ? 1.5708f : 0.0f;

	cameraSpeed_ = radius * 0.4f;
	cameraNear_ = radius * 0.005f;
	cameraFar_ = radius * 10.0f;
}

void Engine::updateCamera(float deltaTime)
{
	ImGuiIO& io = ImGui::GetIO();

	// Look only while the right button is held, so the cursor stays usable for the UI.
	// WantCaptureMouse keeps a drag that started on a window from moving the camera.
	if (ImGui::IsMouseDown(ImGuiMouseButton_Right) && !io.WantCaptureMouse)
	{
		const float sensitivity = 0.003f;
		cameraYaw_ -= io.MouseDelta.x * sensitivity;
		cameraPitch_ -= io.MouseDelta.y * sensitivity;

		// Stop just short of straight up or down, where the view basis degenerates.
		const float limit = 1.55f;
		cameraPitch_ = (cameraPitch_ > limit) ? limit : cameraPitch_;
		cameraPitch_ = (cameraPitch_ < -limit) ? -limit : cameraPitch_;
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

	const mv::math::Vec3 forward = mv::math::normalize({
		std::cos(cameraPitch_) * std::sin(cameraYaw_),
		std::sin(cameraPitch_),
		-std::cos(cameraPitch_) * std::cos(cameraYaw_) });

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
	// --- depth target ---------------------------------------------------------

	mv::rhi::TextureDesc depthDesc{};
	depthDesc.width = width_;
	depthDesc.height = height_;
	depthDesc.depth = 1;
	depthDesc.usage = mv::rhi::ETextureUsage::eDepthStencilAttachment;
	depthDesc.format = mv::rhi::ETextureFormat::eD32_SFLOAT;
	depthDesc.memoryType = mv::rhi::EMemoryType::eDeviceLocalImage;

	depthTexture_ = rhi_->createTexture(depthDesc);

	// --- material system ------------------------------------------------------

	const std::vector<mv::types::u32> vsCode = readShaderFile(useVulkan_ ? "model.vs.spv" : "model.vs.cso");
	const std::vector<mv::types::u32> psCode = readShaderFile(useVulkan_ ? "model.ps.spv" : "model.ps.cso");

	if (vsCode.empty() || psCode.empty())
		return false;

	const mv::material::MaterialSystem::ShaderCode vs{ vsCode.data(), (mv::types::u32)(vsCode.size() * sizeof(mv::types::u32)) };
	const mv::material::MaterialSystem::ShaderCode ps{ psCode.data(), (mv::types::u32)(psCode.size() * sizeof(mv::types::u32)) };

	if (!materialSystem_.initialize(rhi_, vs, ps, rhi_->backbufferFormat(), mv::rhi::ETextureFormat::eD32_SFLOAT))
		return false;

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
		sceneDesc.size = 256;
		sceneDesc.usage = mv::rhi::EBufferUsage::eUniform;
		sceneDesc.memoryType = mv::rhi::EMemoryType::eHostVisibleBuffer;

		sceneBuffers_[i] = rhi_->createBuffer(sceneDesc);

		mv::rhi::BindGroupDesc groupDesc{};
		groupDesc.layout = materialSystem_.sceneLayout();
		groupDesc.uniformBuffers.push_back({ .binding = 0, .buffer = sceneBuffers_[i], .offset = 0, .range = 256 });
		groupDesc.storageBuffers.push_back({
			.binding = 1, .buffer = virtualTextures_.infoBuffer(), .offset = 0,
			.stride = sizeof(mv::vt::GpuVirtualTextureInfo), .count = mv::vt::kMaxVirtualTextures });
		groupDesc.storageBuffers.push_back({
			.binding = 2, .buffer = virtualTextures_.pageTableBuffer(), .offset = 0,
			.stride = sizeof(mv::types::u32), .count = mv::vt::kMaxPageTableEntries });

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

	{
		const auto& stats = virtualTextures_.stats();

		char message[256];
		sprintf_s(message,
			"Virtual textures: %u textures, %u pages, %u atlases (%llu MB), %u dropped\n",
			stats.virtualTextureCount, stats.pageCount, stats.atlasCount,
			stats.atlasBytes / (1024ull * 1024ull), stats.droppedPages);

		OutputDebugStringA(message);
	}

	// Sponza is the default model and an interior, so start inside it.
	enterScene();

	if (!initializeVisibilityBuffer())
		return false;

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

	// Must match DrawInfo in vb_shade.hlsl.
	struct DrawInfo
	{
		mv::types::u32 firstIndex;
		mv::types::u32 materialIndex;
		mv::types::u32 pad[2];
	};

	std::vector<DrawInfo> draws;
	draws.reserve(model_.primitives.size());
	for (const auto& primitive : model_.primitives)
	{
		draws.push_back({ primitive.firstIndex, primitive.material, { 0, 0 } });
	}

	mv::rhi::BufferDesc drawInfoDesc{};
	drawInfoDesc.size = draws.size() * sizeof(DrawInfo);
	drawInfoDesc.usage = mv::rhi::EBufferUsage::eStorage;
	drawInfoDesc.memoryType = mv::rhi::EMemoryType::eHostVisibleBuffer;

	drawInfoBuffer_ = rhi_->createBuffer(drawInfoDesc);
	rhi_->writeBuffer(drawInfoBuffer_, draws.data(), drawInfoDesc.size, 0);

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
	// Nothing clears it per frame yet: the values are cumulative, which is fine for a
	// static view and is the streaming manager's problem to solve properly.
	{
		const std::vector<mv::types::u32> initial(feedbackCount, 0xFFFFFFFFu);
		rhi_->uploadBuffer(feedbackBuffer_, initial.data(), feedbackSize);
	}

	mv::rhi::BindGroupDesc resourceGroupDesc{};
	resourceGroupDesc.layout = vbResourceLayout_;
	resourceGroupDesc.storageBuffers.push_back({ .binding = 0, .buffer = model_.vertexBuffer, .offset = 0, .stride = sizeof(mv::asset::ModelVertex), .count = model_.vertexCount });
	resourceGroupDesc.storageBuffers.push_back({ .binding = 1, .buffer = model_.indexBuffer, .offset = 0, .stride = sizeof(mv::types::u32), .count = model_.indexCount });
	resourceGroupDesc.storageBuffers.push_back({ .binding = 2, .buffer = drawInfoBuffer_, .offset = 0, .stride = sizeof(DrawInfo), .count = (mv::types::u32)draws.size() });
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
	vbCacheDesc.colorFormat = mv::rhi::ETextureFormat::eR32G32_UINT;
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
	shadeDesc.colorFormats.push_back(rhi_->backbufferFormat());
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
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();

	ImGui::SetNextWindowPos(ImVec2(20.0f, 20.0f), ImGuiCond_FirstUseEver);
	ImGui::Begin("RHI");
	ImGui::Text("Backend: %s", useVulkan_ ? "Vulkan 1.4" : "D3D12");
	ImGui::Text("%.1f FPS (%.3f ms/frame)", ImGui::GetIO().Framerate, 1000.0f / ImGui::GetIO().Framerate);
	ImGui::Separator();
	ImGui::Text("Frames in flight: %u", rhi_->framesInFlight());
	ImGui::Separator();
	ImGui::Text("Primitives: %d", (int)model_.primitives.size());
	ImGui::Text("Materials:  %u", materialSystem_.materialCount());
	ImGui::Text("Bindless textures: %u", materialSystem_.textureCount());
	ImGui::Separator();
	ImGui::Checkbox("Visibility buffer", &useVisibilityBuffer_);
	ImGui::TextDisabled(useVisibilityBuffer_ ? "id pass + fullscreen resolve" : "forward shading");

	ImGui::BeginDisabled(!useVisibilityBuffer_);
	ImGui::Combo("Debug", &debugMode_, kDebugModeNames, kDebugModeCount);
	ImGui::EndDisabled();
	ImGui::Separator();
	ImGui::TextUnformatted("RMB look, WASD move, Q/E down/up, Shift fast, V debug, B virtual textures");
	ImGui::Text("Pos: %.1f %.1f %.1f", cameraPosition_.x, cameraPosition_.y, cameraPosition_.z);
	ImGui::SliderFloat("Speed", &cameraSpeed_, 0.05f, 50.0f, "%.2f", ImGuiSliderFlags_Logarithmic);
	if (ImGui::Button("Frame model")) frameModel();
	ImGui::SameLine();
	if (ImGui::Button("Enter scene")) enterScene();
	ImGui::Separator();
	ImGui::SliderFloat("Light", &lightIntensity_, 0.0f, 10.0f);
	ImGui::SliderFloat("Ambient", &ambientIntensity_, 0.0f, 1.0f);

	ImGui::Separator();
	{
		const auto& vtStats = virtualTextures_.stats();

		// The one control that matters here: with every page resident, turning this off has
		// to leave the image unchanged, because the page table is resolving to exactly the
		// texels the direct path would have sampled.
		ImGui::Checkbox("Virtual textures", &vtEnabled_);
		ImGui::TextDisabled("%u textures, %u pages, %u atlases (%llu MB)",
			vtStats.virtualTextureCount, vtStats.pageCount, vtStats.atlasCount,
			(unsigned long long)(vtStats.atlasBytes / (1024ull * 1024ull)));

		if (vtStats.droppedPages > 0)
		{
			ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1.0f), "%u pages did not fit", vtStats.droppedPages);
		}
	}

	ImGui::Separator();
	if (ImGui::SliderInt("Forced base mip", &forcedBaseMip_, 0, 6))
	{
		// Exercises the mip-range views a streaming system will use to hide levels that
		// have not arrived yet.
		rhi_->waitIdle();
		materialSystem_.setForcedBaseMip((mv::types::u32)forcedBaseMip_);
	}

	if (useVisibilityBuffer_ && ImGui::CollapsingHeader("Streaming feedback", ImGuiTreeNodeFlags_DefaultOpen))
	{
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

	ImGui::End();

	ImGui::Render();

	updateCamera(ImGui::GetIO().DeltaTime);

	mv::rhi::FrameContext context = rhi_->beginFrame();

	// The pass lambda takes its own rg::Context parameter, which shadows this one.
	const mv::types::u32 frameIndex = context.currentFrameIndex;

	const mv::math::Vec3 eye = cameraPosition_;
	const mv::math::Vec3 forward = mv::math::normalize({
		std::cos(cameraPitch_) * std::sin(cameraYaw_),
		std::sin(cameraPitch_),
		-std::cos(cameraPitch_) * std::cos(cameraYaw_) });

	const mv::math::Mat4 view = mv::math::lookAtRH(eye, eye + forward, { 0.0f, 1.0f, 0.0f });
	const mv::math::Mat4 projection = mv::math::perspectiveRH(
		1.0472f, // 60 degrees
		(float)width_ / (float)height_,
		cameraNear_,
		cameraFar_);

	// Row-vector convention: view is applied first, then the projection.
	const mv::math::Mat4 viewProj = view * projection;

	// Mirrors the SceneConstants cbuffer in model.hlsl.
	struct SceneConstants
	{
		float viewProj[16];
		float cameraPosition[3];  mv::types::u32 debugMode;
		float lightDirection[3];  mv::types::u32 vtEnabled;
		float lightColor[3];      float ambientIntensity;
	} sceneConstants{};

	// Only the visibility buffer resolve honours this; the forward path has no ids to show.
	sceneConstants.debugMode = useVisibilityBuffer_ ? (mv::types::u32)debugMode_ : 0;
	sceneConstants.vtEnabled = vtEnabled_ ? 1u : 0u;

	memcpy(sceneConstants.viewProj, viewProj.m, sizeof(sceneConstants.viewProj));

	// The PBR view vector needs the eye position, which the baked-in world space vertices
	// cannot supply on their own.
	sceneConstants.cameraPosition[0] = eye.x;
	sceneConstants.cameraPosition[1] = eye.y;
	sceneConstants.cameraPosition[2] = eye.z;

	const mv::math::Vec3 lightDirection = mv::math::normalize({ -0.4f, -0.8f, -0.45f });
	sceneConstants.lightDirection[0] = lightDirection.x;
	sceneConstants.lightDirection[1] = lightDirection.y;
	sceneConstants.lightDirection[2] = lightDirection.z;

	sceneConstants.lightColor[0] = lightIntensity_;
	sceneConstants.lightColor[1] = lightIntensity_;
	sceneConstants.lightColor[2] = lightIntensity_;
	sceneConstants.ambientIntensity = ambientIntensity_;

	rhi_->writeBuffer(sceneBuffers_[frameIndex], &sceneConstants, sizeof(sceneConstants), 0);

	mv::rg::RenderGraph rg(rhi_);

	mv::rg::RGTextureHandle backbuffer = rg.importTexture("Backbuffer", context.backbuffer, mv::rg::ERGTextureUsage::Undefined);

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
					passDesc.depthTarget.texture = depthTexture_;
					passDesc.depthTarget.clear = true;
					passDesc.depthTarget.clearDepth = 1.0f;

					rhi->beginRenderPass(cmd, passDesc);

					rhi->setViewport(cmd, 0.0f, 0.0f, (float)width_, (float)height_);
					rhi->setScissor(cmd, 0, 0, width_, height_);

					const mv::rhi::PipelineLayoutHandle pipelineLayout = materialSystem_.pipelineLayout();

					rhi->bindBindGroup(cmd, pipelineLayout, 0, sceneBindGroups_[frameIndex]);
					rhi->bindBindGroup(cmd, pipelineLayout, 1, materialSystem_.bindlessBindGroup());

					rhi->bindVertexBuffer(cmd, 0, model_.vertexBuffer, sizeof(mv::asset::ModelVertex), 0);
					rhi->bindIndexBuffer(cmd, model_.indexBuffer, mv::rhi::EIndexFormat::eUint32, 0);

					mv::rhi::PipelineHandle boundPipeline = mv::INVALID_HANDLE;

					for (mv::types::u32 i = 0; i < (mv::types::u32)model_.primitives.size(); i++)
					{
						const auto& primitive = model_.primitives[i];
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
					builder.accessTexture(backbuffer, mv::rg::ERGTextureUsage::ColorAttachment);
				},
				.execute = [&](mv::rg::Context& context)
				{
					mv::rhi::IRHI* rhi = context.rhi();
					const mv::rhi::CommandBufferHandle cmd = context.cmd();

					mv::rhi::RenderPassColorTarget target{};
					target.texture = context.getTexture(backbuffer);
					target.clear = true;
					target.clearColor[0] = 0.2f;
					target.clearColor[1] = 0.3f;
					target.clearColor[2] = 0.3f;
					target.clearColor[3] = 1.0f;

					mv::rhi::RenderPassDesc passDesc{};
					passDesc.colorTargets.push_back(target);

					rhi->beginRenderPass(cmd, passDesc);

					rhi->setViewport(cmd, 0.0f, 0.0f, (float)width_, (float)height_);
					rhi->setScissor(cmd, 0, 0, width_, height_);

					rhi->bindGraphicsPipeline(cmd, vbShadePipeline_);
					rhi->bindBindGroup(cmd, vbShadePipelineLayout_, 0, sceneBindGroups_[frameIndex]);
					rhi->bindBindGroup(cmd, vbShadePipelineLayout_, 1, materialSystem_.bindlessBindGroup());
					rhi->bindBindGroup(cmd, vbShadePipelineLayout_, 2, vbResourceGroup_);

					// One oversized triangle covering the screen; no vertex buffer.
					rhi->draw(cmd, 3, 1, 0, 0);

					imguiRenderer_.render(cmd, frameIndex);

					rhi->endRenderPass(cmd);

					// Pull the mip requests the pass just wrote into readback memory. What
					// the CPU maps is therefore always a couple of frames behind, which is
					// exactly what a streaming system wants: never stall on the GPU.
					rhi->copyBuffer(cmd, feedbackReadback_, feedbackBuffer_, 4096 * sizeof(mv::types::u32));
				}
			}
		);

		rg.compile();
		rg.execute();

		rhi_->endFrame();
		return;
	}

	rg.addPass(
		{
			.name = "Model Pass",
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
				target.clear = true;
				target.clearColor[0] = 0.2f;
				target.clearColor[1] = 0.3f;
				target.clearColor[2] = 0.3f;
				target.clearColor[3] = 1.0f;

				mv::rhi::RenderPassDesc passDesc{};
				passDesc.colorTargets.push_back(target);
				passDesc.depthTarget.texture = depthTexture_;
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
				rhi->bindVertexBuffer(cmd, 0, model_.vertexBuffer, sizeof(mv::asset::ModelVertex), 0);
				rhi->bindIndexBuffer(cmd, model_.indexBuffer, mv::rhi::EIndexFormat::eUint32, 0);

				// Primitives arrive sorted by material, so tracking the last pipeline is
				// enough to collapse the redundant binds.
				mv::rhi::PipelineHandle boundPipeline = mv::INVALID_HANDLE;

				for (const auto& primitive : model_.primitives)
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

				imguiRenderer_.render(cmd, frameIndex);

				rhi->endRenderPass(cmd);
			}
		}
	);

	rg.compile();

	rg.execute();

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
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;

    default:
        return DefWindowProc(hWnd, msg, wParam, lParam);
    }
}


