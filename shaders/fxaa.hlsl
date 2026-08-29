// Fast approximate anti-aliasing, on the tone-mapped image.
//
// It works on perceived luminance, which only means anything once the range has been
// compressed, so it belongs after tone mapping. That ordering is the reason the chain is
// a list rather than a set.

#include "post.hlsli"

float sampleLuma(float2 uv)
{
	return luminance(postTexture0.SampleLevel(postSampler, uv, 0.0f).rgb);
}

float4 PSMain(PostVSOutput input) : SV_TARGET
{
	float2 texelSize = float2(effectConstants.params0.x, effectConstants.params0.y);
	float contrastThreshold = effectConstants.params0.z;
	float relativeThreshold = effectConstants.params0.w;

	float3 center = postTexture0.SampleLevel(postSampler, input.uv, 0.0f).rgb;

	// The four direct neighbours are enough to find both the contrast and the edge
	// direction; the diagonals only refine the blend factor.
	float lumaCenter = luminance(center);
	float lumaNorth = sampleLuma(input.uv + float2(0.0f, -texelSize.y));
	float lumaSouth = sampleLuma(input.uv + float2(0.0f, texelSize.y));
	float lumaEast  = sampleLuma(input.uv + float2(texelSize.x, 0.0f));
	float lumaWest  = sampleLuma(input.uv + float2(-texelSize.x, 0.0f));

	float lumaMin = min(lumaCenter, min(min(lumaNorth, lumaSouth), min(lumaEast, lumaWest)));
	float lumaMax = max(lumaCenter, max(max(lumaNorth, lumaSouth), max(lumaEast, lumaWest)));
	float contrast = lumaMax - lumaMin;

	// Flat enough to leave alone. The relative term is what keeps the filter from chewing
	// on noise in the dark, where a small absolute difference is a large relative one.
	if (contrast < max(contrastThreshold, lumaMax * relativeThreshold))
		return float4(center, 1.0f);

	float lumaNorthWest = sampleLuma(input.uv + float2(-texelSize.x, -texelSize.y));
	float lumaNorthEast = sampleLuma(input.uv + float2(texelSize.x, -texelSize.y));
	float lumaSouthWest = sampleLuma(input.uv + float2(-texelSize.x, texelSize.y));
	float lumaSouthEast = sampleLuma(input.uv + float2(texelSize.x, texelSize.y));

	// How much this pixel differs from the average around it, normalised by the local
	// contrast, is the blend factor: pixels in the middle of an edge move most.
	float average = (2.0f * (lumaNorth + lumaSouth + lumaEast + lumaWest)
		+ lumaNorthWest + lumaNorthEast + lumaSouthWest + lumaSouthEast) / 12.0f;

	float blend = saturate(abs(average - lumaCenter) / contrast);
	blend = smoothstep(0.0f, 1.0f, blend);
	blend = blend * blend;

	// Whether the edge runs across or up the screen decides which way to step.
	float horizontal =
		abs(lumaNorth + lumaSouth - 2.0f * lumaCenter) * 2.0f +
		abs(lumaNorthEast + lumaSouthEast - 2.0f * lumaEast) +
		abs(lumaNorthWest + lumaSouthWest - 2.0f * lumaWest);

	float vertical =
		abs(lumaEast + lumaWest - 2.0f * lumaCenter) * 2.0f +
		abs(lumaNorthEast + lumaNorthWest - 2.0f * lumaNorth) +
		abs(lumaSouthEast + lumaSouthWest - 2.0f * lumaSouth);

	bool isHorizontal = horizontal >= vertical;

	// Step towards whichever side of the edge is further from this pixel in luminance.
	float positive = abs((isHorizontal ? lumaNorth : lumaEast) - lumaCenter);
	float negative = abs((isHorizontal ? lumaSouth : lumaWest) - lumaCenter);

	float pixelStep = isHorizontal ? texelSize.y : texelSize.x;
	if (positive < negative)
	{
		pixelStep = -pixelStep;
	}

	float2 uv = input.uv;
	if (isHorizontal)
	{
		uv.y += pixelStep * blend;
	}
	else
	{
		uv.x += pixelStep * blend;
	}

	return float4(postTexture0.SampleLevel(postSampler, uv, 0.0f).rgb, 1.0f);
}
