
#ifndef _ENGINE_H_
#define _ENGINE_H_

#include "rhi/rhi.h"
#include "rg/render_graph.h"
#include "ui/imgui_renderer.h"
#include "asset/gltf_loader.h"
#include "virtual_texture/virtual_texture.h"
#include "shadow/cascaded_shadow_map.h"
#include "env/environment.h"
#include "post/post_process.h"
#include "post/effects.h"
#include "terrain/terrain.h"
#include "compute/mip_generator.h"
#include "compute/buffer_fill.h"
#include "compute/terrain_builder.h"
#include "compute/environment_baker.h"
#include "clouds/cloud_renderer.h"
#include "water/water_surface.h"
#include "fog/height_fog.h"
#include "grass/grass_field.h"
#include "props/prop_renderer.h"
#include "props/skinned_prop_renderer.h"
#include "voxel/sculpt_volume.h"
#include "voxel/sculpt_gpu.h"
#include "anim/animator.h"
#include "debug/debug_line_renderer.h"

#include "game/game_clock.h"
#include "game/input.h"
#include "game/world.h"
#include "game/height_field.h"
#include "game/character_controller.h"
#include "game/physics_world.h"
#include "game/audio.h"
#include "game/game_state.h"
#include "hud/hud_renderer.h"

#include <unordered_map>
#include "util/noise.h"

#include <Windows.h>

class Engine
{
public:

	bool initialize(HINSTANCE hInstance, int nCmdShow);
	void deinitialize();

	void run();

	// Called from the window procedure. Only records the size: a drag sends a message per
	// pixel, and rebuilding every target that often would be unusable.
	void requestResize(mv::types::u32 width, mv::types::u32 height)
	{
		pendingWidth_ = width;
		pendingHeight_ = height;
	}


private:
	// Applies a pending size, if there is one and it is not a minimise.
	void applyPendingResize();

	bool initializeWindow(HINSTANCE hInstance, int nCmdShow);
	bool initializeRHI(void* hwnd);
	bool initializeSceneResources();
	bool initializeVisibilityBuffer();
	bool initializeShadows();
	bool initializeEnvironment();
	bool initializePostProcess();
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

	// Above the terrain looking across it, which neither of the other two gives: framing
	// puts the camera too far out to see relief, and entering drops it inside a hill.
	void surveyTerrain();

	// Re-points the passes at whichever model is active. Nothing is rebuilt except the
	// per-draw table and the three geometry bindings the resolve pass reads.
	void activateScene();

	// Regenerates the heightmap, the mesh and the baked maps from the current parameters.
	void rebuildTerrain();

	// Copies the heightmap into the buffer the grass vertex shader plants blades from.
	void uploadGrassHeightField();

	// Game state transitions with their side effects: entering play drops the
	// character to the ground under the camera, leaving it returns the fly camera.
	void startPlay();
	void exitToTitle();

	// Queues this frame's HUD content by game state; the UI pass draws it.
	void buildHud();

private:
	std::shared_ptr<mv::rhi::IRHI> rhi_;

	// Owns the material bind group layout, the pipeline layout and the pipeline variants,
	// so the engine no longer restates the shader's binding contract.
	mv::material::MaterialSystem materialSystem_;

	// The scene constants change every frame, so each frame in flight gets its own buffer
	// and its own bind group pointing at it.
	std::vector<mv::rhi::BufferHandle> sceneBuffers_;
	std::vector<mv::rhi::BindGroupHandle> sceneBindGroups_;


	// Where each pixel was on screen last frame, written alongside the shaded colour by
	// every pass that produces pixels. The temporal pass follows surfaces with this rather
	// than reprojecting depth, which only works while nothing but the camera moves.
	mv::rhi::TextureHandle velocityTexture_ = mv::INVALID_HANDLE;

	// The depth buffer, owned here rather than by the graph: the cloud pass names it in a
	// descriptor, and a graph transient has a different handle every frame.
	mv::rhi::TextureHandle sceneDepthTexture_ = mv::INVALID_HANDLE;

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

	// How elongated a footprint the virtual texture path refines for. 1 is the isotropic
	// choice, which is what the major-axis LOD gives.
	float vtMaxAnisotropy_ = 4.0f;

	// --- shadows --------------------------------------------------------------

	mv::shadow::CascadedShadowMap shadowMap_;

	// One pipeline for every cascade: the cascade index rides in the push constant and
	// the viewport picks the tile.
	mv::rhi::PipelineHandle shadowPipeline_ = mv::INVALID_HANDLE;

	bool shadowEnabled_ = true;

	// In depth-buffer units, applied on top of the rasterizer's slope-scaled bias.
	float shadowDepthBias_ = 0.0015f;

	// In shadow texels, along the surface normal.
	float shadowNormalBias_ = 1.5f;

