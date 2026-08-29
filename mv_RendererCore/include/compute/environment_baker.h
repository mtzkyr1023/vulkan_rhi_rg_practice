#ifndef _MV_ENVIRONMENT_BAKER_H_
#define _MV_ENVIRONMENT_BAKER_H_

#include <memory>
#include <vector>

#include "rhi/rhi.h"

#include "util/math.h"
#include "util/types.h"

namespace mv
{
	namespace env
	{
		struct SkyParams;
	}

	namespace compute
	{
		using namespace types;

		// The sky and its roughness-prefiltered chain, as dispatches.
		//
		// What does not move here is the spherical-harmonic projection. Those nine RGB
		// coefficients are a solid-angle-weighted sum over every texel of every face, and
		// they have to arrive in the scene constant buffer on the CPU. A two-stage GPU
		// reduction and a readback to deliver twenty-seven floats would be more machinery
		// than the projection costs; the sky pass writes its radiance to a buffer, that is
		// copied back, and the existing CPU projection runs on it unchanged.
		class EnvironmentBaker
		{
		public:
			struct Shaders
			{
				const u32* sky = nullptr;        u32 skySize = 0;
				const u32* prefilter = nullptr;  u32 prefilterSize = 0;
			};

			bool initialize(const std::shared_ptr<rhi::IRHI>& rhi, const Shaders& shaders);
			void deinitialize();

			bool isReady() const { return ready_; }

			// The cloud layer, for the sky pass to march into the cube.
			//
			// Passed as plain fields rather than as a CloudParams so this header does not
			// have to include the cloud renderer: the environment is baked from the sky
			// model, and the clouds are one more thing in front of it, not a dependency of
			// what the baker is.
			//
			// Leave steps at zero and the pass marches nothing, which is what a build with
			// the clouds switched off wants. The three textures still have to be something
			// -- a descriptor that names nothing is invalid even when no thread reads it --
			// so the baker keeps a one-texel stand-in for each and uses it when they are
			// not supplied.
			struct CloudLayer
			{
				rhi::TextureHandle shape = INVALID_HANDLE;
				rhi::TextureHandle detail = INVALID_HANDLE;
				rhi::TextureHandle weather = INVALID_HANDLE;

				// Matching CloudFieldConstants in clouds.hlsli.
				f32 windOffset[3]{};
				f32 coverageScale = 0.0f;

				f32 planetRadius = 0.0f;
				f32 layerBottom = 0.0f;
				f32 layerTop = 0.0f;
				f32 shapeScale = 0.0f;

				f32 detailScale = 0.0f;
				f32 detailStrength = 0.0f;
				f32 densityScale = 0.0f;
				f32 extinction = 0.0f;

				f32 sunColor[3]{};
				f32 ambientStrength = 0.0f;

				f32 forwardScattering = 0.0f;
				f32 backwardScattering = 0.0f;
				f32 scatterBlend = 0.0f;

				u32 steps = 0;
			};

			// Fills every level of `cubemap` and copies level 0's radiance into
			// outRadiance, laid out face-major so the caller's projection can walk it.
			//
			// The cube must have been created with eStorage as well as eSampled.
			void bake(
				const env::SkyParams& params,
				rhi::TextureHandle cubemap,
				u32 faceSize,
				u32 mipCount,
				std::vector<math::Vec3>& outRadiance,
				const CloudLayer& clouds);

		private:
			std::shared_ptr<rhi::IRHI> rhi_;

			rhi::BindGroupLayoutHandle skyLayout_ = INVALID_HANDLE;
			rhi::BindGroupLayoutHandle prefilterLayout_ = INVALID_HANDLE;

			rhi::PipelineLayoutHandle skyPipelineLayout_ = INVALID_HANDLE;
			rhi::PipelineLayoutHandle prefilterPipelineLayout_ = INVALID_HANDLE;

			rhi::PipelineHandle skyPipeline_ = INVALID_HANDLE;
			rhi::PipelineHandle prefilterPipeline_ = INVALID_HANDLE;

			// Level 0's radiance, and the staging copy the CPU reads it through.
			rhi::BufferHandle radianceBuffer_ = INVALID_HANDLE;
			rhi::BufferHandle radianceReadback_ = INVALID_HANDLE;

			u32 radianceCapacity_ = 0;

			// One texel each, bound when no cloud volumes are supplied.
			rhi::TextureHandle dummyVolume_ = INVALID_HANDLE;
			rhi::TextureHandle dummyMap_ = INVALID_HANDLE;

			bool ready_ = false;
		};
	}
}

#endif
