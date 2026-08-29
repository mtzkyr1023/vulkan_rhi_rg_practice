
// Shadow cascade pass: rasterizes geometry into one cascade of the depth atlas and writes
// nothing else. The cascade to render is passed as a push constant so all four share one
// pipeline and one set of bindings, with only the viewport and that index changing.
//
// Alpha-masked materials still have to sample their base colour, or every leaf would cast
// the shadow of its full quad.

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
	output.position = mul(float4(input.position, 1.0f), cascadeViewProj[drawConstants.cascadeIndex]);
	output.uv = input.uv;

	return output;
}

// No render target: the depth write is the entire output. The shader still exists because
// a masked material has to be able to reject a fragment before that write happens.
void PSMain(VSOutput input)
{
	GpuMaterial material = materials[drawConstants.materialIndex];

	if (material.alphaCutoff > 0.0f)
	{
		float alpha = textures[NonUniformResourceIndex(material.baseColorTexture)]
			.Sample(samplers[material.samplerIndex], input.uv).a * material.baseColorFactor.a;

		clip(alpha - material.alphaCutoff);
	}
}
