// Halves one mip level into the next.
//
// This replaces a CPU resize that ran once per level per texture, with the source pixels
// still in system memory. That was the only portable option before the RHI had compute:
// Vulkan has vkCmdBlitImage but D3D12 has no blit at all, so the one shared implementation
// had to be the one neither API was involved in.
//
// A box filter over four texels, which is what the CPU resize was also doing at a 2:1
// ratio. The taps are explicit loads rather than one filtered sample because the sRGB case
// has to decode each tap before averaging, and using the same tap pattern for both keeps
// the two paths comparable.

#define MV_CUSTOM_PUSH_CONSTANTS
#include "common.hlsli"

struct MipGenConstants
{
	// Size of the level being written, and of the one being read. The second is not simply
	// twice the first: a level of odd size halves to a floor, so the last row and column of
	// the source have to be found rather than assumed.
	uint2 outputSize;
	uint2 inputSize;

	// 1 when the texture is sRGB encoded. A UAV cannot carry an sRGB format on either API,
	// so the write goes through the linear twin of the format and the encode happens here.
	uint srgb;

	uint3 _pad;
};

#ifdef MV_TARGET_VULKAN
[[vk::push_constant]] MipGenConstants mipGen;
#else
ConstantBuffer<MipGenConstants> mipGen : register(b0, space9);
#endif

// The source is read through a storage image rather than a sampled one so the whole chain
// can stay in one layout while it is being built. Vulkan needs a storage image to be in
// GENERAL, and the RHI transitions a texture as a whole, so a level read as SAMPLED while
// its neighbour is written would be a layout the descriptor disagrees with.
// SPIR-V decorates a storage image with the format it is accessed as, and DXC assumes
// rgba32f for an RWTexture2D<float4> unless told otherwise. A mismatch against the view's
// format makes every load and store undefined -- not just the texel touched, the whole
// image. DXIL has no equivalent concept and warns if handed the attribute, hence the guard.
#ifdef MV_TARGET_VULKAN
[[vk::image_format("rgba8")]] RWTexture2D<float4> sourceLevel : register(u0, space0);
[[vk::image_format("rgba8")]] RWTexture2D<float4> targetLevel : register(u1, space0);
#else
RWTexture2D<float4> sourceLevel : register(u0, space0);
RWTexture2D<float4> targetLevel : register(u1, space0);
#endif

float3 srgbToLinear(float3 c)
{
	const float3 low = c / 12.92f;
	const float3 high = pow(max(c + 0.055f, 0.0f) / 1.055f, 2.4f);

	return lerp(high, low, step(c, 0.04045f.xxx));
}

float3 linearToSrgb(float3 c)
{
	const float3 low = c * 12.92f;
	const float3 high = 1.055f * pow(max(c, 0.0f), 1.0f / 2.4f) - 0.055f;

	return lerp(high, low, step(c, 0.0031308f.xxx));
}

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
	if (id.x >= mipGen.outputSize.x || id.y >= mipGen.outputSize.y)
		return;

	const int2 maxCoord = int2(mipGen.inputSize) - 1;
	const int2 base = int2(id.xy) * 2;

	float3 sum = 0.0f.xxx;
	float alpha = 0.0f;

	[unroll]
	for (int dy = 0; dy < 2; dy++)
	{
		[unroll]
		for (int dx = 0; dx < 2; dx++)
		{
			// Clamped: a 1-wide or 1-tall level has no second tap to take, and an
			// out-of-bounds Load returns zero rather than the edge, which would darken
			// the whole last row.
			const int2 coord = min(base + int2(dx, dy), maxCoord);

			const float4 s = sourceLevel[coord];

			// Averaging sRGB-encoded values darkens the result, because the encoding is
			// not linear in light. Nothing else can undo it: the source is read through
			// the same non-sRGB view the target is written through, precisely so that
			// both ends agree about what the bits mean.
			sum += (mipGen.srgb != 0) ? srgbToLinear(s.rgb) : s.rgb;
			alpha += s.a;
		}
	}

	sum *= 0.25f;
	alpha *= 0.25f;

	if (mipGen.srgb != 0)
		sum = linearToSrgb(sum);

	targetLevel[id.xy] = float4(sum, alpha);
}
