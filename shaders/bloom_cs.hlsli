#ifndef _MV_BLOOM_CS_HLSLI_
#define _MV_BLOOM_CS_HLSLI_

// Shared bindings for the compute form of the bloom pyramid.
//
// Deliberately not post.hlsli. That header declares the scene and bindless sets, which a
// pyramid pass never touches, and D3D12 rejects a compute root signature whose parameters
// name anything other than SHADER_VISIBILITY_ALL -- so a compute pass cannot share a
// pipeline layout with the graphics chain even if it wanted to. Three bindings of its own
// is both the smaller and the more honest arrangement.

// Must match BloomConstants in post/effects.cpp.
struct BloomComputeConstants
{
	float threshold;
	float knee;
	float intensity;
	float texelX;

	float texelY;
	uint targetWidth;
	uint targetHeight;
	uint _bloomPad;
};

#ifdef MV_TARGET_VULKAN
[[vk::push_constant]] BloomComputeConstants bloom;
#else
ConstantBuffer<BloomComputeConstants> bloom : register(b0, space9);
#endif

// The level being read, and for the upward pass the level being added into.
Texture2D<float4> bloomSource : register(t0, space0);
Texture2D<float4> bloomAccum  : register(t1, space0);

SamplerState bloomSampler : register(s2, space0);

// rgba16f, matching the chain format. SPIR-V decorates a storage image with the format it
// is accessed as and DXC assumes rgba32f otherwise, which makes every access undefined.
#ifdef MV_TARGET_VULKAN
[[vk::image_format("rgba16f")]] RWTexture2D<float4> bloomTarget : register(u3, space0);
#else
RWTexture2D<float4> bloomTarget : register(u3, space0);
#endif

// The uv of this thread's texel centre in the level being written, or a negative marker if
// the thread is outside it.
bool bloomThreadUv(uint3 id, out float2 uv)
{
	uv = 0.0f.xx;

	if (id.x >= bloom.targetWidth || id.y >= bloom.targetHeight)
		return false;

	uv = (float2(id.xy) + 0.5f) / float2(bloom.targetWidth, bloom.targetHeight);

	return true;
}

#endif
