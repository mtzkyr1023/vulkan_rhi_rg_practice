// One step back up the pyramid: the smaller level, filtered as it is magnified, added to
// the level above it.
//
// Summing every level rather than taking only the smallest is what gives bloom its long
// tail. Each scale contributes, weighted by how many blurs it has been through.

#include "post.hlsli"

float4 PSMain(PostVSOutput input) : SV_TARGET
{
	// The texel size of the level being written.
	float2 texel = float2(effectConstants.params0.w, effectConstants.params1.x);

	// A 3x3 tent, which is smooth enough that the magnification does not show as blocks.
	float3 result = 0.0f.xxx;

	result += postTexture0.SampleLevel(postSampler, input.uv + texel * float2(-1.0f,  1.0f), 0.0f).rgb * 1.0f;
	result += postTexture0.SampleLevel(postSampler, input.uv + texel * float2( 0.0f,  1.0f), 0.0f).rgb * 2.0f;
	result += postTexture0.SampleLevel(postSampler, input.uv + texel * float2( 1.0f,  1.0f), 0.0f).rgb * 1.0f;

	result += postTexture0.SampleLevel(postSampler, input.uv + texel * float2(-1.0f,  0.0f), 0.0f).rgb * 2.0f;
	result += postTexture0.SampleLevel(postSampler, input.uv, 0.0f).rgb * 4.0f;
	result += postTexture0.SampleLevel(postSampler, input.uv + texel * float2( 1.0f,  0.0f), 0.0f).rgb * 2.0f;

	result += postTexture0.SampleLevel(postSampler, input.uv + texel * float2(-1.0f, -1.0f), 0.0f).rgb * 1.0f;
	result += postTexture0.SampleLevel(postSampler, input.uv + texel * float2( 0.0f, -1.0f), 0.0f).rgb * 2.0f;
	result += postTexture0.SampleLevel(postSampler, input.uv + texel * float2( 1.0f, -1.0f), 0.0f).rgb * 1.0f;

	result *= 1.0f / 16.0f;

	// postTexture1 is the level being added into, which is also the render target. Reading
	// it here rather than blending is deliberate: the chain has no blend state to
	// configure, and an explicit read makes the dependency visible.
	float3 existing = postTexture1.SampleLevel(postSampler, input.uv, 0.0f).rgb;

	return float4(existing + result, 1.0f);
}
