#ifndef _MV_NOISE3_HLSLI_
#define _MV_NOISE3_HLSLI_

#include "noise.hlsli"

// Three-dimensional noise, for the cloud volumes.
//
// Everything here tiles. A cloud layer covers tens of kilometres and the volume that
// describes it is 128 voxels across, so the texture is repeated across the sky dozens of
// times over -- a seam would be a visible grid, repeated. Tiling means wrapping the lattice
// at the period, which is why every octave's frequency is rounded to an integer before use.

uint mvHashLattice3(int x, int y, int z, uint seed)
{
	return mvNoiseMix(uint(x) * 0x27d4eb2du ^ uint(y) * 0x165667b1u ^ uint(z) * 0x9e3779b9u ^ seed);
}

int3 mvWrapLattice3(int3 v, int period)
{
	if (period <= 0)
		return v;

	const int3 m = v % period;

	return select(m < 0, m + period, m);
}

// A unit vector per lattice point, from three hashes decorrelated by re-mixing.
float3 mvGradient3(uint h)
{
	const float u = mvHashToUnit(h) * 2.0f - 1.0f;
	const float phi = mvHashToUnit(mvNoiseMix(h)) * 6.28318530718f;

	const float r = sqrt(max(0.0f, 1.0f - u * u));

	return float3(r * cos(phi), r * sin(phi), u);
}

float mvPerlinNoise3(float3 p, uint seed, int period)
{
	const int3 i = int3(floor(p));
	const float3 f = p - float3(i);

	const float3 t = float3(
		mvSmoothstepQuintic(f.x),
		mvSmoothstepQuintic(f.y),
		mvSmoothstepQuintic(f.z));

	float corners[8];

	[unroll]
	for (int c = 0; c < 8; c++)
	{
		const int3 offset = int3(c & 1, (c >> 1) & 1, (c >> 2) & 1);
		const int3 lattice = mvWrapLattice3(i + offset, period);

		const float3 g = mvGradient3(mvHashLattice3(lattice.x, lattice.y, lattice.z, seed));

		corners[c] = dot(g, f - float3(offset));
	}

	const float x00 = lerp(corners[0], corners[1], t.x);
	const float x10 = lerp(corners[2], corners[3], t.x);
	const float x01 = lerp(corners[4], corners[5], t.x);
	const float x11 = lerp(corners[6], corners[7], t.x);

	const float y0 = lerp(x00, x10, t.y);
	const float y1 = lerp(x01, x11, t.y);

	// 3D Perlin peaks near sqrt(3)/2 with unit gradients.
	return lerp(y0, y1, t.z) * 1.1547f;
}

// Distance to the nearest of one feature point per cell, inverted so that the cell centres
// are bright and the boundaries dark. That inversion is what makes Worley read as billows
// rather than as a net, and it is the shape the whole cloud model is built on.
float mvWorleyNoise3(float3 p, uint seed, int period)
{
	const int3 i = int3(floor(p));

	float nearest = 1e30f;

	[unroll]
	for (int dz = -1; dz <= 1; dz++)
	{
		[unroll]
		for (int dy = -1; dy <= 1; dy++)
		{
			[unroll]
			for (int dx = -1; dx <= 1; dx++)
			{
				const int3 cell = i + int3(dx, dy, dz);
				const int3 wrapped = mvWrapLattice3(cell, period);

				const uint h = mvHashLattice3(wrapped.x, wrapped.y, wrapped.z, seed);

				// Jittered within the cell, not scattered freely: that is what bounds the
				// search to the 3x3x3 around the sample.
				const float3 feature = float3(cell) + float3(
					mvHashToUnit(h),
					mvHashToUnit(mvNoiseMix(h)),
					mvHashToUnit(mvNoiseMix(mvNoiseMix(h))));

				nearest = min(nearest, dot(feature - p, feature - p));
			}
		}
	}

	return saturate(1.0f - sqrt(nearest));
}

// Worley octaves, halving in amplitude as they double in frequency.
float mvWorleyFbm3(float3 p, uint seed, int basePeriod, uint octaves)
{
	float total = 0.0f;
	float amplitude = 1.0f;
	float normalisation = 0.0f;

	int period = basePeriod;

	for (uint o = 0; o < octaves; o++)
	{
		total += mvWorleyNoise3(p * float(period), seed + o * 0x9e3779b9u, period) * amplitude;
		normalisation += amplitude;

		amplitude *= 0.5f;
		period *= 2;
	}

	return total / max(normalisation, 1e-5f);
}

float mvPerlinFbm3(float3 p, uint seed, int basePeriod, uint octaves)
{
	float total = 0.0f;
	float amplitude = 1.0f;
	float normalisation = 0.0f;

	int period = basePeriod;

	for (uint o = 0; o < octaves; o++)
	{
		total += mvPerlinNoise3(p * float(period), seed + o * 0x85ebca6bu, period) * amplitude;
		normalisation += amplitude;

		amplitude *= 0.5f;
		period *= 2;
	}

	return total / max(normalisation, 1e-5f);
}

// Perlin remapped into the Worley billows.
//
// Perlin alone gives smooth blobs with no internal structure; Worley alone gives cells with
// no variation across them. Using the Worley field as the low end of a remap of Perlin
// keeps the billowed silhouette and fills it with detail, which is the shape cumulus has.
float mvPerlinWorley3(float3 p, uint seed, int period, uint octaves)
{
	const float perlin = mvPerlinFbm3(p, seed, period, octaves) * 0.5f + 0.5f;
	const float worley = mvWorleyFbm3(p, seed + 0x1b56c4e9u, period, 3);

	// Remap Perlin so its floor follows the Worley field: where Worley is high the whole
	// range survives, where it is low the value is pushed towards zero.
	//
	// The window has to start at worley - 1, not at 1 - worley. Anchoring it the other way
	// requires Perlin to beat the Worley field outright, which for two fields that both sit
	// near 0.5 means almost nothing survives -- the volume comes out nearly empty and the
	// sky with it.
	return saturate((perlin - (worley - 1.0f)) / max(2.0f - worley, 1e-5f));
}

#endif
