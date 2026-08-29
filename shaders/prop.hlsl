// Props: loaded glTF models placed in the world by entities, drawn forward with a model
// matrix -- the one thing no other geometry pass here has.
//
// Everything else the renderer draws is baked into world space at load or build time,
// because the visibility buffer resolves triangles out of one global vertex array with no
// idea which draw produced them, and a transform it cannot recover is a transform that
// cannot exist. An entity is the opposite: the same mesh standing in many places, moved by
// gameplay whenever it likes. So props go around the visibility buffer entirely -- a
// forward draw after the resolve, into the same colour, velocity and depth targets, where
// the clouds, the water and the fog treat them as scenery like everything else.
//
// The shading is model.hlsl's: same pbr.hlsli, same material buffer, same bindless
// textures. Only the vertex half differs, by one matrix.

#define MV_CUSTOM_PUSH_CONSTANTS
#include "pbr.hlsli"

// Must match PropGpuConstants in prop_renderer.cpp.
struct PropConstants
{
	row_major float4x4 model;

	uint materialIndex;
	float3 _propPad;
};

#ifdef MV_TARGET_VULKAN
[[vk::push_constant]] PropConstants prop;
#else
ConstantBuffer<PropConstants> prop : register(b0, space9);
#endif

struct VSInput
{
	float3 position : POSITION;
	float3 normal   : NORMAL;
	float2 uv       : TEXCOORD0;
};

struct VSOutput
{
	float4 position      : SV_POSITION;
	float3 worldPosition : POSITION0;
	float3 normal        : NORMAL;
	float2 uv            : TEXCOORD0;
};

VSOutput VSMain(VSInput input)
{
	VSOutput output;

	const float4 world = mul(float4(input.position, 1.0f), prop.model);

	output.position = mul(world, viewProj);
	output.worldPosition = world.xyz;

	// The rotation part only. Entities scale uniformly, so the inverse transpose this
	// stands in for is the same matrix up to the normalize.
	output.normal = normalize(mul(input.normal, (float3x3)prop.model));

	output.uv = input.uv;

	return output;
}

struct PropPSOutput
{
	float4 color    : SV_TARGET0;
	float2 velocity : SV_TARGET1;
};

PropPSOutput PSMain(VSOutput input)
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

	PropPSOutput output;
	output.color = shadeSurface(surface, baseColor);

	const float2 rasterUv = input.position.xy / viewportSize;
	output.velocity = computeVelocity(input.worldPosition, rasterUv);

	return output;
}
