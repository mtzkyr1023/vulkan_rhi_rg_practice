// Writes a constant 32-bit value across a range of a structured buffer.
//
// The streaming feedback buffer needs this. It is only ever InterlockedMin'd into, so it
// has to start every frame at the maximum or the values are cumulative -- once a texture
// has been seen at mip 3 anywhere, it reads as mip 3 forever, and the readback describes
// the whole history of the camera rather than what is on screen now. Uploading a cleared
// copy per frame would be a 16KB transfer and a stall; a dispatch is neither.

#define MV_CUSTOM_PUSH_CONSTANTS
#include "common.hlsli"

struct BufferFillConstants
{
	uint count;
	uint value;
	uint2 _pad;
};

#ifdef MV_TARGET_VULKAN
[[vk::push_constant]] BufferFillConstants bufferFill;
#else
ConstantBuffer<BufferFillConstants> bufferFill : register(b0, space9);
#endif

RWStructuredBuffer<uint> target : register(u0, space0);

[numthreads(64, 1, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
	if (id.x >= bufferFill.count)
		return;

	target[id.x] = bufferFill.value;
}
