// HUD: rectangles and pixel text over the finished frame, under the debug UI.
// Positions arrive in pixels from the top left and become NDC here; everything
// samples one small atlas -- glyphs where their bits are, a white slot for fills.

// Must match HudGpuConstants in hud_renderer.cpp.
struct HudConstants
{
	float2 screenSize;
	float2 pad;
};

#ifdef MV_TARGET_VULKAN
[[vk::push_constant]] HudConstants hud;
#else
ConstantBuffer<HudConstants> hud : register(b0, space9);
#endif

Texture2D    fontAtlas   : register(t0, space0);
SamplerState atlasSampler : register(s1, space0);

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

VSOutput VSMain(VSInput input)
{
	VSOutput output;

	const float2 ndc = input.position / hud.screenSize * 2.0f - 1.0f;

	output.position = float4(ndc.x, -ndc.y, 0.0f, 1.0f);
	output.uv = input.uv;
	output.color = input.color;

	return output;
}

float4 PSMain(VSOutput input) : SV_Target0
{
	return input.color * fontAtlas.Sample(atlasSampler, input.uv);
}
