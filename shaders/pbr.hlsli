
// glTF metallic-roughness PBR: Cook-Torrance specular (GGX / Smith / Schlick) with a
// Lambert diffuse term, one directional light and a constant ambient stand-in for IBL.
//
// Lighting runs in linear space. Base colour and emissive are sampled through sRGB
// texture formats so the hardware decodes them, and the result is tone mapped and
// re-encoded to sRGB on the way out because the swap chain is a UNORM target.
//
// Every texture fetch takes explicit gradients: the forward path can supply the ones the
// hardware would have derived anyway, while the visibility buffer pass has no quad
// derivatives to work from and computes them from the triangle instead.

#ifndef _MV_PBR_HLSLI_
#define _MV_PBR_HLSLI_

#include "vt.hlsli"

static const float PI = 3.14159265359f;

// GGX / Trowbridge-Reitz normal distribution.
float distributionGGX(float NdotH, float roughness)
{
	float a = roughness * roughness;
	float a2 = a * a;
	float d = NdotH * NdotH * (a2 - 1.0f) + 1.0f;

	return a2 / max(PI * d * d, 1e-7f);
}

// Smith height-correlated visibility, already including the 1 / (4 NdotL NdotV) term.
float visibilitySmith(float NdotV, float NdotL, float roughness)
{
	float a = roughness * roughness;
	float a2 = a * a;

	float lambdaV = NdotL * sqrt(NdotV * NdotV * (1.0f - a2) + a2);
	float lambdaL = NdotV * sqrt(NdotL * NdotL * (1.0f - a2) + a2);

	return 0.5f / max(lambdaV + lambdaL, 1e-7f);
}

float3 fresnelSchlick(float cosTheta, float3 F0)
{
	return F0 + (1.0f - F0) * pow(saturate(1.0f - cosTheta), 5.0f);
}

// Roughness-aware Fresnel, used for the ambient term so rough surfaces do not pick up a
// hard rim highlight.
float3 fresnelSchlickRoughness(float cosTheta, float3 F0, float roughness)
{
	float3 maxReflectance = max((1.0f - roughness).xxx, F0);
	return F0 + (maxReflectance - F0) * pow(saturate(1.0f - cosTheta), 5.0f);
}

// Narkowicz's ACES approximation.
float3 tonemapACES(float3 x)
{
	const float a = 2.51f;
	const float b = 0.03f;
	const float c = 2.43f;
	const float d = 0.59f;
	const float e = 0.14f;

	return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

float3 linearToSrgb(float3 linearColor)
{
	float3 low = linearColor * 12.92f;
	float3 high = 1.055f * pow(max(linearColor, 1e-5f), 1.0f / 2.4f) - 0.055f;

	return lerp(low, high, step(0.0031308f.xxx, linearColor));
}

// Builds a tangent frame from the surface normal and the position/uv gradients. glTF
// files are not required to carry TANGENT, and this avoids generating one at load time.
// Taking the gradients as arguments rather than calling ddx/ddy lets the visibility
// buffer pass reuse it, where no quad derivatives exist.
float3x3 cotangentFrame(float3 N, float3 dp1, float3 dp2, float2 duv1, float2 duv2)
{
	float3 dp2perp = cross(dp2, N);
	float3 dp1perp = cross(N, dp1);

	float3 T = dp2perp * duv1.x + dp1perp * duv2.x;
	float3 B = dp2perp * duv1.y + dp1perp * duv2.y;

	float invmax = rsqrt(max(dot(T, T), dot(B, B)));

	return float3x3(T * invmax, B * invmax, N);
}

struct SurfaceInput
{
	GpuMaterial material;

	float2 uv;
	float2 uvDdx;
	float2 uvDdy;

	float3 worldPosition;
	float3 geometricNormal;

	// World-space position gradients, for the tangent frame.
	float3 positionDdx;
	float3 positionDdy;
};

// Returns the sampled base colour so callers can alpha test before doing the rest.
//
// Every material fetch goes through sampleVirtual, which resolves the page table when the
// texture has been virtualised and samples it directly when it has not. The per-material
// sampler preset is not passed along: virtual addressing has to be clamped and wrapped by
// hand, so the choice is made inside.
float4 sampleBaseColor(SurfaceInput surface)
{
	return sampleVirtual(surface.material.baseColorTexture, surface.uv, surface.uvDdx, surface.uvDdy)
		* surface.material.baseColorFactor;
}

float4 shadeSurface(SurfaceInput surface, float4 baseColor)
{
	GpuMaterial material = surface.material;

	// glTF packs roughness in G and metallic in B.
	float3 metallicRoughness = sampleVirtual(material.metallicRoughnessTexture, surface.uv, surface.uvDdx, surface.uvDdy).rgb;
	float roughness = saturate(metallicRoughness.g * material.roughnessFactor);
	float metallic = saturate(metallicRoughness.b * material.metallicFactor);

	// A perfectly smooth surface produces a singular specular lobe, so clamp it away.
	roughness = max(roughness, 0.045f);

	float occlusion = sampleVirtual(material.occlusionTexture, surface.uv, surface.uvDdx, surface.uvDdy).r;
	occlusion = lerp(1.0f, occlusion, material.occlusionStrength);

	float3 emissive = sampleVirtual(material.emissiveTexture, surface.uv, surface.uvDdx, surface.uvDdy).rgb * material.emissiveFactor;

	float3 geometricNormal = normalize(surface.geometricNormal);
	float3 V = normalize(cameraPosition - surface.worldPosition);

	// Two-sided shading: culling is off for double-sided materials, so back faces need
	// their normal flipped or they light as if they faced away.
	if (dot(geometricNormal, V) < 0.0f)
	{
		geometricNormal = -geometricNormal;
	}

	float3 tangentNormal = sampleVirtual(material.normalTexture, surface.uv, surface.uvDdx, surface.uvDdy).rgb * 2.0f - 1.0f;
	tangentNormal.xy *= material.normalScale;

	float3x3 TBN = cotangentFrame(geometricNormal, surface.positionDdx, surface.positionDdy, surface.uvDdx, surface.uvDdy);
	float3 N = normalize(mul(tangentNormal, TBN));

	float3 L = -normalize(lightDirection);
	float3 H = normalize(V + L);

	float NdotV = saturate(dot(N, V)) + 1e-5f;
	float NdotL = saturate(dot(N, L));
	float NdotH = saturate(dot(N, H));
	float VdotH = saturate(dot(V, H));

	// Dielectrics reflect ~4% at normal incidence; metals use their albedo as F0 and have
	// no diffuse term.
	float3 F0 = lerp(0.04f.xxx, baseColor.rgb, metallic);
	float3 diffuseColor = baseColor.rgb * (1.0f - metallic);

	float3 F = fresnelSchlick(VdotH, F0);
	float D = distributionGGX(NdotH, roughness);
	float Vis = visibilitySmith(NdotV, NdotL, roughness);

	float3 specular = F * D * Vis;
	float3 diffuse = (1.0f - F) * diffuseColor / PI;

	float3 direct = (diffuse + specular) * lightColor * NdotL;

	// Stand-in for image based lighting: a constant environment split between a diffuse
	// and a specular lobe by the same Fresnel term.
	float3 ambientF = fresnelSchlickRoughness(NdotV, F0, roughness);
	float3 ambient = (diffuseColor * (1.0f - ambientF) + ambientF * 0.5f) * ambientIntensity * occlusion;

	float3 color = direct * occlusion + ambient + emissive;

	return float4(linearToSrgb(tonemapACES(color)), baseColor.a);
}

#endif
