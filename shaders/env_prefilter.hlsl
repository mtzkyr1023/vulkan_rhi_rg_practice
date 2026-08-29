// One roughness level of the prefiltered chain.
//
// Importance-sampled GGX rather than a uniform sweep: at sixty-four samples the difference
// between the two is the difference between a usable reflection and a noisy one, because
// the samples land where the lobe actually has energy.

#define MV_CUSTOM_PUSH_CONSTANTS
#include "common.hlsli"
#include "env.hlsli"

#ifdef MV_TARGET_VULKAN
[[vk::push_constant]] SkyConstants sky;
#else
ConstantBuffer<SkyConstants> sky : register(b0, space9);
#endif

// The source is read as a storage image rather than a cube texture so the whole chain can
// stay in one layout while it is built -- the same reason the mip generator does. Cube
// lookup and bilinear filtering are done by hand, which is what the CPU version did too.
#ifdef MV_TARGET_VULKAN
[[vk::image_format("rgba16f")]] RWTexture2DArray<float4> sourceLevel : register(u0, space0);
[[vk::image_format("rgba16f")]] RWTexture2DArray<float4> targetLevel : register(u1, space0);
#else
RWTexture2DArray<float4> sourceLevel : register(u0, space0);
RWTexture2DArray<float4> targetLevel : register(u1, space0);
#endif

// Bilinear lookup into the source level, by direction. Clamped within the face rather than
// wrapped across the seam: at these sizes the error is a fraction of a texel at the edges
// and crossing faces properly costs three more branches per tap.
float3 mvSampleCube(float3 direction, uint size)
{
	uint face;
	float u, v;
	mvDirectionToFace(normalize(direction), face, u, v);

	const float x = (u * 0.5f + 0.5f) * float(size) - 0.5f;
	const float y = (v * 0.5f + 0.5f) * float(size) - 0.5f;

	const float fx = floor(x);
	const float fy = floor(y);

	const float tx = x - fx;
	const float ty = y - fy;

	const int last = int(size) - 1;

	const int x0 = clamp(int(fx), 0, last);
	const int y0 = clamp(int(fy), 0, last);
	const int x1 = clamp(int(fx) + 1, 0, last);
	const int y1 = clamp(int(fy) + 1, 0, last);

	const float3 c00 = sourceLevel[uint3(x0, y0, face)].rgb;
	const float3 c10 = sourceLevel[uint3(x1, y0, face)].rgb;
	const float3 c01 = sourceLevel[uint3(x0, y1, face)].rgb;
	const float3 c11 = sourceLevel[uint3(x1, y1, face)].rgb;

	return lerp(lerp(c00, c10, tx), lerp(c01, c11, tx), ty);
}

// Low-discrepancy pairs. Radical inverse in base 2 by bit reversal.
float mvRadicalInverse(uint bits)
{
	bits = (bits << 16u) | (bits >> 16u);
	bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
	bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
	bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
	bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);

	return float(bits) * 2.3283064365386963e-10f;
}

float3 mvImportanceSampleGGX(uint index, uint count, float roughness, float3 normal)
{
	const float a = roughness * roughness;

	const float u1 = float(index) / float(count);
	const float u2 = mvRadicalInverse(index);

	const float phi = 2.0f * MV_PI * u1;
	const float cosTheta = sqrt((1.0f - u2) / (1.0f + (a * a - 1.0f) * u2));
	const float sinTheta = sqrt(max(0.0f, 1.0f - cosTheta * cosTheta));

	const float3 tangentH = float3(sinTheta * cos(phi), sinTheta * sin(phi), cosTheta);

	const float3 up = (abs(normal.z) < 0.999f) ? float3(0.0f, 0.0f, 1.0f) : float3(1.0f, 0.0f, 0.0f);

	const float3 tangentX = normalize(cross(up, normal));
	const float3 tangentY = cross(normal, tangentX);

	return tangentX * tangentH.x + tangentY * tangentH.y + normal * tangentH.z;
}

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
	if (id.x >= sky.faceSize || id.y >= sky.faceSize)
		return;

	const uint face = id.z;

	const float u = ((float(id.x) + 0.5f) / float(sky.faceSize)) * 2.0f - 1.0f;
	const float v = ((float(id.y) + 0.5f) / float(sky.faceSize)) * 2.0f - 1.0f;

	// The split-sum approximation assumes the view direction equals the normal. It is
	// wrong at grazing angles, which is why the reflection stretches less than it should,
	// and it is what makes a single prefiltered chain enough.
	const float3 normal = normalize(mvFaceDirection(face, u, v));

	const uint kSamples = 64;

	float3 total = 0.0f.xxx;
	float weight = 0.0f;

	for (uint i = 0; i < kSamples; i++)
	{
		const float3 h = mvImportanceSampleGGX(i, kSamples, sky.roughness, normal);
		const float3 l = normalize(2.0f * dot(normal, h) * h - normal);

		const float nDotL = dot(normal, l);

		if (nDotL <= 0.0f)
			continue;

		total += mvSampleCube(l, sky.sourceSize) * nDotL;
		weight += nDotL;
	}

	// No sample landed in the hemisphere, which only happens at the most extreme
	// roughness; the mirror direction is the honest answer there.
	const float3 result = (weight > 0.0f) ? (total / weight) : mvSampleCube(normal, sky.sourceSize);

	targetLevel[uint3(id.xy, face)] = float4(result, 1.0f);
}
