// One level of the downward chain. The compute form of bloom_downsample.hlsl.
//
// Still sampler-based rather than reading the source as a storage image: the thirteen taps
// are all bilinear, and letting the hardware do the filtering is the whole reason this
// pattern costs what it does.

#define MV_CUSTOM_PUSH_CONSTANTS
#include "common.hlsli"
#include "bloom_cs.hlsli"

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
	float2 uv;
	if (!bloomThreadUv(id, uv))
		return;

	// The texel size of the level being written, so the taps straddle the four source
	// texels that feed each destination one.
	const float2 texel = float2(bloom.texelX, bloom.texelY);

	const float3 a = bloomSource.SampleLevel(bloomSampler, uv + texel * float2(-2.0f,  2.0f), 0.0f).rgb;
	const float3 b = bloomSource.SampleLevel(bloomSampler, uv + texel * float2( 0.0f,  2.0f), 0.0f).rgb;
	const float3 c = bloomSource.SampleLevel(bloomSampler, uv + texel * float2( 2.0f,  2.0f), 0.0f).rgb;

	const float3 d = bloomSource.SampleLevel(bloomSampler, uv + texel * float2(-2.0f,  0.0f), 0.0f).rgb;
	const float3 e = bloomSource.SampleLevel(bloomSampler, uv, 0.0f).rgb;
	const float3 f = bloomSource.SampleLevel(bloomSampler, uv + texel * float2( 2.0f,  0.0f), 0.0f).rgb;

	const float3 g = bloomSource.SampleLevel(bloomSampler, uv + texel * float2(-2.0f, -2.0f), 0.0f).rgb;
	const float3 h = bloomSource.SampleLevel(bloomSampler, uv + texel * float2( 0.0f, -2.0f), 0.0f).rgb;
	const float3 i = bloomSource.SampleLevel(bloomSampler, uv + texel * float2( 2.0f, -2.0f), 0.0f).rgb;

	const float3 j = bloomSource.SampleLevel(bloomSampler, uv + texel * float2(-1.0f,  1.0f), 0.0f).rgb;
	const float3 k = bloomSource.SampleLevel(bloomSampler, uv + texel * float2( 1.0f,  1.0f), 0.0f).rgb;
	const float3 l = bloomSource.SampleLevel(bloomSampler, uv + texel * float2(-1.0f, -1.0f), 0.0f).rgb;
	const float3 m = bloomSource.SampleLevel(bloomSampler, uv + texel * float2( 1.0f, -1.0f), 0.0f).rgb;

	// The inner box carries half the weight; the four outer boxes share the rest.
	float3 result = (j + k + l + m) * 0.5f;
	result += (a + b + d + e) * 0.125f;
	result += (b + c + e + f) * 0.125f;
	result += (d + e + g + h) * 0.125f;
	result += (e + f + h + i) * 0.125f;

	result *= 0.25f;

	bloomTarget[id.xy] = float4(result, 1.0f);
}