	int shadowPcfRadius_ = 1;

	// Fraction of a cascade over which it crossfades into the next.
	float shadowCascadeBlend_ = 0.15f;

	float shadowLambda_ = 1.0f;
	float shadowNearDistance_ = 1.0f;
	float shadowDistance_ = 40.0f;

	// --- environment ----------------------------------------------------------

	// The sky, its nine irradiance coefficients and its prefiltered chain.
	mv::env::Environment environment_;
	mv::env::SkyParams skyParams_;

	mv::rhi::PipelineHandle skyboxPipeline_ = mv::INVALID_HANDLE;

	bool skyboxEnabled_ = true;
	bool iblEnabled_ = true;

	float iblIntensity_ = 1.0f;

	// The sun drives the sky, the shadows and the irradiance together, so it is steered
	// from here rather than being a constant buried in the frame setup.
	float sunAzimuth_ = 2.3f;
	float sunElevation_ = 0.9f;

	// Baking costs tens of milliseconds, so it happens when the sun moves rather than
	// every frame.
	bool environmentDirty_ = true;

	// --- post process ---------------------------------------------------------

	// The chain between the scene target and the backbuffer. The geometry passes no longer
	// write the backbuffer directly, and tone mapping is the chain's job rather than the
	// last line of the material shader.
	mv::post::PostProcessStack postStack_;

	// Borrowed, owned by the stack. Needed here because the jitter has to reach the
	// projection matrix before the frame is drawn, which is long before the chain runs.
	mv::post::TemporalAntiAliasing* taa_ = nullptr;

	// Counts frames rather than reusing the frame-in-flight index, which only has two
	// values and would collapse the jitter sequence to two points.
	mv::types::u32 frameCounter_ = 0;

	// Last frame's unjittered view-projection, for temporal reprojection.
	mv::math::Mat4 prevViewProj_ = mv::math::Mat4::identity();

	mv::asset::GltfLoader gltfLoader_;
	mv::asset::Model model_;

	// --- compute --------------------------------------------------------------

	// Fills mip chains with a dispatch instead of a CPU resize. Owned here because it
	// needs a shader, and loading those is the engine's job.
	mv::compute::MipGenerator mipGenerator_;

	// Resets the streaming feedback buffer at the top of every frame, so what the readback
	// shows is this frame rather than every frame the camera has ever been in.
	mv::compute::BufferFill feedbackClear_;

	// Heightmap, mesh and material maps as four dispatches. Owned here for the same reason
	// as the mip generator: it needs shaders.
	mv::compute::TerrainBuilder terrainBuilder_;

	// The sky and its prefiltered chain as dispatches. The nine coefficients stay on the
	// CPU; see EnvironmentBaker for why.
	mv::compute::EnvironmentBaker environmentBaker_;

	// --- clouds ---------------------------------------------------------------

	// Two baked noise volumes, a weather map and a half-resolution raymarch, composited
	// into the HDR scene target between the geometry and the post-process chain.
	mv::clouds::CloudRenderer clouds_;
	mv::clouds::CloudParams cloudParams_;

	bool cloudsEnabled_ = true;

	// Set by the parameter widgets; the volumes are rebaked once the control is let go.
	bool cloudsDirty_ = false;

	// --- water ----------------------------------------------------------------

	// A plane solved per pixel, blended into the HDR target after the geometry and before
	// the clouds. It reflects the environment cube, so what it mirrors is the sky the rest
	// of the frame is lit by -- clouds included.
	mv::water::WaterSurface water_;
	mv::water::WaterParams waterParams_;

	// --- fog ------------------------------------------------------------------

	// Exponential height haze over everything the depth buffer knows about. One
	// fullscreen pass, no state: the profile integrates in closed form.
	mv::fog::HeightFog fog_;
	mv::fog::FogParams fogParams_;

	// --- grass ----------------------------------------------------------------

	// Procedural blades planted on the terrain by the vertex shader. The engine's whole
	// contribution is a copy of the heightmap in a buffer and one draw call.
	mv::grass::GrassField grass_;
	mv::grass::GrassParams grassParams_;

	// The heightmap as the grass shader reads it. Recreated when the resolution changes,
	// refilled on every terrain rebuild.
	mv::rhi::BufferHandle grassHeightBuffer_ = mv::INVALID_HANDLE;
	mv::types::u32 grassHeightResolution_ = 0;

	// --- game -----------------------------------------------------------------

	// The gameplay modules, exercised by play mode: walk the terrain as a character
	// instead of flying the camera. The world holds the player entity, which is the
	// shape everything else will grow into -- gameplay writes transforms, rendering
	// reads them.
	mv::game::Input gameInput_;
	mv::game::GameClock gameClock_;
	mv::game::HeightField groundField_;
	mv::game::CharacterController player_;
	mv::game::CharacterParams playerParams_;
	mv::game::World world_;
	mv::game::EntityHandle playerEntity_{};

