// Marching cubes on the GPU: one thread per cell, triangles appended to a
// vertex buffer with an atomic counter that doubles as the indirect draw's
// vertex count -- the same append-and-draw-indirect shape the grass cull uses.
//
// The tables ride in a storage buffer uploaded once from the CPU tables (one
// copy of the data, two consumers): ints [0, 256) are the edge table, and
// [256, 256 + 256*16) the triangle table, flattened row-major.

// Must match SculptMcGpuConstants in sculpt_gpu.cpp.
struct SculptMcConstants
{
	float3 origin;
	float cellSize;

	uint cells;
	uint maxVertices;
	float2 _pad;
};

#ifdef MV_TARGET_VULKAN
[[vk::push_constant]] SculptMcConstants mc;
#else
ConstantBuffer<SculptMcConstants> mc : register(b0, space9);
#endif

StructuredBuffer<float> density : register(t0, space0);
StructuredBuffer<int> tables : register(t1, space0);
RWStructuredBuffer<float4> vertices : register(u2, space0);
RWStructuredBuffer<uint> drawArgs : register(u3, space0);

static const int3 kCornerOffset[8] =
{
	int3(0, 0, 0), int3(1, 0, 0), int3(1, 1, 0), int3(0, 1, 0),
	int3(0, 0, 1), int3(1, 0, 1), int3(1, 1, 1), int3(0, 1, 1),
};

static const int2 kEdgeCorners[12] =
{
	int2(0, 1), int2(1, 2), int2(2, 3), int2(3, 0),
	int2(4, 5), int2(5, 6), int2(6, 7), int2(7, 4),
	int2(0, 4), int2(1, 5), int2(2, 6), int2(3, 7),
};

float densityAt(int3 c)
{
	const int corners = (int)mc.cells + 1;

	c = clamp(c, int3(0, 0, 0), int3(corners - 1, corners - 1, corners - 1));

	return density[(c.z * corners + c.y) * corners + c.x];
}

float sampleDensity(float3 g)
{
	const int3 base = int3(floor(g));
	const float3 f = g - float3(base);

	const float c00 = lerp(densityAt(base), densityAt(base + int3(1, 0, 0)), f.x);
	const float c10 = lerp(densityAt(base + int3(0, 1, 0)), densityAt(base + int3(1, 1, 0)), f.x);
	const float c01 = lerp(densityAt(base + int3(0, 0, 1)), densityAt(base + int3(1, 0, 1)), f.x);
	const float c11 = lerp(densityAt(base + int3(0, 1, 1)), densityAt(base + int3(1, 1, 1)), f.x);

	return lerp(lerp(c00, c10, f.y), lerp(c01, c11, f.y), f.z);
}

float3 gradientAt(float3 g)
{
	const float h = 0.75f;

	return float3(
		sampleDensity(g + float3(h, 0, 0)) - sampleDensity(g - float3(h, 0, 0)),
		sampleDensity(g + float3(0, h, 0)) - sampleDensity(g - float3(0, h, 0)),
		sampleDensity(g + float3(0, 0, h)) - sampleDensity(g - float3(0, 0, h)));
}

[numthreads(4, 4, 4)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
	if (id.x >= mc.cells || id.y >= mc.cells || id.z >= mc.cells)
		return;

	float corner[8];
	uint cubeIndex = 0;

	for (uint i = 0; i < 8; i++)
	{
		corner[i] = densityAt(int3(id) + kCornerOffset[i]);

		if (corner[i] < 0.0f)
			cubeIndex |= 1u << i;
	}

	const int edges = tables[cubeIndex];

	if (edges == 0)
		return;

	float3 edgeVertex[12];

	for (int e = 0; e < 12; e++)
	{
		if (((edges >> e) & 1) == 0)
			continue;

		const int a = kEdgeCorners[e].x;
		const int b = kEdgeCorners[e].y;

		const float t = corner[a] / (corner[a] - corner[b]);

		edgeVertex[e] = float3(int3(id) + kCornerOffset[a]) +
			t * float3(kCornerOffset[b] - kCornerOffset[a]);
	}

	for (uint tri = 0; tri < 16; tri += 3)
	{
		const int e0 = tables[256 + cubeIndex * 16 + tri];

		if (e0 < 0)
			break;

		const int e1 = tables[256 + cubeIndex * 16 + tri + 1];
		const int e2 = tables[256 + cubeIndex * 16 + tri + 2];

		uint base;
		InterlockedAdd(drawArgs[0], 3, base);

		// Past the budget the count keeps climbing but nothing is written; the
		// overflow vertices read back as zeros, which rasterise as degenerate.
		if (base + 3 > mc.maxVertices)
			break;

		const int triEdges[3] = { e0, e1, e2 };

		for (uint v = 0; v < 3; v++)
		{
			const float3 grid = edgeVertex[triEdges[v]];

			const float3 gradient = gradientAt(grid);
			const float len = length(gradient);
			const float3 normal = len > 1e-6f ? -gradient / len : float3(0.0f, 1.0f, 0.0f);

			const float3 world = mc.origin + grid * mc.cellSize;

			vertices[(base + v) * 2 + 0] = float4(world, 0.0f);
			vertices[(base + v) * 2 + 1] = float4(normal, 0.0f);
		}
	}
}
