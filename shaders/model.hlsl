
// Forward path: shades directly at raster time, using the hardware's quad derivatives.
// The visibility buffer path in vb.hlsl / vb_shade.hlsl produces the same image from the
// same PBR code in pbr.hlsli.

#include "pbr.hlsli"

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
	// Node transforms are baked into the vertices at load time, so the incoming position
	// is already in world space.
	VSOutput output;
	output.position = mul(float4(input.position, 1.0f), viewProj);
	output.worldPosition = input.position;
	output.normal = input.normal;
	output.uv = input.uv;

	return output;
}

float4 PSMain(VSOutput input) : SV_TARGET
{
	SurfaceInput surface;
	surface.material = materials[drawConstants.materialIndex];
	surface.uv = input.uv;
	surface.uvDdx = ddx(input.uv);
	surface.uvDdy = ddy(input.uv);
	surface.worldPosition = input.worldPosition;
	surface.geometricNormal = input.normal;
	surface.positionDdx = ddx(input.worldPosition);
	surface.positionDdy = ddy(input.worldPosition);

	float4 baseColor = sampleBaseColor(surface);

	// Alpha masking, before any of the lighting work is done.
	clip(baseColor.a - surface.material.alphaCutoff);

	return shadeSurface(surface, baseColor);
}