	// Bullet, wrapped: the terrain as a heightfield body, thrown props as spheres. It
	// steps inside play mode's fixed loop, so simulation and character share one clock.
	mv::game::PhysicsWorld physics_;

	// Title / Playing / Paused, with the legal moves written down in the machine.
	// What entering a state means stays here: startPlay teleports the character,
	// pause freezes the fixed clock by simply not ticking it.
	mv::game::GameStateMachine gameState_;

	// The player's screen -- crosshair, titles, prompts -- as opposed to ImGui's
	// developer panel. Drawn on the backbuffer under the debug UI.
	mv::hud::HudRenderer hudRenderer_;
	bool hudEnabled_ = true;

	// The fox: the renderer's first deforming mesh. The animator keeps the joint
	// palette current every frame; the fox itself trots a circle around an anchor
	// that startPlay drops in front of the player.
	mv::asset::SkinnedModel foxModel_;
	mv::anim::Animator foxAnimator_;
	mv::props::SkinnedPropRenderer skinnedPropRenderer_;

	bool foxLoaded_ = false;
	mv::math::Vec3 foxAnchor_{ 0.0f, 0.0f, 0.0f };
	mv::math::Vec3 foxPosition_{ 0.0f, 0.0f, 0.0f };
	mv::types::f32 foxAngle_ = 0.0f;
	mv::types::f32 foxYaw_ = 0.0f;
	mv::types::f32 foxScale_ = 1.0f;
	mv::types::f32 foxSpeedScale_ = 1.0f;

	// The carvable ground: a grid of marching-cubes chunks laid over the terrain
	// around the first play spawn, each initialised from the heightfield so the
	// world can be dug anywhere inside the region. One SculptVolume per chunk is
	// the CPU mirror Bullet eats; the GPU draws.
	static constexpr mv::types::u32 kSculptChunksPerSide = 5;

	std::vector<mv::voxel::SculptVolume> sculptChunks_;
	std::vector<bool> sculptChunkDirty_;
	bool sculptPlaced_ = false;
	mv::types::f32 sculptCooldown_ = 0.0f;

	// Streaming: chunks live at absolute cell coordinates and map onto the fixed
	// slots toroidally, so a window that follows the player refills only the cells
	// that changed -- the slot a cell vacates is exactly the slot its replacement
	// wants. Edited cells stash their density on the way out and restore on return.
	struct SculptSlotState
	{
		mv::types::s32 cx = -1000000;
		mv::types::s32 cz = -1000000;
		bool everEdited = false;
	};

	std::vector<SculptSlotState> sculptSlotStates_;
	std::vector<std::pair<mv::types::s32, mv::types::s32>> sculptRefillQueue_;
	std::unordered_map<mv::types::u64, std::vector<mv::types::f32>> sculptSavedDensity_;

	void refillSculptSlot(mv::types::u32 slot, mv::types::s32 cx, mv::types::s32 cz);

	// The GPU mesh path for the same density: compute marching cubes into an
	// append buffer, drawn indirect. The CPU mesh stays behind it for Bullet.
	mv::voxel::SculptGpu sculptGpu_;

	// Continuous deformation: the carved shape resampled through a travelling wave
	// and re-marched every frame, entirely on the GPU. Visual only -- the physics
	// mesh stays the still shape, which at these amplitudes nobody's feet notice.
	// The physics mesh trails the strokes: dirty on every brush, rebuilt once the
	// stroke has paused. Solid surfaces may be a beat stale; strokes never hitch.
	bool sculptPhysicsDirty_ = false;
	mv::types::f32 sculptLastEditTime_ = 0.0f;

	bool sculptAnimate_ = false;
	mv::types::f32 sculptWaveAmplitude_ = 0.6f;
	mv::types::f32 sculptWaveLength_ = 6.0f;
	mv::types::f32 sculptWaveSpeed_ = 1.5f;
	mv::types::f32 sculptTime_ = 0.0f;

	// The near/far actually in this frame's projection: the fly camera's
	// scene-scaled planes, or human-scale ones on foot. Every pass that
	// linearises depth must read these, or clouds, water and fog disagree with
	// the depth buffer about where things are.
	float activeNear_ = 0.25f;
	float activeFar_ = 1000.0f;

	// A jump pressed on a frame that drains no fixed step would vanish -- at hundreds of
	// frames per second most frames drain none -- so the edge waits here until a step
	// consumes it.
	bool jumpQueued_ = false;

