
// Visibility buffer pass: rasterizes geometry and writes nothing but an identifier per
// pixel. All the material work is deferred to vb_shade.hlsl, so this runs one tiny pixel
// shader over the scene instead of a full BRDF per overdrawn fragment.
//
// Alpha-masked materials still have to sample their base colour here, because a pixel
// they clip must not claim the visibility buffer slot.

#include "common.hlsli"

struct VSInput
{
	float3 position : POSITION;
	float3 normal   : NORMAL;
	float2 uv       : TEXCOORD0;
};

struct VSOutput
{
	float4 position : SV_POSITION;
	float2 uv       : TEXCOORD0;
};

VSOutput VSMain(VSInput input)
{
	VSOutput output;
	output.position = mul(float4(input.position, 1.0f), viewProj);
	output.uv = input.uv;

	return output;
}

// R32G32_UINT. drawIndex is stored biased by one so that a cleared pixel, which is zero,
// reads as "no geometry" without needing an integer clear value.
uint2 PSMain(VSOutput input, uint primitiveID : SV_PrimitiveID) : SV_TARGET
{
	GpuMaterial material = materials[drawConstants.materialIndex];

	if (material.alphaCutoff > 0.0f)
	{
		float alpha = textures[NonUniformResourceIndex(material.baseColorTexture)]
			.Sample(samplers[material.samplerIndex], input.uv).a * material.baseColorFactor.a;

		clip(alpha - material.alphaCutoff);
	}

	return uint2(drawConstants.drawIndex + 1, primitiveID);
}
