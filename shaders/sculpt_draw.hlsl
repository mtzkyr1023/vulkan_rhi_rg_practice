// Draws the GPU-meshed sculpt surface straight out of the buffer the marching
// cubes dispatch appended: no input assembler, the vertex id indexes the
// storage buffer. Shading is the prop path's -- same pbr.hlsli, same material
// buffer -- so the carved rock sits in the same light as everything else.

#define MV_CUSTOM_PUSH_CONSTANTS
#include "pbr.hlsli"

// Must match SculptDrawGpuConstants in sculpt_gpu.cpp.
struct SculptDrawConstants
{
	uint materialIndex;
	float3 _pad;
};

#ifdef MV_TARGET_VULKAN
[[vk::push_constant]] SculptDrawConstants sculpt;
#else
ConstantBuffer<SculptDrawConstants> sculpt : register(b0, space9);
#endif

// Two float4 per vertex: world position, then normal.
StructuredBuffer<float4> meshVertices : register(t0, space2);

struct VSOutput
{
	float4 position      : SV_POSITION;
	float3 worldPosition : POSITION0;
	float3 normal        : NORMAL;
};

VSOutput VSMain(uint vertexId : SV_VertexID)
{
	const float3 world = meshVertices[vertexId * 2 + 0].xyz;
	const float3 normal = meshVertices[vertexId * 2 + 1].xyz;

	VSOutput output;
	output.position = mul(float4(world, 1.0f), viewProj);
	output.worldPosition = world;
	output.normal = normal;

	return output;
}

struct SculptPSOutput
{
	float4 color    : SV_TARGET0;
	float2 velocity : SV_TARGET1;
};

SculptPSOutput PSMain(VSOutput input)
{
	SurfaceInput surface;
	surface.material = materials[sculpt.materialIndex];
	surface.uv = float2(0.0f, 0.0f);
	surface.uvDdx = float2(0.0f, 0.0f);
	surface.uvDdy = float2(0.0f, 0.0f);
	surface.worldPosition = input.worldPosition;
	surface.geometricNormal = normalize(input.normal);
	surface.positionDdx = ddx(input.worldPosition);
	surface.positionDdy = ddy(input.worldPosition);

	const float4 baseColor = sampleBaseColor(surface);

	SculptPSOutput output;
	output.color = shadeSurface(surface, baseColor);

	const float2 rasterUv = input.position.xy / viewportSize;
	output.velocity = computeVelocity(input.worldPosition, rasterUv);

	return output;
}
