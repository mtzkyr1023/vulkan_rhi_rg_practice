// Grass culling: one thread per grid cell decides whether its blade exists and is on
// screen, and only the survivors reach the vertex shader.
//
// Before this pass, every one of the grid's hundred-and-sixty-thousand candidate blades
// ran the full vertex shader fifteen times -- height fetch, growth rules, hash -- just to
// discover, for most of them, that they were behind the camera or on rock. Now that work
// runs once per cell here, the survivors are appended to a compact instance buffer with an
// atomic on the indirect draw's own instanceCount, and the draw that follows reads its
// arguments straight from that buffer. The CPU never learns how many blades there are,
// which is the whole point: nothing to read back, nothing to stall on.
//
// The frustum arrives as four planes -- the sides only. The four side planes meet at the
// eye, so together they also reject everything behind the camera, and the far plane is
// redundant against a field that already ends at its own radius.

#define MV_CUSTOM_PUSH_CONSTANTS
#include "common.hlsli"

// Must match CullGpuConstants in grass_field.cpp.
struct GrassCullConstants
{
	float4 planes[4];

	float3 cameraPosition;
	float  radius;

	float  worldSize;
	float  heightScale;
	float  rockHeight;
	float  rockSlope;

	float  waterLevel;
	float  density;
	float  bladeHeight;
	float  _cullPad;

	uint   fieldResolution;
	uint   bladesPerSide;
	uint2  _cullPad2;
};

#ifdef MV_TARGET_VULKAN
[[vk::push_constant]] GrassCullConstants cull;
#else
ConstantBuffer<GrassCullConstants> cull : register(b0, space9);
#endif

StructuredBuffer<float> heightField : register(t0, space0);

// Three float4s per surviving blade: root+scale, normal+colourSeed, yaw+lean+windPhase.
// Must match the reader in grass.hlsl.
RWStructuredBuffer<float4> instances : register(u1, space0);

// The indirect draw arguments: vertexCount, instanceCount, firstVertex, firstInstance.
// instanceCount is the append counter -- the atomic below is the only bookkeeping the
// whole system has.
RWStructuredBuffer<uint> drawArgs : register(u2, space0);

float2 mvGrassHash(float2 cell)
{
	float3 p = float3(cell.xy, cell.x * 0.43f + cell.y * 0.71f);
	p = frac(p * float3(0.1031f, 0.1030f, 0.0973f));
	p += dot(p, p.yzx + 33.33f);

	return frac((p.xx + p.yz) * p.zy);
}

float mvFieldHeight(float2 world)
{
	const float2 uv = saturate(world / cull.worldSize + 0.5f);

	const float2 f = uv * float(cull.fieldResolution - 1);
	const uint2 i0 = (uint2)f;
	const uint2 i1 = min(i0 + 1, cull.fieldResolution - 1);
	const float2 t = f - (float2)i0;

	const float h00 = heightField[i0.y * cull.fieldResolution + i0.x];
	const float h10 = heightField[i0.y * cull.fieldResolution + i1.x];
	const float h01 = heightField[i1.y * cull.fieldResolution + i0.x];
	const float h11 = heightField[i1.y * cull.fieldResolution + i1.x];

	return lerp(lerp(h00, h10, t.x), lerp(h01, h11, t.x), t.y);
}

[numthreads(64, 1, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
	if (id.x >= cull.bladesPerSide * cull.bladesPerSide)
		return;

	// The same cell mapping the vertex shader used to do, absolute so the hash -- and the
	// blade -- stays put while the camera drags the grid around.
	const float cellSize = (2.0f * cull.radius) / float(cull.bladesPerSide);

	const int half_ = (int)(cull.bladesPerSide / 2);
	const int2 base = (int2)floor(cull.cameraPosition.xz / cellSize);

	const int2 cell = base + int2(
		(int)(id.x % cull.bladesPerSide) - half_,
		(int)(id.x / cull.bladesPerSide) - half_);

	const float2 h0 = mvGrassHash((float2)cell);
	const float2 h1 = mvGrassHash((float2)cell + 17.31f);

	const float2 rootXZ = ((float2)cell + 0.15f + h0 * 0.7f) * cellSize;

	// Off the map there is no terrain to stand on, and the clamped height lookup would
	// repeat the edge row across the open sea.
	const float halfWorld = cull.worldSize * 0.5f - 2.0f;

	if (any(abs(rootXZ) > halfWorld))
		return;

	const float hNorm = mvFieldHeight(rootXZ);
	const float heightMetres = hNorm * cull.heightScale;

	const float spacing = cull.worldSize / float(cull.fieldResolution - 1);
	const float hX = mvFieldHeight(rootXZ + float2(spacing, 0.0f));
	const float hZ = mvFieldHeight(rootXZ + float2(0.0f, spacing));

	const float3 terrainNormal = normalize(float3(
		-(hX - hNorm) * cull.heightScale / spacing,
		1.0f,
		-(hZ - hNorm) * cull.heightScale / spacing));

	const float slope = 1.0f - saturate(terrainNormal.y);

	// The terrain bake's rock rule, inverted: what is not rock and not shore grows grass.
	const float rockFromHeight = smoothstep(cull.rockHeight - 0.10f, cull.rockHeight + 0.10f, hNorm);
	const float rockFromSlope = smoothstep(cull.rockSlope - 0.12f, cull.rockSlope + 0.12f, slope);
	const float shore = smoothstep(cull.waterLevel + 1.0f, cull.waterLevel + 4.0f, heightMetres);

	const float grow = (1.0f - max(rockFromHeight, rockFromSlope)) * shore;

	const float distance_ = length(rootXZ - cull.cameraPosition.xz);
	const float fade = 1.0f - smoothstep(cull.radius * 0.7f, cull.radius, distance_);

	// Stochastic: each cell rolls once against the local density, so the edge of a rock
	// band frays into scattered blades instead of ending on a contour line.
	if (h1.x > grow * cull.density * fade)
		return;

	// The frustum. A sphere generous enough to cover the tallest lean of the tallest
	// blade, against the four side planes.
	const float3 centre = float3(rootXZ.x, heightMetres + cull.bladeHeight, rootXZ.y);
	const float bound = cull.bladeHeight * 2.0f + cellSize;

	[unroll]
	for (uint i = 0; i < 4; i++)
	{
		if (dot(cull.planes[i].xyz, centre) + cull.planes[i].w < -bound)
			return;
	}

	// Survived: append. The counter is the indirect draw's instanceCount itself.
	uint slot;
	InterlockedAdd(drawArgs[1], 1, slot);

	const float scale = (0.6f + 0.8f * h1.y) * (0.7f + 0.6f * grow);

	instances[slot * 3 + 0] = float4(rootXZ.x, heightMetres, rootXZ.y, scale);
	instances[slot * 3 + 1] = float4(terrainNormal, h1.y);
	instances[slot * 3 + 2] = float4(h0.x * 6.2831853f, 0.12f + 0.25f * h1.y, h0.y, 0.0f);
}
