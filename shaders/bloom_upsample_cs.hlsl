// One level of the upward chain. The compute form of bloom_upsample.hlsl.

#define MV_CUSTOM_PUSH_CONSTANTS
#include "common.hlsli"
#include "bloom_cs.hlsli"

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
	float2 uv;
	if (!bloomThreadUv(id, uv))
		return;

	// The texel size of the level being written.
	const float2 texel = float2(bloom.texelX, bloom.texelY);

	// A 3x3 tent, which is smooth enough that the magnification does not show as blocks.
	float3 result = 0.0f.xxx;

	result += bloomSource.SampleLevel(bloomSampler, uv + texel * float2(-1.0f,  1.0f), 0.0f).rgb * 1.0f;
	result += bloomSource.SampleLevel(bloomSampler, uv + texel * float2( 0.0f,  1.0f), 0.0f).rgb * 2.0f;
	result += bloomSource.SampleLevel(bloomSampler, uv + texel * float2( 1.0f,  1.0f), 0.0f).rgb * 1.0f;

	result += bloomSource.SampleLevel(bloomSampler, uv + texel * float2(-1.0f,  0.0f), 0.0f).rgb * 2.0f;
	result += bloomSource.SampleLevel(bloomSampler, uv, 0.0f).rgb * 4.0f;
	result += bloomSource.SampleLevel(bloomSampler, uv + texel * float2( 1.0f,  0.0f), 0.0f).rgb * 2.0f;

	result += bloomSource.SampleLevel(bloomSampler, uv + texel * float2(-1.0f, -1.0f), 0.0f).rgb * 1.0f;
	result += bloomSource.SampleLevel(bloomSampler, uv + texel * float2( 0.0f, -1.0f), 0.0f).rgb * 2.0f;
	result += bloomSource.SampleLevel(bloomSampler, uv + texel * float2( 1.0f, -1.0f), 0.0f).rgb * 1.0f;

	result *= 1.0f / 16.0f;

	// bloomAccum is what the downward pass left at this level -- a different texture from
	// the one being written, which is why the pyramid keeps two sets of levels rather than
	// blending in place.
	const float3 existing = bloomAccum.SampleLevel(bloomSampler, uv, 0.0f).rgb;

	bloomTarget[id.xy] = float4(existing + result, 1.0f);
}
