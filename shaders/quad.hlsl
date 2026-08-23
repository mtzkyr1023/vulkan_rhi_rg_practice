// Exercises the vertex/index buffer path and one uniform buffer bound as set 0.

struct VSInput
{
	float3 position : POSITION;
	float3 color    : COLOR0;
};

struct VSOutput
{
	float4 position : SV_POSITION;
	float3 color    : COLOR0;
};

// space0 == Vulkan descriptor set 0. row_major is stated explicitly: HLSL constant
// buffers default to column-major, and the CPU side uploads row-major data.
cbuffer SceneConstants : register(b0, space0)
{
	row_major float4x4 viewProj;
};

VSOutput VSMain(VSInput input)
{
	VSOutput output;
	output.position = mul(float4(input.position, 1.0f), viewProj);
	output.color = input.color;

	return output;
}

float4 PSMain(VSOutput input) : SV_TARGET
{
	return float4(input.color, 1.0f);
}
