
// Cascaded shadow map lookup.
//
// The cascades live as a 2x2 grid in one depth atlas. A fragment picks a cascade from its
// view depth, projects into that cascade, and compares against the stored depth through a
// comparison sampler so each tap is already a filtered 2x2.

#ifndef _MV_SHADOW_HLSLI_
#define _MV_SHADOW_HLSLI_

#include "common.hlsli"

// Must match cascaded_shadow_map.h.
#define MV_SHADOW_RESOLUTION 2048.0f
#define MV_SHADOW_ATLAS_SIZE 4096.0f

// The first cascade whose split reaches past this depth. Cascades are ordered near to far,
// so the first match is the tightest one that covers the fragment.
uint selectCascade(float viewDepth)
{
	// Unrolled rather than looped over cascadeSplits[i]: indexing a float4 dynamically
	// costs more than four compares.
	if (viewDepth < cascadeSplits.x) return 0;
	if (viewDepth < cascadeSplits.y) return 1;
	if (viewDepth < cascadeSplits.z) return 2;

	return 3;
}

float cascadeTexelSizeFor(uint cascade)
{
	if (cascade == 0) return cascadeTexelSize.x;
	if (cascade == 1) return cascadeTexelSize.y;
	if (cascade == 2) return cascadeTexelSize.z;

	return cascadeTexelSize.w;
}

// Cascade-local [0,1] coordinates into the atlas tile that cascade occupies.
float2 cascadeAtlasUv(uint cascade, float2 uv)
{
	float2 tile = float2(cascade % 2u, cascade / 2u);

	return (tile + uv) * 0.5f;
}

// Returns 1 where the fragment is lit and 0 where it is fully shadowed.
float sampleShadow(uint cascade, float3 worldPosition, float3 normal, float NdotL)
{
	// Normal offset: slide the lookup out along the surface normal by about one shadow
	// texel before projecting. It removes the acne that a depth bias alone cannot, because
	// the error is a horizontal displacement across the surface rather than a depth
	// difference, and it scales with the cascade so it stays constant in screen terms.
	float texelSize = cascadeTexelSizeFor(cascade);
	float slope = saturate(1.0f - NdotL);

	float3 offsetPosition = worldPosition + normal * (texelSize * shadowNormalBias * (1.0f + slope * 2.0f));

	float4 clip = mul(float4(offsetPosition, 1.0f), cascadeViewProj[cascade]);

	// The cascade projection is orthographic, so w is 1 and there is nothing to divide by.
	float2 uv = clip.xy * float2(0.5f, -0.5f) + 0.5f;
	float depth = clip.z - shadowDepthBias;

	// Outside the cascade entirely. Only reachable for the last one, since every closer
	// fragment falls into a cascade that does cover it.
	if (any(uv < 0.0f) || any(uv > 1.0f) || depth > 1.0f)
		return 1.0f;

	// One texel of the tile, expressed in atlas UV.
	float texelUv = 1.0f / MV_SHADOW_ATLAS_SIZE;

	if (shadowPcfRadius <= 0)
		return shadowAtlas.SampleCmpLevelZero(shadowSampler, cascadeAtlasUv(cascade, uv), depth);

	// A box of comparison taps. Each one is already bilinear across four texels, so a
	// radius of 1 covers a 4x4 footprint rather than 3x3.
	float sum = 0.0f;
	float count = 0.0f;

	for (int y = -shadowPcfRadius; y <= shadowPcfRadius; y++)
	{
		for (int x = -shadowPcfRadius; x <= shadowPcfRadius; x++)
		{
			// Clamped inside the tile so a tap near the edge cannot read the cascade
			// stored next to it in the atlas.
			float2 tapUv = clamp(uv + float2(x, y) * (1.0f / MV_SHADOW_RESOLUTION), 0.0f, 1.0f);

			sum += shadowAtlas.SampleCmpLevelZero(shadowSampler, cascadeAtlasUv(cascade, tapUv), depth);
			count += 1.0f;
		}
	}

	return sum / count;
}

float cascadeSplitFor(uint cascade)
{
	if (cascade == 0) return cascadeSplits.x;
	if (cascade == 1) return cascadeSplits.y;
	if (cascade == 2) return cascadeSplits.z;

	return cascadeSplits.w;
}

// The full lookup for a shaded surface.
float shadowFactor(float3 worldPosition, float3 normal, float NdotL)
{
	if (!shadowEnabled)
		return 1.0f;

	float viewDepth = dot(worldPosition - cameraPosition, cameraForward);

	uint cascade = selectCascade(viewDepth);
	float shadow = sampleShadow(cascade, worldPosition, normal, NdotL);

	// Fade into the next cascade over the last part of this one. The two disagree slightly
	// wherever they meet, because a coarser cascade filters over a larger world footprint,
	// and without this the disagreement draws a line across the scene.
	if (cascade + 1u < MV_SHADOW_CASCADES && shadowCascadeBlend > 0.0f)
	{
		float split = cascadeSplitFor(cascade);
		float blendStart = split * (1.0f - shadowCascadeBlend);

		if (viewDepth > blendStart)
		{
			float t = saturate((viewDepth - blendStart) / max(split - blendStart, 1e-5f));

			shadow = lerp(shadow, sampleShadow(cascade + 1u, worldPosition, normal, NdotL), t);
		}
	}

	return shadow;
}

// Sun visibility at a point in the air, for volumetric sampling.
//
// The surface variant above offsets the lookup along the normal and averages a kernel of
// taps, because acne on a surface is the failure mode it fights. A point in the air has no
// surface, no normal and no acne, and a march that takes dozens of these per pixel wants
// exactly one hardware-filtered tap.
float shadowVisibilityAt(float3 worldPosition)
{
	if (!shadowEnabled)
		return 1.0f;

	const float viewDepth = dot(worldPosition - cameraPosition, cameraForward);

	// Past the last cascade the atlas has nothing to say, which for air is "lit".
	if (viewDepth > cascadeSplits.w)
		return 1.0f;

	const uint cascade = selectCascade(viewDepth);

	const float4 clip = mul(float4(worldPosition, 1.0f), cascadeViewProj[cascade]);

	const float2 uv = clip.xy * float2(0.5f, -0.5f) + 0.5f;
	const float depth = clip.z - shadowDepthBias;

	if (any(uv < 0.0f) || any(uv > 1.0f) || depth > 1.0f)
		return 1.0f;

	return shadowAtlas.SampleCmpLevelZero(shadowSampler, cascadeAtlasUv(cascade, uv), depth);
}

// Distinct colours per cascade for the debug view.
float3 cascadeDebugColor(float3 worldPosition)
{
	float viewDepth = dot(worldPosition - cameraPosition, cameraForward);

	const float3 palette[MV_SHADOW_CASCADES] =
	{
		float3(1.0f, 0.3f, 0.3f),
		float3(0.3f, 1.0f, 0.3f),
		float3(0.3f, 0.5f, 1.0f),
		float3(1.0f, 1.0f, 0.3f),
	};

	return palette[selectCascade(viewDepth)];
}

#endif
