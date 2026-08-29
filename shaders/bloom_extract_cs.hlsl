// Bright pixels only, at half resolution. The compute form of bloom_extract.hlsl.

#define MV_CUSTOM_PUSH_CONSTANTS
#include "common.hlsli"
#include "bloom_cs.hlsli"

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
	float2 uv;
	if (!bloomThreadUv(id, uv))
		return;

	const float3 color = bloomSource.SampleLevel(bloomSampler, uv, 0.0f).rgb;

	const float brightness = max(color.r, max(color.g, color.b));

	// Quadratic across the knee, linear above it.
	float softness = clamp(brightness - bloom.threshold + bloom.knee, 0.0f, 2.0f * bloom.knee);
	softness = softness * softness / (4.0f * bloom.knee + 1e-5f);

	const float contribution = max(softness, brightness - bloom.threshold) / max(brightness, 1e-5f);

	bloomTarget[id.xy] = float4(color * contribution, 1.0f);
}
