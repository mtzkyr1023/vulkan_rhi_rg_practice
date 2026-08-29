// Skinned props: the prop pass with a skeleton. Vertices arrive in mesh space
// with four joint indices and weights; the palette (inverse bind * animated
// global, built on the CPU) deforms them, then the model matrix places the whole
// creature in the world. Shading is identical to prop.hlsl -- same pbr.hlsli,
// same material buffer, same bindless textures.
//
// The palette is bound as rows (float4 per element) rather than float4x4: a
// structured matrix element invites the two compilers to disagree about layout,
// and four explicit rows cannot be misread.

#define MV_CUSTOM_PUSH_CONSTANTS
#include "pbr.hlsli"

// Must match SkinnedGpuConstants in skinned_prop_renderer.cpp.
struct SkinnedConstants
{
	row_major float4x4 model;

	uint materialIndex;
	float3 _skinnedPad;
};

#ifdef MV_TARGET_VULKAN
[[vk::push_constant]] SkinnedConstants prop;
#else
ConstantBuffer<SkinnedConstants> prop : register(b0, space9);
#endif

// Joint matrices as rows: joint j owns elements [j*4, j*4+4).
StructuredBuffer<float4> paletteRows : register(t0, space2);

struct VSInput
{
	float3 position : POSITION;
	float3 normal   : NORMAL;
	float2 uv       : TEXCOORD0;
	float4 joints   : JOINTS0;
	float4 weights  : WEIGHTS0;
};

struct VSOutput
{
	float4 position      : SV_POSITION;
	float3 worldPosition : POSITION0;
	float3 normal        : NORMAL;
	float2 uv            : TEXCOORD0;
};

float4x4 jointMatrix(uint joint)
{
	return float4x4(
		paletteRows[joint * 4 + 0],
		paletteRows[joint * 4 + 1],
		paletteRows[joint * 4 + 2],
		paletteRows[joint * 4 + 3]);
}

VSOutput VSMain(VSInput input)
{
	VSOutput output;

	// The linear blend: four matrices, four weights, summed before the multiply
	// so the vertex pays one transform, not four.
	const float4x4 skin =
		jointMatrix((uint)input.joints.x) * input.weights.x +
		jointMatrix((uint)input.joints.y) * input.weights.y +
		jointMatrix((uint)input.joints.z) * input.weights.z +
		jointMatrix((uint)input.joints.w) * input.weights.w;

	const float4 skinned = mul(float4(input.position, 1.0f), skin);
	const float4 world = mul(skinned, prop.model);

	output.position = mul(world, viewProj);
	output.worldPosition = world.xyz;

	// Rotation parts only; both matrices scale uniformly enough that the
	// normalize stands in for the inverse transpose.
	const float3 skinnedNormal = mul(input.normal, (float3x3)skin);
	output.normal = normalize(mul(skinnedNormal, (float3x3)prop.model));

	output.uv = input.uv;

	return output;
}

struct SkinnedPSOutput
{
	float4 color    : SV_TARGET0;
	float2 velocity : SV_TARGET1;
};

SkinnedPSOutput PSMain(VSOutput input)
{
	SurfaceInput surface;
	surface.material = materials[prop.materialIndex];
	surface.uv = input.uv;
	surface.uvDdx = ddx(input.uv);
	surface.uvDdy = ddy(input.uv);
	surface.worldPosition = input.worldPosition;
	surface.geometricNormal = input.normal;
	surface.positionDdx = ddx(input.worldPosition);
	surface.positionDdy = ddy(input.worldPosition);

	float4 baseColor = sampleBaseColor(surface);

	clip(baseColor.a - surface.material.alphaCutoff);

	SkinnedPSOutput output;
	output.color = shadeSurface(surface, baseColor);

	const float2 rasterUv = input.position.xy / viewportSize;
	output.velocity = computeVelocity(input.worldPosition, rasterUv);

	return output;
}
