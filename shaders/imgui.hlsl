// Renderer for Dear ImGui draw lists. Vertex layout matches ImDrawVert exactly:
// float2 pos, float2 uv, packed RGBA8 colour.

struct VSInput
{
	float2 position : POSITION;
	float2 uv       : TEXCOORD0;
	float4 color    : COLOR0;
};

struct VSOutput
{
	float4 position : SV_POSITION;
	float2 uv       : TEXCOORD0;
	float4 color    : COLOR0;
};

cbuffer ImGuiConstants : register(b0, space0)
{
	row_major float4x4 projection;
};

// Separate texture and sampler, matching the RHI's eSampledImage / eSampler split
// (Vulkan SAMPLED_IMAGE + SAMPLER rather than a combined sampler).
Texture2D    fontTexture : register(t1, space0);
SamplerState fontSampler : register(s2, space0);

VSOutput VSMain(VSInput input)
{
	VSOutput output;
	output.position = mul(float4(input.position, 0.0f, 1.0f), projection);
	output.uv = input.uv;
	output.color = input.color;

	return output;
}

float4 PSMain(VSOutput input) : SV_TARGET
{
	return input.color * fontTexture.Sample(fontSampler, input.uv);
}
