// Procedural grass, drawn indirectly: the vertex shader only ever sees blades that exist.
//
// The culling pass (grass_cull.hlsl) already decided which cells grow a blade and which
// of those the frustum can see, and appended one compact record per survivor -- root,
// scale, terrain normal, lean. What is left for the vertex shader is the cheap part:
// unfold fifteen canonical vertices into a tapered, wind-bent ribbon and transform them.
// The instance count in the draw arguments was written by the cull's own atomic, so the
// CPU never learns it and never has to.
//
// Blades grow where the terrain bake painted grass, by the bake's own rules -- that
// decision lives in the cull now, next to the height field it needs.
//
// Drawn as a forward pass after the visibility resolve, into the same colour, velocity
// and depth targets. The visibility buffer resolves triangles out of one global vertex
// array by id, and geometry that exists only in a shader has no id to give it.

#define MV_CUSTOM_PUSH_CONSTANTS
#include "common.hlsli"
#include "shadow.hlsli"
#include "ibl.hlsli"

// Must match GrassGpuConstants in grass_field.cpp.
struct GrassConstants
{
	float3 rootColor;
	float  bladeHeight;

	float3 tipColor;
	float  bladeWidth;

	float  time;
	float  windStrength;
	float2 _grassPad;
};

#ifdef MV_TARGET_VULKAN
[[vk::push_constant]] GrassConstants grass;
#else
ConstantBuffer<GrassConstants> grass : register(b0, space9);
#endif

// What the cull pass appended: three float4s per blade -- root+scale, normal+colourSeed,
// yaw+lean+windPhase. Must match the writer in grass_cull.hlsl.
StructuredBuffer<float4> instances : register(t0, space2);

struct GrassVSOutput
{
	float4 position      : SV_POSITION;
	float3 worldPosition : TEXCOORD0;
	float3 normal        : TEXCOORD1;

	// Root-to-tip fraction in x, the blade's colour variation in y.
	float2 blade         : TEXCOORD2;
};

GrassVSOutput VSMain(uint vertexId : SV_VertexID, uint instanceId : SV_InstanceID)
{
	GrassVSOutput output;

	// The canonical blade: seven vertices, five triangles, listed out.
	const uint indices[15] = { 0, 1, 2, 1, 3, 2, 2, 3, 4, 3, 5, 4, 4, 5, 6 };
	const float sides[7] = { -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, 0.0f };
	const float heights[7] = { 0.0f, 0.0f, 0.45f, 0.45f, 0.8f, 0.8f, 1.0f };

	const uint corner = indices[vertexId];
	const float side = sides[corner];
	const float t = heights[corner];

	const float4 i0 = instances[instanceId * 3 + 0];
	const float4 i1 = instances[instanceId * 3 + 1];
	const float4 i2 = instances[instanceId * 3 + 2];

	const float3 root = i0.xyz;
	const float scale = i0.w;
	const float3 terrainNormal = i1.xyz;
	const float colorSeed = i1.w;
	const float yaw = i2.x;
	const float leanBase = i2.y;
	const float windPhase = i2.z;

	const float bladeHeight = grass.bladeHeight * scale;
	const float halfWidth = grass.bladeWidth * 0.5f * scale;

	const float2 facing = float2(cos(yaw), sin(yaw));
	const float2 across = float2(-facing.y, facing.x);

	// Wind: a slow field that moves whole patches together, and a per-blade flutter on
	// top. Both drive the lean, and the lean rides t squared so the root stays planted.
	const float gust =
		sin(grass.time * 1.7f + root.x * 0.35f + root.z * 0.23f) * 0.6f +
		sin(grass.time * 3.9f + windPhase * 6.2831853f) * 0.25f;

	const float lean = leanBase + gust * grass.windStrength;

	const float bend = lean * t * t;

	const float3 position = root
		+ float3(across.x, 0.0f, across.y) * (side * halfWidth * (1.0f - t * 0.75f))
		+ float3(0.0f, 1.0f, 0.0f) * (t * bladeHeight * (1.0f - 0.3f * bend * bend))
		+ float3(facing.x, 0.0f, facing.y) * (bend * bladeHeight);

	output.position = mul(float4(position, 1.0f), viewProj);
	output.worldPosition = position;

	// Lit with the terrain's own normal rather than the blade's facets: a field reads as
	// a surface, and per-face normals on two-triangle strips read as glitter.
	output.normal = terrainNormal;
	output.blade = float2(t, colorSeed);

	return output;
}

struct GrassPSOutput
{
	float4 color    : SV_TARGET0;
	float2 velocity : SV_TARGET1;
};

GrassPSOutput PSMain(GrassVSOutput input)
{
	GrassPSOutput output;

	const float t = input.blade.x;

	// Darker at the root, where blades shadow each other; a cheap stand-in for the
	// occlusion a real canopy has.
	float3 albedo = lerp(grass.rootColor, grass.tipColor, t);
	albedo *= 0.85f + 0.3f * input.blade.y;

	const float3 N = normalize(input.normal);
	const float3 toSun = normalize(-lightDirection);

	const float ndl = saturate(dot(N, toSun));

	// Wrapped a little: a blade is thinner than the lighting model thinks, and light
	// bleeds through the edge of what a hard cosine would leave black.
	const float wrapped = ndl * 0.75f + 0.25f;

	float3 direct = albedo * lightColor * wrapped;

	direct *= shadowFactor(input.worldPosition, N, ndl);
	direct *= cloudShadowFactor(input.worldPosition);

	const float3 ambient = albedo * shIrradiance(N) / 3.14159265359f * iblIntensity;

	// The root occlusion applies to everything: down there neither sun nor sky arrives.
	const float rootOcclusion = 0.55f + 0.45f * t;

	output.color = float4((direct + ambient) * rootOcclusion, 1.0f);

	const float2 rasterUv = input.position.xy / viewportSize;
	output.velocity = computeVelocity(input.worldPosition, rasterUv);

	return output;
}
