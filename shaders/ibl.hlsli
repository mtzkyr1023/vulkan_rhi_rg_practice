
// Image based lighting from a procedural sky.
//
// Diffuse comes from nine spherical harmonic coefficients rather than a texture. A
// Lambertian surface integrates incoming radiance against a clamped cosine lobe, and that
// lobe is so smooth that everything above the second band contributes under one percent.
// Nine numbers therefore reconstruct the whole diffuse response, for any environment, at
// no sampling cost at all.
//
// Specular uses the split-sum approximation: the environment is pre-convolved with the
// GGX lobe into a mip chain, and the BRDF's own contribution comes from an analytic fit
// rather than the usual lookup table.

#ifndef _MV_IBL_HLSLI_
#define _MV_IBL_HLSLI_

#include "common.hlsli"

// Must match environment.h.
#define MV_IBL_MIP_COUNT 6

// Ramamoorthi and Hanrahan's constants: the cosine lobe's own spherical harmonic
// coefficients, folded in so a nine-term dot product yields irradiance directly.
static const float MV_SH_C1 = 0.429043f;
static const float MV_SH_C2 = 0.511664f;
static const float MV_SH_C3 = 0.743125f;
static const float MV_SH_C4 = 0.886227f;
static const float MV_SH_C5 = 0.247708f;

// Irradiance arriving at a surface facing `n`, reconstructed from the coefficients.
float3 shIrradiance(float3 n)
{
	float3 L00  = shCoefficients[0].rgb;
	float3 L1m1 = shCoefficients[1].rgb;
	float3 L10  = shCoefficients[2].rgb;
	float3 L11  = shCoefficients[3].rgb;
	float3 L2m2 = shCoefficients[4].rgb;
	float3 L2m1 = shCoefficients[5].rgb;
	float3 L20  = shCoefficients[6].rgb;
	float3 L21  = shCoefficients[7].rgb;
	float3 L22  = shCoefficients[8].rgb;

	float x = n.x;
	float y = n.y;
	float z = n.z;

	return max(0.0f.xxx,
		MV_SH_C4 * L00
		+ 2.0f * MV_SH_C2 * (L11 * x + L1m1 * y + L10 * z)
		+ 2.0f * MV_SH_C1 * (L2m2 * x * y + L21 * x * z + L2m1 * y * z)
		+ MV_SH_C3 * L20 * z * z
		- MV_SH_C5 * L20
		+ MV_SH_C1 * L22 * (x * x - y * y));
}

// Karis's analytic fit to the split-sum environment BRDF. Two constant vectors and a few
// multiplies replace the 2D lookup table the technique is usually shipped with, at an
// error too small to see.
float2 envBRDFApprox(float roughness, float NdotV)
{
	const float4 c0 = float4(-1.0f, -0.0275f, -0.572f, 0.022f);
	const float4 c1 = float4(1.0f, 0.0425f, 1.04f, -0.04f);

	float4 r = roughness * c0 + c1;
	float a004 = min(r.x * r.x, exp2(-9.28f * NdotV)) * r.x + r.y;

	return float2(-1.04f, 1.04f) * a004 + r.zw;
}

// The environment as seen along `direction`, blurred to match `roughness`.
float3 prefilteredRadiance(float3 direction, float roughness)
{
	// The chain was built with roughness running linearly from 0 at the top to 1 at the
	// bottom, so the level is just that mapping in reverse.
	float mip = roughness * (float)(MV_IBL_MIP_COUNT - 1);

	return environmentMap.SampleLevel(samplers[MV_SAMPLER_LINEAR_CLAMP], direction, mip).rgb;
}

// The ambient response of a surface: a diffuse term from the coefficients and a specular
// term from the prefiltered chain.
float3 evaluateIBL(float3 N, float3 V, float3 F0, float3 diffuseColor, float roughness, float occlusion)
{
	float NdotV = saturate(dot(N, V));

	float3 diffuse = diffuseColor * shIrradiance(N) / 3.14159265359f;

	float3 R = reflect(-V, N);
	float3 prefiltered = prefilteredRadiance(R, roughness);

	float2 brdf = envBRDFApprox(roughness, NdotV);
	float3 specular = prefiltered * (F0 * brdf.x + brdf.y);

	// One occlusion term for both, which is wrong for specular but is what a single
	// baked-in ambient occlusion map can honestly support.
	return (diffuse + specular) * occlusion * iblIntensity;
}

#endif
