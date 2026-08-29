// Vignette and chromatic aberration. The shortest example of adding an effect: one class,
// one shader, three parameters.

#include "post.hlsli"

float4 PSMain(PostVSOutput input) : SV_TARGET
{
	float vignetteIntensity = effectConstants.params0.x;
	float vignetteSmoothness = effectConstants.params0.y;
	float aberration = effectConstants.params0.z;

	// Distance from the centre, which both effects are driven by: a real lens is only
	// itself in the middle and gets worse towards the edge.
	float2 centered = input.uv - 0.5f;
	float radius = length(centered);

	// The three channels are refracted by slightly different amounts, so they are sampled
	// at slightly different distances from the centre.
	float2 offset = centered * aberration * radius;

	float4 color;
	color.r = postTexture0.SampleLevel(postSampler, input.uv + offset, 0.0f).r;
	color.g = postTexture0.SampleLevel(postSampler, input.uv, 0.0f).g;
	color.b = postTexture0.SampleLevel(postSampler, input.uv - offset, 0.0f).b;
	color.a = 1.0f;

	float vignette = smoothstep(0.8f, vignetteSmoothness, radius * (1.0f + vignetteIntensity));
	color.rgb *= lerp(1.0f, vignette, saturate(vignetteIntensity));

	return color;
}
