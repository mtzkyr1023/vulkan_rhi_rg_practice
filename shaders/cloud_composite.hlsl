// Upsamples the half-resolution march and blends it into the scene.
//
// A fullscreen draw rather than a dispatch, because the destination is the HDR scene target
// the rest of the frame renders into and the blend hardware does the composite for free:
// with (One, SrcAlpha) and a premultiplied source, the output is
//
//     scene * transmittance + scattering
//
// which is exactly what the march accumulated.
//
// The upsample weights the four coarse taps by how close their depth is to this pixel's.
// A plain bilinear filter bleeds cloud across the silhouette of a hill, and at half
// resolution that halo is two pixels wide and crawls as the camera moves.
//
// There is no separate half-resolution depth buffer: the march sampled the full-resolution
// one at its own texel centres, so sampling it at those same centres here reproduces
// exactly the depth each tap was marched against.

#define MV_CUSTOM_PUSH_CONSTANTS
#include "common.hlsli"

struct CloudCompositeConstants
{
	// Texel size of the half-resolution buffer, and its dimensions.
	float2 lowResTexel;
	uint2 lowResSize;

	// How much depth difference it takes to start rejecting a tap. In depth-buffer units,
	// which are non-linear, so this is deliberately generous.
	float depthTolerance;
	float3 _compositePad;
};

#ifdef MV_TARGET_VULKAN
[[vk::push_constant]] CloudCompositeConstants composite;
#else
ConstantBuffer<CloudCompositeConstants> composite : register(b0, space9);
#endif

Texture2D<float4> cloudLowRes : register(t0, space0);
Texture2D<float>  sceneDepth  : register(t1, space0);

SamplerState pointClamp : register(s2, space0);

struct CloudVSOutput
{
	float4 position : SV_POSITION;
	float2 uv       : TEXCOORD0;
};

// One oversized triangle rather than a quad: no diagonal seam, and no vertex buffer.
CloudVSOutput VSMain(uint vertexId : SV_VertexID)
{
	CloudVSOutput output;

	output.uv = float2((vertexId << 1) & 2, vertexId & 2);
	output.position = float4(output.uv * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);

	return output;
}

float4 PSMain(CloudVSOutput input) : SV_TARGET
{

	const float centreDepth = sceneDepth.SampleLevel(pointClamp, input.uv, 0.0f);

	// The four coarse texels this pixel sits between.
	const float2 coarse = input.uv * float2(composite.lowResSize) - 0.5f;
	const float2 base = floor(coarse);
	const float2 f = coarse - base;

	const float bilinear[4] =
	{
		(1.0f - f.x) * (1.0f - f.y),
		f.x * (1.0f - f.y),
		(1.0f - f.x) * f.y,
		f.x * f.y,
	};

	const int2 offsets[4] = { int2(0, 0), int2(1, 0), int2(0, 1), int2(1, 1) };

	float4 accumulated = 0.0f.xxxx;
	float totalWeight = 0.0f;

	[unroll]
	for (int i = 0; i < 4; i++)
	{
		const float2 tapUv = (base + 0.5f + float2(offsets[i])) * composite.lowResTexel;

		const float tapDepth = sceneDepth.SampleLevel(pointClamp, tapUv, 0.0f);

		// Falls off as the tap's depth diverges from this pixel's, so a tap that marched
		// against the sky contributes nothing to a pixel on a hill in front of it.
		const float difference = abs(tapDepth - centreDepth);
		const float depthWeight = 1.0f / (difference / max(composite.depthTolerance, 1e-6f) + 1e-3f);

		const float weight = bilinear[i] * depthWeight;

		accumulated += cloudLowRes.SampleLevel(pointClamp, tapUv, 0.0f) * weight;
		totalWeight += weight;
	}

	// The epsilon above keeps this from happening, but a divide that can produce a NaN in
	// an HDR target is not worth leaving in.
	if (totalWeight <= 0.0f)
		return float4(0.0f, 0.0f, 0.0f, 1.0f);

	return accumulated / totalWeight;
}
