
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
#define MV_DEBUG_CASCADE      10

// Fixed sampler presets, matching the order in material_system.cpp.
#define MV_SAMPLER_LINEAR_REPEAT  0
#define MV_SAMPLER_LINEAR_CLAMP   1
#define MV_SAMPLER_NEAREST_REPEAT 2
#define MV_SAMPLER_NEAREST_CLAMP  3

// Must match kMaxCascades in cascaded_shadow_map.h.
#define MV_SHADOW_CASCADES 4

// space0 == descriptor set 0: per-frame scene data.
cbuffer SceneConstants : register(b0, space0)
{
	row_major float4x4 viewProj;
	float3 cameraPosition;   uint  debugMode;
	float3 lightDirection;   uint  vtEnabled;
	float3 lightColor;       float ambientIntensity;

	// Cascade selection is done on view-space depth, which needs the view direction as
	// well as the eye position.
	float3 cameraForward;    uint  shadowEnabled;

	row_major float4x4 cascadeViewProj[MV_SHADOW_CASCADES];

	// The view depth each cascade stops at.
	float4 cascadeSplits;

	// World-space size of one shadow texel, per cascade, for the normal offset bias.
	float4 cascadeTexelSize;

	float shadowDepthBias;
	float shadowNormalBias;
	// Radius of the PCF kernel in shadow texels. 0 is a single hardware-filtered tap.
	int   shadowPcfRadius;
	float iblIntensity;

	// The skybox reconstructs its view rays from the camera basis, which needs the aspect
	// ratio the projection was built with.
	float2 viewportSize;

	// This frame's sub-pixel camera offset, in UV. Velocity is measured between unjittered
	// positions, or it would carry the jitter as if it were motion and the temporal pass
	// would chase its own tail.
	float2 jitterUv;

	// The fraction of a cascade over which it fades into the next one. Without it the
	// boundary is a visible line, because the two cascades have different resolutions and
	// therefore different amounts of filtering.
	float shadowCascadeBlend;

	// The coarsest mip currently made resident, which the streaming feedback has to add
	// back: a mip-range view reports its own level 0, not the texture's.
	float forcedBaseMip;

	// How elongated a footprint the virtual texture path will refine for. 1 is the
	// isotropic choice; higher trades resident pages for sharpness at grazing angles.
	float vtMaxAnisotropy;

	// How much of the direct sun the cloud layer is allowed to take away. Zero is off, and
	// off is also what a frame with no cloud renderer gets, so the sampler below reads a
	// texture that exists but never changes anything.
	float cloudShadowStrength;

	// Where the cloud shadow map sits: a light-space frame, like any shadow map's. The map
	// lives on a plane perpendicular to the sun; a receiver finds its texel by projecting
	// its offset from the origin onto the two axes. It follows the camera and the sun, so
	// all of it changes every frame.
	float3 cloudShadowOrigin;
	float  cloudShadowInvExtent;

	float3 cloudShadowRight;
	float  _scenePad0;

	float3 cloudShadowUp;
	float  _scenePad1;

	// Nine RGB coefficients of the environment's diffuse irradiance. Padded to float4
	// because a cbuffer array element is always sixteen bytes wide.
	float4 shCoefficients[9];

	// The previous frame's unjittered view-projection: what turns a world position into
	// where it was on screen. computeVelocity is its only user.
	row_major float4x4 prevViewProj;
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

// The cascade atlas, and a sampler that compares rather than returns. SampleCmp against
// this averages four depth tests in one tap, which is the cheapest useful filtering a
// shadow map can have.
Texture2D                shadowAtlas   : register(t3, space0);
SamplerComparisonState   shadowSampler : register(s4, space0);

// The sky, with roughness-prefiltered mips. Mip 0 is the sky itself, which is what the
// skybox pass draws; the lower levels are what a rough surface reflects.
TextureCube              environmentMap : register(t5, space0);

// Transmittance of the cloud layer along the sun ray, indexed by world XZ. One channel's
// worth of information in four, because the backend has no single-channel storage format
// and a shadow needs eight bits far more than it needs a fourth channel.
Texture2D                cloudShadowMap : register(t6, space0);

// How far this surface moved across the screen since the last frame, in UV.
//
// Both ends are unjittered: the raster position carries this frame's sub-pixel offset and
// has it removed, and the previous matrix never had one. What is left is the motion of the
// surface itself, which is what a temporal pass needs to follow it.
float2 computeVelocity(float3 worldPosition, float2 rasterUv)
{
	float4 previousClip = mul(float4(worldPosition, 1.0f), prevViewProj);
	float2 previousUv = (previousClip.xy / previousClip.w) * float2(0.5f, -0.5f) + 0.5f;

	return (rasterUv - jitterUv) - previousUv;
}

// The same for a direction rather than a point. A w of zero drops the translation, leaving
// only the rotation, which is all the sky can move by.
float2 computeDirectionVelocity(float3 direction, float2 rasterUv)
{
	float4 previousClip = mul(float4(direction, 0.0f), prevViewProj);
	float2 previousUv = (previousClip.xy / previousClip.w) * float2(0.5f, -0.5f) + 0.5f;

	return (rasterUv - jitterUv) - previousUv;
}

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

// How much of the direct sun survives the cloud layer above this point.
//
// One tap, because the shadow map already holds the answer: the transmittance of the whole
// layer along the sun ray, baked from the ground up. Sampled with the linear-clamp preset
// from set 1, so outside the map -- which the camera-following extent makes rare -- the
// edge value extends rather than tiling a wrong one in.
//
// Only the direct term is occluded. The ambient and IBL terms stand in for light arriving
// from the whole sky, and the sky is where the cloud already put it.
float cloudShadowFactor(float3 worldPosition)
{
	if (cloudShadowStrength <= 0.0f)
		return 1.0f;

	// A shadow-map lookup: project the receiver into the map's light-space frame. Every
	// point on one sun ray lands on one texel, which is what makes the answer right for a
	// peak, for the ground under it and for the air between -- the old world-XZ lookup
	// slid the shadow sideways by height / tan(elevation) for anything above the ground.
	const float3 d = worldPosition - cloudShadowOrigin;

	const float2 uv = float2(dot(d, cloudShadowRight), dot(d, cloudShadowUp))
		* cloudShadowInvExtent + 0.5f;

	const float transmittance = cloudShadowMap.Sample(samplers[MV_SAMPLER_LINEAR_CLAMP], uv).r;

	return lerp(1.0f, transmittance, cloudShadowStrength);
}

struct DrawConstants
{
	uint drawIndex;
	uint materialIndex;

	// Only the shadow pass reads this: it draws the scene once per cascade and picks the
	// matching projection out of the scene constants.
	uint cascadeIndex;
};

// Vulkan has a dedicated push constant block; D3D12 root constants are still a cbuffer
// and have to live in a register space that no bind group uses.
//
// A shader that puts something else in the push constant block defines
// MV_CUSTOM_PUSH_CONSTANTS: there is only one, and the post-process chain uses it for
// effect parameters rather than for a draw.
#ifndef MV_CUSTOM_PUSH_CONSTANTS
#ifdef MV_TARGET_VULKAN
[[vk::push_constant]] DrawConstants drawConstants;
#else
ConstantBuffer<DrawConstants> drawConstants : register(b0, space9);
#endif
#endif

#endif
