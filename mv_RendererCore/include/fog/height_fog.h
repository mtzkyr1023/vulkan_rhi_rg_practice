#ifndef _MV_HEIGHT_FOG_H_
#define _MV_HEIGHT_FOG_H_

#include <memory>

#include "rhi/rhi.h"

#include "util/math.h"
#include "util/types.h"

namespace mv
{
	namespace fog
	{
		using namespace types;

		struct FogParams
		{
			// Extinction per metre at sea level. The working range is small numbers: at
			// 0.0005 a ridge two kilometres out keeps about a third of its contrast, which
			// reads as distance rather than as weather.
			f32 density = 0.0005f;

			// 1 / metres over which the density falls to 1/e. 1/200 puts the fog in the
			// valleys and leaves the peaks of a 280-metre terrain mostly clear of it.
			f32 heightFalloff = 0.005f;

			// Metres of clear air before the haze starts.
			f32 startDistance = 0.0f;

			// The most a pixel is allowed to be swallowed. 1 is the physical answer.
			f32 maxOpacity = 1.0f;

			// The in-scattered sun. Zero collapses the march back into the closed form;
			// anything above it buys sunbeams through the gaps in the terrain and the
			// clouds, and dark bands through their shadows.
			f32 shaftIntensity = 0.6f;

			// Henyey-Greenstein asymmetry: how strongly the beams favour looking towards
			// the sun. Forward, because that is where anyone has ever seen a god ray.
			f32 shaftAnisotropy = 0.6f;

			// Metres the march covers before handing over to the closed form, and how many
			// steps pay for it. The temporal pass smooths the jitter between steps.
			f32 shaftDistance = 2000.0f;
			u32 shaftSteps = 24;

			bool enabled = true;
		};

		// Exponential height fog and the light shafts inside it, as one fullscreen pass.
		//
		// The fog itself has a closed-form integral; the shafts are a short jittered march
		// through the same profile that asks the cascade atlas and the cloud shadow map
		// whether the sun reaches each piece of air. It owns nothing but a pipeline and one
		// depth descriptor -- everything else it samples arrives through the scene set.
		class HeightFog
		{
		public:
			struct Shaders
			{
				const u32* vs = nullptr; u32 vsSize = 0;
				const u32* ps = nullptr; u32 psSize = 0;
			};

			// The scene and bindless layouts come along because the shafts sample the
			// cascade atlas and the cloud shadow map, which live in the scene set: the
			// pipeline layout is [scene, bindless, own], the visibility resolve's
			// arrangement.
			bool initialize(
				const std::shared_ptr<rhi::IRHI>& rhi,
				const Shaders& shaders,
				rhi::ETextureFormat sceneColorFormat,
				rhi::BindGroupLayoutHandle sceneLayout,
				rhi::BindGroupLayoutHandle bindlessLayout);

			void deinitialize();

			bool isReady() const { return ready_; }

			struct View
			{
				math::Vec3 position;
				math::Vec3 forward;

				f32 fovY = 0.0f;
				f32 nearZ = 0.0f;
				f32 farZ = 0.0f;

				u32 width = 0;
				u32 height = 0;
			};

			// Draws into whatever colour target is bound, exactly as the water pass does,
			// with the same lazy bind group and the same re-point-only-on-change rule.
			void record(
				rhi::CommandBufferHandle cmd,
				const FogParams& params,
				const View& view,
				const math::Vec3& lightDirection,
				f32 sunIntensity,
				rhi::BindGroupHandle sceneGroup,
				rhi::BindGroupHandle bindlessGroup,
				rhi::TextureHandle sceneDepth);

		private:
			std::shared_ptr<rhi::IRHI> rhi_;

			rhi::BindGroupLayoutHandle layout_ = INVALID_HANDLE;
			rhi::PipelineLayoutHandle pipelineLayout_ = INVALID_HANDLE;
			rhi::PipelineHandle pipeline_ = INVALID_HANDLE;
			rhi::BindGroupHandle group_ = INVALID_HANDLE;

			rhi::TextureHandle boundDepth_ = INVALID_HANDLE;

			bool ready_ = false;
		};
	}
}

#endif
