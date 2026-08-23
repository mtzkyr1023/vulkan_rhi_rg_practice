
#ifndef _ENGINE_H_
#define _ENGINE_H_

#include "rhi/rhi.h"
#include "rg/render_graph.h"
#include "ui/imgui_renderer.h"
#include "asset/gltf_loader.h"
#include "virtual_texture/virtual_texture.h"

#include <Windows.h>

class Engine
{
public:

	bool initialize(HINSTANCE hInstance, int nCmdShow);
	void deinitialize();

	void run();


private:
	bool initializeWindow(HINSTANCE hInstance, int nCmdShow);
	bool initializeRHI(void* hwnd);
	bool initializeSceneResources();
	bool initializeVisibilityBuffer();
	bool initializeImGui();

	void deinitializeWindow();
	void deinitializeRHI();
	void deinitializeImGui();

	void tick();

	void updateCamera(float deltaTime);

	// Two useful starting points: outside looking at the whole thing, which suits a single
	// object, and standing inside it, which is the only way to see an interior like Sponza.
	void frameModel();
	void enterScene();

private:
	std::shared_ptr<mv::rhi::IRHI> rhi_;

	// Owns the material bind group layout, the pipeline layout and the pipeline variants,
	// so the engine no longer restates the shader's binding contract.
	mv::material::MaterialSystem materialSystem_;

	// The scene constants change every frame, so each frame in flight gets its own buffer
	// and its own bind group pointing at it.
	std::vector<mv::rhi::BufferHandle> sceneBuffers_;
	std::vector<mv::rhi::BindGroupHandle> sceneBindGroups_;

	mv::rhi::TextureHandle depthTexture_ = mv::INVALID_HANDLE;

	// --- visibility buffer ----------------------------------------------------

	// R32G32_UINT holding (drawIndex + 1, primitiveID) per pixel.
	mv::rhi::TextureHandle visibilityTexture_ = mv::INVALID_HANDLE;

	// drawIndex -> where that draw's triangles start and which material it uses. The
	// resolve pass has only the ids from the visibility buffer to work from.
	mv::rhi::BufferHandle drawInfoBuffer_ = mv::INVALID_HANDLE;

	// set 2, bound only by the resolve pass so the visibility buffer is never both a
	// render target and a bound resource.
	mv::rhi::BindGroupLayoutHandle vbResourceLayout_ = mv::INVALID_HANDLE;
	mv::rhi::BindGroupHandle vbResourceGroup_ = mv::INVALID_HANDLE;

	// Reuses the material cache's variant logic for the id pass, which still needs the
	// double-sided distinction even though it writes no colour.
	mv::material::MaterialPipelineCache vbPipelineCache_;

	mv::rhi::PipelineLayoutHandle vbShadePipelineLayout_ = mv::INVALID_HANDLE;
	mv::rhi::PipelineHandle vbShadePipeline_ = mv::INVALID_HANDLE;

	bool useVisibilityBuffer_ = true;

	// --- streaming feedback ---------------------------------------------------

	// Written by the resolve pass, copied into readback each frame so the CPU can see
	// which mip each texture is actually being asked for. The streaming manager that will
	// consume this does not exist yet; this proves the round trip works.
	mv::rhi::BufferHandle feedbackBuffer_ = mv::INVALID_HANDLE;
	mv::rhi::BufferHandle feedbackReadback_ = mv::INVALID_HANDLE;

	// Applies the same base mip to every texture, to exercise mip-range views.
	int forcedBaseMip_ = 0;

	// Index into kDebugModeNames; must stay in step with the MV_DEBUG_* defines in
	// common.hlsli.
	int debugMode_ = 0;

	// Owns the page atlases and the page table. Its buffers live in scene set 0, so it
	// has to exist before the scene bind groups are built.
	mv::vt::VirtualTextureSystem virtualTextures_;

	bool vtEnabled_ = true;

	mv::asset::GltfLoader gltfLoader_;
	mv::asset::Model model_;

	mv::ui::ImGuiRenderer imguiRenderer_;

	// Free-fly camera. An orbit rig cannot get inside a scene like Sponza, and walking
	// through it is the only way to see the artifacts a visibility buffer can produce.
	mv::math::Vec3 cameraPosition_{};
	float cameraYaw_ = 0.0f;
	float cameraPitch_ = 0.0f;

	// World units per second, scaled to the model so it works for a helmet and a cathedral.
	float cameraSpeed_ = 1.0f;

	float cameraNear_ = 0.1f;
	float cameraFar_ = 100.0f;

	float lightIntensity_ = 3.0f;
	float ambientIntensity_ = 0.25f;

	// Relative to the executable. Sponza is the interesting case for the material system:
	// many materials, shared textures and alpha-masked foliage. DamagedHelmet.glb is the
	// small alternative.
	const char* modelPath_ = "assets\\models\\Sponza\\Sponza.gltf";

	// The one switch for the sample: picks the backend, and with it whether the SPIR-V
	// or the DXIL build of the shaders is loaded.
	bool useVulkan_ = false;

	HWND hwnd_ = nullptr;

	mv::types::u32 width_ = 1280;
	mv::types::u32 height_ = 720;
};


#endif










