#ifndef _MV_TERRAIN_HLSLI_
#define _MV_TERRAIN_HLSLI_

#include "noise.hlsli"

// Must match TerrainGpuConstants in compute/terrain_builder.cpp.
//
// One struct for all four terrain passes rather than one each: they are pushed at the same
// point in the build and every field is derived from the same TerrainDesc, so splitting
// them would only mean keeping four things in step instead of one.
struct TerrainConstants
{
	NoiseParams noise;

	// Side of the heightmap, in samples. Square.
	uint fieldSize;

	// Vertices per side of the mesh.
	uint resolution;

	// Side of the baked maps, in texels.
	uint textureSize;

	float worldSize;

	float heightScale;
	float rockHeight;
	float snowHeight;
	float rockSlope;

	float waterHeight;
	uint3 _terrainPad;
};

// Bilinear lookup into the height buffer, clamped at the edges. The CPU twin is
// Heightmap::sample; the half-texel offset is what keeps the two agreeing about where a
// sample sits, and without it the baked normals slide half a texel off the geometry.
float mvSampleHeight(StructuredBuffer<float> heights, uint fieldSize, float u, float v)
{
	const float x = saturate(u) * float(fieldSize) - 0.5f;
	const float y = saturate(v) * float(fieldSize) - 0.5f;

	const float fx = floor(x);
	const float fy = floor(y);

	const int x0 = int(fx);
	const int y0 = int(fy);

	const float tx = x - fx;
	const float ty = y - fy;

	const int last = int(fieldSize) - 1;

	const uint cx0 = uint(clamp(x0, 0, last));
	const uint cy0 = uint(clamp(y0, 0, last));
	const uint cx1 = uint(clamp(x0 + 1, 0, last));
	const uint cy1 = uint(clamp(y0 + 1, 0, last));

	const float h00 = heights[cy0 * fieldSize + cx0];
	const float h10 = heights[cy0 * fieldSize + cx1];
	const float h01 = heights[cy1 * fieldSize + cx0];
	const float h11 = heights[cy1 * fieldSize + cx1];

	return lerp(lerp(h00, h10, tx), lerp(h01, h11, tx), ty);
}

// Floats are compared as integers to get an atomic min out of hardware that only has one
// for uints. The bit patterns of non-negative IEEE floats order the same way as the values,
// so the bias is there purely to keep everything non-negative -- the fractal output is
// normalised by its amplitude sum and cannot reach it, but clamping costs nothing.
#define MV_HEIGHT_BIAS 8.0f

uint mvEncodeHeight(float h)
{
	return asuint(max(h + MV_HEIGHT_BIAS, 0.0f));
}

float mvDecodeHeight(uint u)
{
	return asfloat(u) - MV_HEIGHT_BIAS;
}

#endif
