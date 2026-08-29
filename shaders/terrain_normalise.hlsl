// Rescales the height buffer so it spans exactly [0, 1].
//
// Separate from the pass that produced it because the extremes are not known until every
// texel has been evaluated, and a dispatch is the only barrier available between the two.

#define MV_CUSTOM_PUSH_CONSTANTS
#include "common.hlsli"
#include "terrain.hlsli"

#ifdef MV_TARGET_VULKAN
[[vk::push_constant]] TerrainConstants terrain;
#else
ConstantBuffer<TerrainConstants> terrain : register(b0, space9);
#endif

RWStructuredBuffer<float> heights : register(u0, space0);
StructuredBuffer<uint>    range   : register(t1, space0);

[numthreads(64, 1, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
	const uint count = terrain.fieldSize * terrain.fieldSize;

	if (id.x >= count)
		return;

	const float lowest = mvDecodeHeight(range[0]);
	const float highest = mvDecodeHeight(~range[1]);

	const float span = highest - lowest;

	// A constant field has no range to stretch; flattening it to the middle is the only
	// answer that does not divide by zero.
	heights[id.x] = (span > 1e-6f) ? saturate((heights[id.x] - lowest) / span) : 0.5f;
}
