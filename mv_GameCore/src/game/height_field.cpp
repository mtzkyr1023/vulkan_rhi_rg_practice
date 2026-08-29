#include "game/height_field.h"

namespace mv::game
{
	math::Vec3 HeightField::normalAt(f32 x, f32 z) const
	{
		if (!sampler_)
			return { 0.0f, 1.0f, 0.0f };

		const f32 hL = sampler_(x - spacing_, z);
		const f32 hR = sampler_(x + spacing_, z);
		const f32 hD = sampler_(x, z - spacing_);
		const f32 hU = sampler_(x, z + spacing_);

		return math::normalize({
			(hL - hR) / (2.0f * spacing_),
			1.0f,
			(hD - hU) / (2.0f * spacing_) });
	}

	bool HeightField::raycast(
		const math::Vec3& origin,
		const math::Vec3& direction,
		f32 maxDistance,
		f32& hitT) const
	{
		if (!sampler_)
			return false;

		// March at the field's own resolution: finer steps sample detail the field does
		// not have, coarser ones step over ridges.
		const f32 stepLength = spacing_ * 0.5f;

		f32 previousT = 0.0f;
		f32 previousAbove = origin.y - heightAt(origin.x, origin.z);

		// Starting under the ground is its own answer.
		if (previousAbove <= 0.0f)
		{
			hitT = 0.0f;
			return true;
		}

		for (f32 t = stepLength; t <= maxDistance; t += stepLength)
		{
			const math::Vec3 p = origin + direction * t;
			const f32 above = p.y - heightAt(p.x, p.z);

			if (above <= 0.0f)
			{
				// Crossed between the previous sample and this one: bisect the interval.
				// The march found the metre, this finds the centimetre.
				f32 lo = previousT;
				f32 hi = t;

				for (int i = 0; i < 8; i++)
				{
					const f32 mid = (lo + hi) * 0.5f;
					const math::Vec3 q = origin + direction * mid;

					if (q.y - heightAt(q.x, q.z) <= 0.0f)
						hi = mid;
					else
						lo = mid;
				}

				hitT = hi;
				return true;
			}

			previousT = t;
			previousAbove = above;
		}

		return false;
	}
}
