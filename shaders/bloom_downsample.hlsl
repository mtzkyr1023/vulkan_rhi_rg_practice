// One step down the pyramid.
//
// Thirteen taps in the pattern from Jimenez's SIGGRAPH 2014 talk: a centre box plus four
// corner boxes. Halving with a plain bilinear tap aliases, and on a bloom source that
// shows up as bright pixels flickering in and out as the camera moves.

#include "post.hlsli"

float4 PSMain(PostVSOutput input) : SV_TARGET
{
	// The texel size of the level being written, so the taps straddle the four source
	// texels that feed each destination one.
	float2 texel = float2(effectConstants.params0.w, effectConstants.params1.x);

	float3 a = postTexture0.SampleLevel(postSampler, input.uv + texel * float2(-2.0f,  2.0f), 0.0f).rgb;
	float3 b = postTexture0.SampleLevel(postSampler, input.uv + texel * float2( 0.0f,  2.0f), 0.0f).rgb;
	float3 c = postTexture0.SampleLevel(postSampler, input.uv + texel * float2( 2.0f,  2.0f), 0.0f).rgb;

	float3 d = postTexture0.SampleLevel(postSampler, input.uv + texel * float2(-2.0f,  0.0f), 0.0f).rgb;
	float3 e = postTexture0.SampleLevel(postSampler, input.uv, 0.0f).rgb;
	float3 f = postTexture0.SampleLevel(postSampler, input.uv + texel * float2( 2.0f,  0.0f), 0.0f).rgb;

	float3 g = postTexture0.SampleLevel(postSampler, input.uv + texel * float2(-2.0f, -2.0f), 0.0f).rgb;
	float3 h = postTexture0.SampleLevel(postSampler, input.uv + texel * float2( 0.0f, -2.0f), 0.0f).rgb;
	float3 i = postTexture0.SampleLevel(postSampler, input.uv + texel * float2( 2.0f, -2.0f), 0.0f).rgb;

	float3 j = postTexture0.SampleLevel(postSampler, input.uv + texel * float2(-1.0f,  1.0f), 0.0f).rgb;
	float3 k = postTexture0.SampleLevel(postSampler, input.uv + texel * float2( 1.0f,  1.0f), 0.0f).rgb;
	float3 l = postTexture0.SampleLevel(postSampler, input.uv + texel * float2(-1.0f, -1.0f), 0.0f).rgb;
	float3 m = postTexture0.SampleLevel(postSampler, input.uv + texel * float2( 1.0f, -1.0f), 0.0f).rgb;

	// The inner box carries half the weight; the four outer boxes share the rest.
	float3 result = (j + k + l + m) * 0.5f;
	result += (a + b + d + e) * 0.125f;
	result += (b + c + e + f) * 0.125f;
	result += (d + e + g + h) * 0.125f;
	result += (e + f + h + i) * 0.125f;

	return float4(result * 0.25f, 1.0f);
}
