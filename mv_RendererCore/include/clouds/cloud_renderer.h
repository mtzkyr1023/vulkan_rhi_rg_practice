#ifndef _MV_CLOUD_RENDERER_H_
#define _MV_CLOUD_RENDERER_H_

#include <memory>

#include "rhi/rhi.h"

#include "util/math.h"
#include "util/types.h"

namespace mv
{
	namespace clouds
	{
		using namespace types;

		// Voxels across the shape volume. 128 is the size the technique was published at and
		// it is a reasonable floor: the volume tiles across tens of kilometres, so every
		// voxel is a few tens of metres of sky and halving it shows as a repeat.
		constexpr u32 kShapeSize = 128;

		// The detail volume only ever erodes edges, so it can be much smaller.
		constexpr u32 kDetailSize = 32;

		constexpr u32 kWeatherSize = 512;

		// The cloud shadow map, in texels across. It covers kilometres, so a texel is metres
		// in the tens -- finer than the layer has structure at.
		constexpr u32 kShadowSize = 512;

		struct CloudParams
		{
			// Metres above the ground. A real cumulus layer sits around here.
			// Low, and close. A cumulus base really does sit a kilometre and a half up, but at
			// that height over a two-kilometre terrain the layer is a ceiling: it never comes
			// down to the horizon inside the scene, and its shadows are the same shadow
			// everywhere. Bringing it down to the height of the peaks is what puts the clouds
			// in the same world as the ground rather than above it.
			f32 layerBottom = 700.0f;
			f32 layerTop = 1800.0f;

			// The curvature the shell follows. Not the real Earth: a smaller planet bends
			// the layer down to the horizon sooner, which is what stops a flat slab from
			// running off to infinity in a scene only a few hundred metres across.
			f32 planetRadius = 6360000.0f;

			// How much of the sky has cloud in it, before the noise decides where.
			f32 coverage = 0.55f;

			// Metres across one repeat of each volume.
			// Sized against the terrain rather than against the sky. One cloud is now a little
			// larger than the whole map, which is what makes a shadow cross it as a shape over
			// tens of seconds instead of dimming all of it at once over minutes.
			f32 shapeScale = 3000.0f;
			f32 detailScale = 250.0f;

			// Metres across one repeat of the weather map.
			f32 weatherScale = 10000.0f;

			f32 detailStrength = 0.35f;
			f32 densityScale = 1.0f;
			f32 extinction = 0.08f;

			// Henyey-Greenstein asymmetry for the two lobes, and the mix between them.
			f32 forwardScattering = 0.8f;
			f32 backwardScattering = 0.3f;
			f32 scatterBlend = 0.5f;

			f32 ambientStrength = 0.25f;

			// How far the march runs before the layer is left to the sky.
			f32 maxDistance = 40000.0f;

			u32 viewSteps = 96;
			u32 lightSteps = 6;

			// Metres per second, and the direction the layer drifts.
			f32 windSpeed = 25.0f;
			math::Vec3 windDirection{ 1.0f, 0.0f, 0.3f };

			u32 seed = 3571;

			// --- shadows onto the scene -----------------------------------------

			// Metres the shadow map spans, centred on the camera. Wide enough that the
			// camera cannot fly off it, fine enough at 512 texels that one texel is a few
			// tens of metres.
			f32 shadowExtent = 6000.0f;

			// How much of the direct sun a fully opaque column takes away. One is the
			// physical answer; less is the artistic one.
			f32 shadowStrength = 1.0f;

			u32 shadowSteps = 24;
		};

		// Volumetric clouds: two baked noise volumes, a weather map, and a raymarch.
		//
		// The volumes are baked once (and again when the seed or coverage changes); the
		// march runs every frame at half resolution and is composited into the HDR scene
		// target by a fullscreen blend.
		class CloudRenderer
		{
		public:
			struct Shaders
			{
				const u32* shape = nullptr;      u32 shapeSize = 0;
				const u32* detail = nullptr;     u32 detailSize = 0;
				const u32* weather = nullptr;    u32 weatherSize = 0;
				const u32* march = nullptr;      u32 marchSize = 0;
				const u32* shadow = nullptr;     u32 shadowSize = 0;

				const u32* compositeVs = nullptr; u32 compositeVsSize = 0;
				const u32* compositePs = nullptr; u32 compositePsSize = 0;
			};

			bool initialize(
				const std::shared_ptr<rhi::IRHI>& rhi,
				const Shaders& shaders,
				rhi::ETextureFormat sceneColorFormat);

			void deinitialize();

			bool isReady() const { return ready_; }

			// Rebakes the volumes and the weather map. Costs milliseconds, so it runs when
			// the parameters change rather than per frame.
			void bake(const CloudParams& params);

			// Sizes the half-resolution target to the window. Safe to call every frame; it
			// only rebuilds when the size actually changed.
			void resize(u32 width, u32 height);

			// Advances the wind. Separate from record so the caller decides whether time
			// passes -- a paused frame should not slide the layer.
			void advance(f32 deltaSeconds) { windDistance_ += deltaSeconds; }

			// The march, into the half-resolution target. Reads the scene depth, so it has
			// to run after the geometry passes.
			// The camera is passed as a basis rather than a matrix: the ray is rebuilt the
			// way the skybox rebuilds it, which needs no inverse view-projection and no
			// agreement about how a matrix is packed into a constant buffer.
			struct View
			{
				math::Vec3 position;
				math::Vec3 forward;

				f32 fovY = 0.0f;
				f32 nearZ = 0.0f;
				f32 farZ = 0.0f;
			};

			void recordMarch(
				rhi::CommandBufferHandle cmd,
				const CloudParams& params,
				const View& view,
				const math::Vec3& lightDirection,
				const math::Vec3& sunColor,
				rhi::TextureHandle sceneDepth);