	// The models entities stand in the world as -- Entity::primitive indexes this
	// array -- and the pass that draws them. Each keeps its bounds centre so the
	// render mesh and the physics shape agree on where the middle is: the physics
	// shape is built centred, and the draw matrix shifts the mesh to match.
	struct PropAsset
	{
		mv::asset::Model model;
		mv::math::Vec3 center{};
	};

	static constexpr mv::types::u32 kPropHelmet = 0;
	static constexpr mv::types::u32 kPropBox = 1;

	PropAsset propAssets_[2];
	mv::props::PropRenderer propRenderer_;

	// The helmet's centred vertex positions, kept for building its convex hull, and
	// the box's half extents measured from its own bounds.
	std::vector<mv::types::f32> helmetHullPoints_;
	mv::math::Vec3 boxHalfExtents_{ 0.5f, 0.5f, 0.5f };

	// What the play-mode aim ray last landed on, for the UI readout.
	mv::game::RayHit aimHit_{};
	bool aimValid_ = false;

	// Sound: XAudio2 behind AudioSystem, fed synthesised one-shots. The triggers all
	// live in gameplay code -- contact events for impacts, water crossings for
	// splashes, the character's grounded edges and stride for feet.
	mv::game::AudioSystem audio_;

	mv::game::SoundHandle soundImpact_ = mv::game::kInvalidSound;
	mv::game::SoundHandle soundSplash_ = mv::game::kInvalidSound;
	mv::game::SoundHandle soundFootstep_ = mv::game::kInvalidSound;
	mv::game::SoundHandle soundJump_ = mv::game::kInvalidSound;
	mv::game::SoundHandle soundLand_ = mv::game::kInvalidSound;

	// Contact plumbing: the frame's events, and a per-body cooldown so a box
	// grinding down a slope reads as occasional knocks, not a machine gun.
	std::vector<mv::game::ContactEvent> contactEvents_;
	std::unordered_map<mv::types::u32, mv::types::f32> impactCooldowns_;
	mv::types::f32 audioClock_ = 0.0f;

	// Footsteps by stride: distance walked while grounded since the last step sound.
	mv::types::f32 footstepDistance_ = 0.0f;
	mv::math::Vec3 prevPlayerPosition_{ 0.0f, 0.0f, 0.0f };
	bool prevGrounded_ = false;

	// Bullet's collision wireframes over the scene, when asked for: capsule, props,
	// hulls -- what the solver actually collides, as opposed to what the meshes show.
	mv::debugdraw::DebugLineRenderer physicsDebugRenderer_;
	bool physicsDebugDraw_ = false;
	std::vector<mv::game::DebugLine> physicsDebugLines_;
	std::vector<mv::debugdraw::DebugLineRenderer::Vertex> physicsDebugVertices_;

	// --- terrain --------------------------------------------------------------

	// A heightmap turned into an asset::Model, which is why nothing downstream needs to
	// know it exists: it draws, shadows and resolves exactly like a loaded one.
	mv::terrain::Terrain terrain_;
	mv::terrain::TerrainDesc terrainDesc_;

	// The field the heightmap is generated from. Ridged rather than plain fBm, and warped,
	// because that is the combination that reads as mountains rather than as noise.
	mv::noise::NoiseDesc terrainNoise_ = []
	{
		mv::noise::NoiseDesc desc{};
		desc.basis = mv::noise::EBasis::ePerlin;
		desc.fractal = mv::noise::EFractal::eBillow;
		desc.frequency = 2.5f;
		desc.octaves = 7;
		desc.lacunarity = 2.05f;
		desc.gain = 0.5f;
		desc.warpStrength = 0.22f;
		desc.warpFrequency = 1.6f;
		desc.seed = 20250823;
		return desc;
	}();

	// Whichever of the two the passes are currently drawing. The visibility buffer
	// resolves triangles out of one global vertex array, so switching scenes means
	// re-pointing set 2 at a different pair of buffers rather than drawing both.
	const mv::asset::Model* scene_ = &model_;

	bool terrainScene_ = true;

	// Set by the parameter widgets, spent once the control being dragged is let go.
	bool terrainDirty_ = false;

	mv::ui::ImGuiRenderer imguiRenderer_;

	// Free-fly camera. An orbit rig cannot get inside a scene like Sponza, and walking
	// through it is the only way to see the artifacts a visibility buffer can produce.
	mv::math::Vec3 cameraPosition_{};
	float cameraYaw_ = 0.0f;
	float cameraPitch_ = 0.0f;

	// World units per second, scaled to the model so it works for a helmet and a cathedral.
	float cameraSpeed_ = 1.0f;

	float cameraNear_ = 0.01f;
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

	// The size the window most recently became, applied at the top of the next frame.
	mv::types::u32 pendingWidth_ = 0;
	mv::types::u32 pendingHeight_ = 0;
};


#endif










