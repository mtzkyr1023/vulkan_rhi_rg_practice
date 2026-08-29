
#ifndef _MV_CASCADED_SHADOW_MAP_H_
#define _MV_CASCADED_SHADOW_MAP_H_

#include <memory>

#include "rhi/rhi.h"

#include "util/math.h"
#include "util/types.h"

namespace mv
{
	namespace shadow
	{
		using namespace types;

		// Must match shadow.hlsli.
		//
		// Four cascades laid out as a 2x2 grid in one depth texture. An atlas rather than a
		// texture array because a viewport is all it takes to target one tile, and it keeps
		// the whole thing to a single descriptor and a single barrier.
		constexpr u32 kMaxCascades = 4;
		constexpr u32 kCascadeResolution = 2048;
		constexpr u32 kAtlasSize = kCascadeResolution * 2;

		// Everything the split scheme needs about the camera. Passed in rather than read
		// from a camera class so the fitting stays independent of how the app stores it.
		struct CameraView
		{
			math::Vec3 position{};
			math::Vec3 forward{};
			math::Vec3 right{};
			math::Vec3 up{};

			f32 fovY = 1.0f;
			f32 aspect = 1.0f;
			f32 nearZ = 0.1f;
		};

		struct Cascade
		{
			math::Mat4 viewProj = math::Mat4::identity();

			// View-space depth this cascade stops at, which is what the shader compares
			// against to choose one.
			f32 splitDepth = 0.0f;

			// World-space size of one shadow texel in this cascade. The normal offset bias
			// is expressed in these, so it stays constant in screen terms across cascades.
			f32 texelWorldSize = 0.0f;
		};

		// Cascaded shadow maps with logarithmic splits.
		//
		// A single shadow map spread over the whole view frustum wastes almost all of its
		// resolution far away, where a pixel covers a huge area, and has far too little up
		// close. Splitting the frustum by depth and giving each slice its own map fixes
		// that, and the split positions are what decide how evenly the resolution lands.
		//
		// Logarithmic splits (d_i = near * (far/near)^(i/N)) give every cascade the same
		// ratio of far to near, which is what makes the projected texel size roughly
		// constant across the whole view. The purely uniform alternative spends nearly
		// every cascade on the distance. In practice the logarithmic scheme puts its first
		// split extremely close to the camera, so lambda blends towards uniform; at 1.0 it
		// is purely logarithmic.
		class CascadedShadowMap
		{
		public:
			bool initialize(const std::shared_ptr<rhi::IRHI>& rhi);
			void deinitialize();

			// Recomputes splits and fits each cascade. The scene bounds decide how far back
			// along the light each cascade has to start so that casters outside the slice
			// still reach it.
			void update(
				const CameraView& camera,
				const math::Vec3& lightDirection,
				const math::Vec3& sceneBoundsMin,
				const math::Vec3& sceneBoundsMax);

			rhi::TextureHandle texture() const { return texture_; }

			const Cascade& cascade(u32 index) const { return cascades_[index]; }

			// Where cascade `index` lives in the atlas.
			void tileOrigin(u32 index, u32& x, u32& y) const
			{
				x = (index % 2) * kCascadeResolution;
				y = (index / 2) * kCascadeResolution;
			}

			// 0 splits the frustum uniformly, 1 splits it logarithmically.
			f32 lambda() const { return lambda_; }
			void setLambda(f32 value) { lambda_ = value; }

			// How far from the camera the cascades reach. Beyond this nothing is shadowed,
			// so it trades shadow range against the resolution every cascade gets.
			f32 distance() const { return distance_; }
			void setDistance(f32 value) { distance_ = value; }

			// Where the split progression starts, which is not the same as the camera near
			// plane. A logarithmic split is a ratio, so anchoring it to a near plane of a
			// few centimetres spends the first cascades on a sliver of space right in front
			// of the eye and leaves the whole visible scene to the last one. Cascade 0 still
			// starts at the camera, so nothing closer than this goes unshadowed.
			f32 nearDistance() const { return nearDistance_; }
			void setNearDistance(f32 value) { nearDistance_ = value; }

		private:
			std::shared_ptr<rhi::IRHI> rhi_;

			rhi::TextureHandle texture_ = INVALID_HANDLE;

			Cascade cascades_[kMaxCascades]{};

			f32 lambda_ = 1.0f;
			f32 distance_ = 40.0f;
			f32 nearDistance_ = 1.0f;
		};
	}
}

#endif
