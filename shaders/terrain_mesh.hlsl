// Turns the height buffer into vertices and indices.
//
// One thread per vertex. The normal is differenced from the heightmap rather than from the
// neighbouring vertices, because the heightmap is the finer of the two: at 257 vertices
// over a 1025 field, differencing the mesh would throw away every octave the mesh cannot
// resolve, and the lighting would lose exactly the detail the baked normal map restores.

#define MV_CUSTOM_PUSH_CONSTANTS
#include "common.hlsli"
#include "terrain.hlsli"

#ifdef MV_TARGET_VULKAN
[[vk::push_constant]] TerrainConstants terrain;
#else
ConstantBuffer<TerrainConstants> terrain : register(b0, space9);
#endif

// Must match asset::ModelVertex. 32 bytes, and the visibility buffer's resolve pass reads
// the same layout out of the same buffer, so the two cannot drift.
struct TerrainVertex
{
	float3 position;
	float3 normal;
	float2 uv;
};

StructuredBuffer<float>        heights  : register(t0, space0);
RWStructuredBuffer<TerrainVertex> vertices : register(u1, space0);
RWStructuredBuffer<uint>          indices  : register(u2, space0);

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
	const uint resolution = terrain.resolution;

	if (id.x >= resolution || id.y >= resolution)
		return;

	const uint quads = resolution - 1;

	const float half = terrain.worldSize * 0.5f;
	const float step = terrain.worldSize / float(quads);

	const float u = float(id.x) / float(quads);
	const float v = float(id.y) / float(quads);

	// Differenced at vertex spacing, so the four taps are the neighbouring vertices' own
	// heights and the result is the average of the slopes of the triangles that meet here
	// -- the definition of a smooth-shaded vertex normal on a height grid.
	//
	// Sampling the field at its own spacing instead, which is four times finer than the
	// vertices at the default settings, makes the normal an alias of detail the triangles
	// cannot represent: neighbouring vertices pick up unrelated points of a high-frequency
	// signal and the interpolation between them is noise. That detail is not lost, it just
	// belongs in the normal map, and the bake puts it there.
	const float vertexStep = 1.0f / float(quads);

	const float h  = mvSampleHeight(heights, terrain.fieldSize, u, v) * terrain.heightScale;

	const float hL = mvSampleHeight(heights, terrain.fieldSize, u - vertexStep, v) * terrain.heightScale;
	const float hR = mvSampleHeight(heights, terrain.fieldSize, u + vertexStep, v) * terrain.heightScale;
	const float hD = mvSampleHeight(heights, terrain.fieldSize, u, v - vertexStep) * terrain.heightScale;
	const float hU = mvSampleHeight(heights, terrain.fieldSize, u, v + vertexStep) * terrain.heightScale;

	const float spacing = vertexStep * terrain.worldSize;

	// The cross product of the two tangents, written out: the tangent along x is
	// (2*spacing, hR - hL, 0) and along z is (0, hU - hD, 2*spacing).
	const float3 normal = normalize(float3(-(hR - hL), 2.0f * spacing, -(hU - hD)));

	TerrainVertex vertex;
	vertex.position = float3(-half + float(id.x) * step, h, -half + float(id.y) * step);
	vertex.normal = normal;
	vertex.uv = float2(u, v);

	vertices[id.y * resolution + id.x] = vertex;

	// The same thread emits the quad whose top-left corner this vertex is, which is every
	// vertex except the last row and column.
	if (id.x < quads && id.y < quads)
	{
		const uint topLeft = id.y * resolution + id.x;
		const uint topRight = topLeft + 1;
		const uint bottomLeft = topLeft + resolution;
		const uint bottomRight = bottomLeft + 1;

		const uint base = (id.y * quads + id.x) * 6;

		// Clockwise in a left-handed, y-up frame, matching the winding loaded models use.
		indices[base + 0] = topLeft;
		indices[base + 1] = bottomLeft;
		indices[base + 2] = topRight;

		indices[base + 3] = topRight;
		indices[base + 4] = bottomLeft;
		indices[base + 5] = bottomRight;
	}
}
