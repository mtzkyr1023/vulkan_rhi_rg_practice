// Resets the grass indirect draw arguments: one thread, four writes.
//
// A buffer copy would do the same job, but the D3D12 copy path fences its source with a
// UAV barrier, and a host-visible template buffer cannot carry the UAV flag that needs.
// A dispatch this small costs nothing and keeps every touch of the arguments buffer in
// one state -- shader write -- until the cull is done with it.

#define MV_CUSTOM_PUSH_CONSTANTS
#include "common.hlsli"

// Same set layout as the cull, same slot; the other two bindings go unused here.
RWStructuredBuffer<uint> drawArgs : register(u2, space0);

[numthreads(1, 1, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
	// vertexCount, instanceCount, firstVertex, firstInstance. Fifteen must match
	// kVertsPerBlade in grass_field.cpp and the canonical blade in grass.hlsl.
	drawArgs[0] = 15;
	drawArgs[1] = 0;
	drawArgs[2] = 0;
	drawArgs[3] = 0;
}
