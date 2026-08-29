// Adds the finished bloom back onto the untouched chain input.

#include "post.hlsli"

float4 PSMain(PostVSOutput input) : SV_TARGET
{
	float intensity = effectConstants.params0.z;

	float4 scene = postTexture0.SampleLevel(postSampler, input.uv, 0.0f);
	float3 bloom = postTexture1.SampleLevel(postSampler, input.uv, 0.0f).rgb;

	return float4(scene.rgb + bloom * intensity, scene.a);
}