			// The cloud shadow map: one dispatch, no dependency on anything else in the frame,
			// so it can go anywhere before the shading passes. Centred on focus, snapped to a
			// texel -- a map that followed the camera continuously would make every shadow
			// edge crawl as the camera moved.
			void recordShadow(
				rhi::CommandBufferHandle cmd,
				const CloudParams& params,
				const math::Vec3& lightDirection,
				const math::Vec3& focus);

			// The upsample and blend, into whatever colour target is currently bound.
			void recordComposite(rhi::CommandBufferHandle cmd, rhi::TextureHandle sceneDepth);

			rhi::TextureHandle shadowTexture() const { return shadowMap_; }

			// Where recordShadow last put the map. The shading pass needs both to turn a world
			// position into a lookup, and they only settle once recordShadow has run.
			// The map's light-space frame, for the shading side's lookup: a plane through
			// origin spanned by right and up, both perpendicular to the sun. Settled by
			// recordShadow, so they are one frame stale in the scene constants -- at camera
			// speeds that is under a texel.
			const math::Vec3& shadowOrigin() const { return shadowOrigin_; }
			const math::Vec3& shadowRight() const { return shadowRight_; }
			const math::Vec3& shadowUp() const { return shadowUp_; }
			f32 shadowExtent() const { return shadowExtent_; }

			// The baked volumes, for the environment cube, which marches the same layer from
			// the ground so that what the scene reflects agrees with what it sees.
			rhi::TextureHandle shapeVolume() const { return shapeVolume_; }
			rhi::TextureHandle detailVolume() const { return detailVolume_; }
			rhi::TextureHandle weatherMap() const { return weatherMap_; }

			// How far the layer has drifted, as a vector. The environment bake needs it to line
			// its own march up with the one on screen.
			math::Vec3 windOffset(const CloudParams& params) const
			{
				return math::normalize(params.windDirection) * (params.windSpeed * windDistance_);
			}

			f32 lastBakeMilliseconds() const { return lastBakeMs_; }

			u32 lowResWidth() const { return lowResWidth_; }
			u32 lowResHeight() const { return lowResHeight_; }

		private:
			void releaseTargets();

		private:
			std::shared_ptr<rhi::IRHI> rhi_;

			rhi::TextureHandle shapeVolume_ = INVALID_HANDLE;
			rhi::TextureHandle detailVolume_ = INVALID_HANDLE;
			rhi::TextureHandle weatherMap_ = INVALID_HANDLE;

			// Premultiplied scattering in rgb, transmittance in a.
			rhi::TextureHandle marchTarget_ = INVALID_HANDLE;

			// Transmittance of the layer along the sun ray, indexed by world XZ.
			rhi::TextureHandle shadowMap_ = INVALID_HANDLE;

			// --- bake ---------------------------------------------------------------

			rhi::BindGroupLayoutHandle volumeLayout_ = INVALID_HANDLE;
			rhi::PipelineLayoutHandle volumePipelineLayout_ = INVALID_HANDLE;

			rhi::PipelineHandle shapePipeline_ = INVALID_HANDLE;
			rhi::PipelineHandle detailPipeline_ = INVALID_HANDLE;
			rhi::PipelineHandle weatherPipeline_ = INVALID_HANDLE;

			rhi::BindGroupHandle shapeGroup_ = INVALID_HANDLE;
			rhi::BindGroupHandle detailGroup_ = INVALID_HANDLE;
			rhi::BindGroupHandle weatherGroup_ = INVALID_HANDLE;

			// --- march --------------------------------------------------------------

			rhi::BindGroupLayoutHandle marchLayout_ = INVALID_HANDLE;
			rhi::PipelineLayoutHandle marchPipelineLayout_ = INVALID_HANDLE;
			rhi::PipelineHandle marchPipeline_ = INVALID_HANDLE;
			rhi::BindGroupHandle marchGroup_ = INVALID_HANDLE;

			// --- shadow map ---------------------------------------------------------

			rhi::BindGroupLayoutHandle shadowLayout_ = INVALID_HANDLE;
			rhi::PipelineLayoutHandle shadowPipelineLayout_ = INVALID_HANDLE;
			rhi::PipelineHandle shadowPipeline_ = INVALID_HANDLE;
			rhi::BindGroupHandle shadowGroup_ = INVALID_HANDLE;

			math::Vec3 shadowOrigin_{};
			math::Vec3 shadowRight_{};
			math::Vec3 shadowUp_{};
			f32 shadowExtent_ = 0.0f;

			// --- composite ----------------------------------------------------------

			rhi::BindGroupLayoutHandle compositeLayout_ = INVALID_HANDLE;
			rhi::PipelineLayoutHandle compositePipelineLayout_ = INVALID_HANDLE;
			rhi::PipelineHandle compositePipeline_ = INVALID_HANDLE;
			rhi::BindGroupHandle compositeGroup_ = INVALID_HANDLE;

			// The depth texture the two groups currently name. The graph hands out a stable
			// handle frame to frame, so re-pointing the descriptor every frame would be
			// rewriting a set that frames still in flight are reading -- which Vulkan rejects
			// outright without the update-after-bind flag, and which is a race on both APIs.
			rhi::TextureHandle boundDepth_ = INVALID_HANDLE;

			u32 lowResWidth_ = 0;
			u32 lowResHeight_ = 0;

			// Metres the layer has drifted, kept as a scalar so the direction can change
			// without the clouds jumping.
			f32 windDistance_ = 0.0f;

			f32 lastBakeMs_ = 0.0f;

			bool ready_ = false;
		};
	}
}

#endif
