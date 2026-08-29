
#ifndef _MV_ENVIRONMENT_H_
#define _MV_ENVIRONMENT_H_

#include <memory>
#include <vector>

#include "rhi/rhi.h"

#include "compute/environment_baker.h"

#include "util/math.h"
#include "util/types.h"

namespace mv
{
	namespace env
	{
		using namespace types;

		// Must match ibl.hlsli.
		//
		// The cube is small on purpose. It only ever feeds a skybox, a nine-coefficient
		// projection and a roughness-filtered chain, none of which carry detail beyond this.
		constexpr u32 kCubeFaceSize = 128;

		// 128, 64, 32, 16, 8, 4: roughness 0 at the top, fully rough at the bottom.
		constexpr u32 kMipCount = 6;

		// Three bands. Nine coefficients reconstruct diffuse irradiance to within about one
		// percent for any environment, because the cosine lobe a Lambertian surface
		// integrates against annihilates everything above the second band.
		constexpr u32 kShCoefficientCount = 9;

		struct SkyParams
		{
			// The direction light travels, matching the scene's light direction, so the sun
			// in the sky and the sun casting shadows are the same object.
			math::Vec3 lightDirection{ -0.4f, -0.8f, -0.45f };

			// Haze. 2 is a clear day; higher values whiten the sky and widen the sun's halo.
			f32 turbidity = 2.5f;

			f32 sunIntensity = 20.0f;

			// What the lower hemisphere bounces back. This is the whole reason a shaded
			// surface facing down is not black, and it is most of what the second SH band
			// ends up encoding.
			math::Vec3 groundAlbedo{ 0.3f, 0.28f, 0.25f };
		};

		// A procedural sky, its diffuse irradiance as spherical harmonics, and its
		// roughness-prefiltered chain for specular.
		//
		// Everything is baked on the CPU. The sky is an analytic function, so there is no
		// source image to load, and building the chain here rather than in a compute pass
		// keeps the whole thing to one texture upload and no new render targets.
		class Environment
		{
		public:
			// baker is optional. Without one everything below runs on the CPU exactly as
			// before -- and the CPU sky model stays either way, because the two have to be
			// comparable for the port to be checkable at all.
			bool initialize(const std::shared_ptr<rhi::IRHI>& rhi, compute::EnvironmentBaker* baker = nullptr);

			// Whether the last bake ran as dispatches.
			bool bakedOnGpu() const { return bakedOnGpu_; }
			void deinitialize();

			// Rebuilds the sky, the prefiltered chain and the coefficients, then uploads.
			// Costs tens of milliseconds, so it runs when the sky changes rather than per
			// frame.
			// The cloud layer is handed straight to the baker, which marches it into level 0 so
			// that the irradiance and the prefiltered chain both carry it. Defaulted, because a
			// build with no clouds still has a sky.
			void bake(const SkyParams& params, const compute::EnvironmentBaker::CloudLayer& clouds = {});

			rhi::TextureHandle cubemap() const { return cubemap_; }

			// Nine RGB coefficients, laid out as sh[i * 3 + channel].
			const f32* shCoefficients() const { return sh_; }

			f32 lastBakeMilliseconds() const { return lastBakeMs_; }

		private:
			// The radiance arriving from `direction`, from the sky above the horizon and
			// from the ground below it.
			math::Vec3 skyRadiance(const math::Vec3& direction, const SkyParams& params) const;

			// Bilinear lookup into a baked mip level, by direction.
			math::Vec3 sampleLevel(const std::vector<math::Vec3>* faces, u32 size, const math::Vec3& direction) const;

			void projectToSh(const std::vector<math::Vec3>* faces, u32 size);

			void prefilter(const std::vector<math::Vec3>* source, u32 sourceSize, std::vector<math::Vec3>* target, u32 targetSize, f32 roughness);

		private:
			std::shared_ptr<rhi::IRHI> rhi_;
			compute::EnvironmentBaker* baker_ = nullptr;

			bool bakedOnGpu_ = false;

			rhi::TextureHandle cubemap_ = INVALID_HANDLE;

			f32 sh_[kShCoefficientCount * 3]{};

			// The colour the ground bounces back, integrated once per bake rather than per
			// downward texel.
			math::Vec3 groundRadiance_{};

			f32 lastBakeMs_ = 0.0f;
		};
	}
}

#endif
