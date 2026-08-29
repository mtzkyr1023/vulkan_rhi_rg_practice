// The shape volume: 128 voxels cubed, four channels.
//
//   r  Perlin-Worley, the base billowed silhouette
//   g  Worley at the base frequency
//   b  Worley at twice that
//   a  Worley at four times that
//
// The three Worley channels are what the raymarch erodes the base with. Keeping them apart
// rather than pre-combining them lets the erosion strength change with altitude and with
// how far the sample is from the viewer, which is most of what stops a cloud layer from
// looking like one repeated blob.

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

	// Voxel centres of the unit cube, which is the domain every period is expressed in.
	const float3 p = (float3(id) + 0.5f) / float(volume.size);

	const float shape = mvPerlinWorley3(p, volume.seed, 4, 4);

	const float worleyLow = mvWorleyFbm3(p, volume.seed + 0x2545f491u, 4, 3);
	const float worleyMid = mvWorleyFbm3(p, volume.seed + 0x68e31da4u, 8, 3);
	const float worleyHigh = mvWorleyFbm3(p, volume.seed + 0x51ed2701u, 16, 3);

	target[id] = float4(shape, worleyLow, worleyMid, worleyHigh);
}
