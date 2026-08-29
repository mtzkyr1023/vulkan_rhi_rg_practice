// Tone mapping and the sRGB encode: where the chain stops being half-float radiance and
// becomes something a display can show.
//
// This used to be the last two lines of the material shader. Having it here is what lets
// every effect before it work in the range the lighting actually produced.

#include "post.hlsli"

// Narkowicz's ACES approximation.
float3 tonemapACESFilmic(float3 x)
{
	const float a = 2.51f;
	const float b = 0.03f;
	const float c = 2.43f;
	const float d = 0.59f;
	const float e = 0.14f;

	return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

float3 encodeSrgb(float3 linearColor)
{
	float3 low = linearColor * 12.92f;
	float3 high = 1.055f * pow(max(linearColor, 1e-5f), 1.0f / 2.4f) - 0.055f;

	return lerp(low, high, step(0.0031308f.xxx, linearColor));
}

float4 PSMain(PostVSOutput input) : SV_TARGET
{
	float exposureStops = effectConstants.params0.x;

	float4 color = postTexture0.SampleLevel(postSampler, input.uv, 0.0f);

	// Stops, so each step doubles the light reaching the sensor.
	color.rgb *= exp2(exposureStops);

	return float4(encodeSrgb(tonemapACESFilmic(color.rgb)), color.a);
}
