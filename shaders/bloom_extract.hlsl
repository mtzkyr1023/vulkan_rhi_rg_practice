// Keeps only what is bright enough to bloom.
//
// The knee softens the cut: a hard threshold makes a pixel pop into existence the moment
// it crosses, which flickers badly on anything moving.

#include "post.hlsli"

float4 PSMain(PostVSOutput input) : SV_TARGET
{
	float threshold = effectConstants.params0.x;
	float knee = effectConstants.params0.y;

	float3 color = postTexture0.SampleLevel(postSampler, input.uv, 0.0f).rgb;

	float brightness = max(color.r, max(color.g, color.b));

	// Quadratic across the knee, linear above it.
	float softness = clamp(brightness - threshold + knee, 0.0f, 2.0f * knee);
	softness = softness * softness / (4.0f * knee + 1e-5f);

	float contribution = max(softness, brightness - threshold) / max(brightness, 1e-5f);

	return float4(color * contribution, 1.0f);
}
