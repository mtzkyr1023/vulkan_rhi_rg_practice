// Evaluates the noise field into the height buffer, and reduces its range while doing it.
//
// The range matters: ridged noise rarely spans anything like [0, 1] on its own, and the
// terrain's height scale is defined against a full-range heightmap. The CPU version found
// the extremes with a second pass over the array; here every thread folds its own value
// into two atomics, which costs one instruction per texel instead of a second traversal.

#define MV_CUSTOM_PUSH_CONSTANTS
#include "common.hlsli"
#include "terrain.hlsli"

#ifdef MV_TARGET_VULKAN
[[vk::push_constant]] TerrainConstants terrain;
#else
ConstantBuffer<TerrainConstants> terrain : register(b0, space9);
#endif

RWStructuredBuffer<float> heights : register(u0, space0);

// Two slots: the minimum, and the complement of the maximum. Storing the maximum inverted
// means both are found with InterlockedMin, so one cleared-to-0xFFFFFFFF buffer serves
// both and there is no second fill value to arrange.
RWStructuredBuffer<uint> range : register(u1, space0);

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
	if (id.x >= terrain.fieldSize || id.y >= terrain.fieldSize)
		return;

	const float u = (float(id.x) + 0.5f) / float(terrain.fieldSize);
	const float v = (float(id.y) + 0.5f) / float(terrain.fieldSize);

	const float h = mvNoiseSample(terrain.noise, u, v);

	heights[id.y * terrain.fieldSize + id.x] = h;

	const uint encoded = mvEncodeHeight(h);

	uint previous;
	InterlockedMin(range[0], encoded, previous);
	InterlockedMin(range[1], ~encoded, previous);
}
