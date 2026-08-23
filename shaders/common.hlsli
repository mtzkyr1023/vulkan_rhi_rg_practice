
// Shared bindings and data layouts. Everything here must agree with material_system.h.

#ifndef _MV_COMMON_HLSLI_
#define _MV_COMMON_HLSLI_

struct GpuMaterial
{
	float4 baseColorFactor;

	float  metallicFactor;
	float  roughnessFactor;
	float  normalScale;
	float  occlusionStrength;

	float3 emissiveFactor;
	// Zero for opaque and blended materials, so clipping against it is a no-op for them
	// and masked materials need no separate shader.
	float  alphaCutoff;

	uint   baseColorTexture;
	uint   metallicRoughnessTexture;
	uint   normalTexture;
	uint   occlusionTexture;

	uint   samplerIndex;
	uint   emissiveTexture;
	uint2  _pad;
};

struct ModelVertex
{
	float3 position;
	float3 normal;
	float2 uv;
};

// What the visibility buffer resolve pass should draw instead of the shaded result.
#define MV_DEBUG_OFF          0
#define MV_DEBUG_DRAW_ID      1
#define MV_DEBUG_PRIMITIVE_ID 2
#define MV_DEBUG_MATERIAL_ID  3
#define MV_DEBUG_BARYCENTRIC  4
#define MV_DEBUG_UV           5
#define MV_DEBUG_NORMAL       6
#define MV_DEBUG_MIP_LEVEL    7
#define MV_DEBUG_VT_PAGE      8
#define MV_DEBUG_VT_LEVEL     9

// Fixed sampler presets, matching the order in material_system.cpp.
#define MV_SAMPLER_LINEAR_REPEAT  0
#define MV_SAMPLER_LINEAR_CLAMP   1
#define MV_SAMPLER_NEAREST_REPEAT 2
#define MV_SAMPLER_NEAREST_CLAMP  3

// space0 == descriptor set 0: per-frame scene data.
cbuffer SceneConstants : register(b0, space0)
{
	row_major float4x4 viewProj;
	float3 cameraPosition;   uint  debugMode;
	float3 lightDirection;   uint  vtEnabled;
	float3 lightColor;       float ambientIntensity;
};

// One virtual texture, indexed by the same number as its bindless texture. Must match
// GpuVirtualTextureInfo in virtual_texture.h.
struct VirtualTextureInfo
{
	uint pageTableOffset;
	uint pagesX;
	uint pagesY;
	// Zero for a texture that was never virtualised.
	uint levelCount;
};

// Also set 0, because every pass that samples a material needs them and set 1 cannot hold
// them: the unbounded texture array there takes every t register above its base.
// t1 and t2 rather than t0: a binding index maps straight onto the register number, and
// binding 0 is already taken by the scene constant buffer.
StructuredBuffer<VirtualTextureInfo> vtInfos     : register(t1, space0);
StructuredBuffer<uint>               vtPageTable : register(t2, space0);

// Spreads consecutive ids into visually distinct colours.
float3 debugIdColor(uint id)
{
	uint h = (id + 1) * 2654435761u;

	return float3(
		float((h >>  0) & 255u),
		float((h >>  8) & 255u),
		float((h >> 16) & 255u)) / 255.0f;
}

// space1 == descriptor set 1: everything global.
//
// The texture array starts at t1 rather than t0 because an unbounded D3D12 range takes
// every register from its base upwards, which would otherwise swallow the material
// buffer. The sampler array is at s2 so its Vulkan binding number does not collide with
// the two above it.
StructuredBuffer<GpuMaterial> materials : register(t0, space1);
Texture2D                     textures[] : register(t1, space1);
SamplerState                  samplers[4] : register(s2, space1);

struct DrawConstants
{
	uint drawIndex;
	uint materialIndex;
};

// Vulkan has a dedicated push constant block; D3D12 root constants are still a cbuffer
// and have to live in a register space that no bind group uses.
#ifdef MV_TARGET_VULKAN
[[vk::push_constant]] DrawConstants drawConstants;
#else
ConstantBuffer<DrawConstants> drawConstants : register(b0, space9);
#endif

#endif
