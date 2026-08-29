// The detail volume: 32 voxels cubed, three channels of Worley at rising frequency.
//
// Applied at the edges of what the shape volume produced, where it turns a smooth boundary
// into the wispy one clouds actually have. Only near the edges: eroding the interior as
// well would just make the cloud thinner everywhere, which reads as fog.

#define MV_CUSTOM_PUSH_CONSTANTS
#include "common.hlsli"
#include "noise3.hlsli"

struct VolumeConstants
{
	uint size;
	uint seed;
	uint2 _volumePad;
};

#ifdef MV_TARGET_VULKAN
[[vk::push_constant]] VolumeConstants volume;
#else
ConstantBuffer<VolumeConstants> volume : register(b0, space9);
#endif

#ifdef MV_TARGET_VULKAN
[[vk::image_format("rgba8")]] RWTexture3D<float4> target : register(u0, space0);
#else
RWTexture3D<float4> target : register(u0, space0);
#endif

[numthreads(4, 4, 4)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
	if (any(id >= volume.size.xxx))
		return;

	const float3 p = (float3(id) + 0.5f) / float(volume.size);

	const float low = mvWorleyFbm3(p, volume.seed + 0x9e3779b9u, 2, 3);
	const float mid = mvWorleyFbm3(p, volume.seed + 0x85ebca6bu, 4, 3);
	const float high = mvWorleyFbm3(p, volume.seed + 0xc2b2ae35u, 8, 3);

	target[id] = float4(low, mid, high, 1.0f);
}
