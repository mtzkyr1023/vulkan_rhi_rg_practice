#ifndef _MV_GAME_HEIGHT_FIELD_H_
#define _MV_GAME_HEIGHT_FIELD_H_

#include <functional>

#include "util/math.h"
#include "util/types.h"

namespace mv
{
	namespace game
	{
		using namespace types;

		// The ground, as far as gameplay is concerned: a height for any horizontal
		// position, and the queries a character or a ray needs against it.
		//
		// It samples through a callback rather than owning terrain data, which is the
		// whole of its politics: the renderer owns a heightmap, a future game might own a
		// different one, and collision code should not care whose it is. The callback
		// takes world XZ in metres and answers height in metres.
		class HeightField
		{
		public:
			using Sampler = std::function<f32(f32 x, f32 z)>;

			void set(Sampler sampler, f32 sampleSpacing)
			{
				sampler_ = std::move(sampler);
				spacing_ = (sampleSpacing > 0.01f) ? sampleSpacing : 1.0f;
			}

			bool valid() const { return (bool)sampler_; }

			f32 heightAt(f32 x, f32 z) const
			{
				return sampler_ ? sampler_(x, z) : 0.0f;
			}

			// Central differences one sample apart, the same normal the terrain shades
			// with, so a character's slope test agrees with what the player sees.
			math::Vec3 normalAt(f32 x, f32 z) const;

			// Steps the ray until it crosses the ground, then bisects the crossing.
			// Returns whether it hit within maxDistance; hitT is metres along the ray.
			bool raycast(
				const math::Vec3& origin,
				const math::Vec3& direction,
				f32 maxDistance,
				f32& hitT) const;

		private:
			Sampler sampler_;
			f32 spacing_ = 1.0f;
		};
	}
}

#endif
