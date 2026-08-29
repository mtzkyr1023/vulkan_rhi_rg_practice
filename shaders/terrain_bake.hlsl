// Bakes the terrain's base colour, normal and metallic-roughness maps.
//
// All three come out of the same texel's height, slope and two extra noise fields, so they
// are written by one pass rather than three: the heightmap taps dominate the cost and
// sharing them across the three outputs is most of the saving over doing this on the CPU.

#define MV_CUSTOM_PUSH_CONSTANTS
#include "common.hlsli"
#include "terrain.hlsli"

#ifdef MV_TARGET_VULKAN
[[vk::push_constant]] TerrainConstants terrain;
#else
ConstantBuffer<TerrainConstants> terrain : register(b0, space9);
#endif

StructuredBuffer<float> heights : register(t0, space0);

// rgba8 rather than the default rgba32f: SPIR-V decorates a storage image with the format
// it is accessed as, and a mismatch against the view makes every access undefined.
#ifdef MV_TARGET_VULKAN
[[vk::image_format("rgba8")]] RWTexture2D<float4> baseColorMap : register(u1, space0);
[[vk::image_format("rgba8")]] RWTexture2D<float4> normalMap    : register(u2, space0);
[[vk::image_format("rgba8")]] RWTexture2D<float4> roughnessMap : register(u3, space0);
#else
RWTexture2D<float4> baseColorMap : register(u1, space0);
RWTexture2D<float4> normalMap    : register(u2, space0);
RWTexture2D<float4> roughnessMap : register(u3, space0);
#endif

// The two decoration fields. Both far above the terrain's own frequency: one breaks up the
// flat colour a pure height-and-slope blend would give, the other roughens the baked normal
// at a scale the mesh has no vertices for.
//
// Mapped from the raw [-1, 1] rather than rescaled to the sampled extremes the way the
// heightmap is. Two more reductions to normalise a field that only ever contributes a few
// percent of a colour is not worth the dispatches, and an absolute mapping is arguably the
// better answer anyway -- it does not change when the seed does.
float tintField(float u, float v)
{
	NoiseParams p = terrain.noise;
	p.basis = MV_NOISE_BASIS_SIMPLEX;
	p.fractal = MV_NOISE_FRACTAL_FBM;
	p.frequency = 24.0f;
	p.octaves = 4;
	p.warpStrength = 0.0f;
	p.seed = terrain.noise.seed + 0x2545f491u;

	return saturate(mvNoiseSample(p, u, v) * 0.5f + 0.5f);
}

float detailField(float u, float v)
{
	NoiseParams p = terrain.noise;
	p.basis = MV_NOISE_BASIS_WORLEY;
	p.fractal = MV_NOISE_FRACTAL_BILLOW;
	p.frequency = 48.0f;
	p.octaves = 3;
	p.warpStrength = 0.0f;
	p.seed = terrain.noise.seed + 0x68e31da4u;

	return saturate(mvNoiseSample(p, u, v) * 0.5f + 0.5f);
}

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
	const uint size = terrain.textureSize;

	if (id.x >= size || id.y >= size)
		return;

	const float texelStep = 1.0f / float(size);
	const float spacing = texelStep * terrain.worldSize;

	const float u = (float(id.x) + 0.5f) * texelStep;
	const float v = (float(id.y) + 0.5f) * texelStep;

	const float height = mvSampleHeight(heights, terrain.fieldSize, u, v);

	const float hL = mvSampleHeight(heights, terrain.fieldSize, u - texelStep, v) * terrain.heightScale;
	const float hR = mvSampleHeight(heights, terrain.fieldSize, u + texelStep, v) * terrain.heightScale;
	const float hD = mvSampleHeight(heights, terrain.fieldSize, u, v - texelStep) * terrain.heightScale;
	const float hU = mvSampleHeight(heights, terrain.fieldSize, u, v + texelStep) * terrain.heightScale;

	// The detail field displaces the surface a few centimetres before the gradient is
	// taken, which is where the fine grain in the normal map comes from.
	const float detailScale = 0.35f;
	const float dL = detailField(u - texelStep, v) * detailScale;
	const float dR = detailField(u + texelStep, v) * detailScale;
	const float dD = detailField(u, v - texelStep) * detailScale;
	const float dU = detailField(u, v + texelStep) * detailScale;

	// The surface as the heightmap and the detail field describe it, at texel scale.
	const float slopeFineX = ((hR + dR) - (hL + dL)) / (2.0f * spacing);
	const float slopeFineZ = ((hU + dU) - (hD + dD)) / (2.0f * spacing);

	const float3 fineNormal = normalize(float3(-slopeFineX, 1.0f, -slopeFineZ));

	// How far from flat, as 0 at horizontal and 1 at vertical. Taken from the fine surface,
	// because whether a spot is rock is a question about the surface, not about how well
	// the mesh happens to resolve it.
	const float slope = 1.0f - saturate(fineNormal.y);

	const float tint = tintField(u, v);

	// --- base colour -------------------------------------------------------------

	const float3 kSand  = float3(0.76f, 0.70f, 0.50f);
	const float3 kGrass = float3(0.24f, 0.38f, 0.16f);
	const float3 kRock  = float3(0.38f, 0.35f, 0.32f);
	const float3 kSnow  = float3(0.90f, 0.92f, 0.95f);

	const float waterFraction = (terrain.heightScale > 0.0f) ? terrain.waterHeight / terrain.heightScale : 0.0f;

	float3 color = lerp(kSand, kGrass, smoothstep(waterFraction, waterFraction + 0.06f, height));

	// Height and slope both push towards rock, and the stronger of the two wins. Taking
	// the max rather than adding them stops a moderately steep slope high up from counting
	// twice.
	const float rockFromHeight = smoothstep(
		terrain.rockHeight - 0.10f,
		terrain.rockHeight + 0.10f,
		height + (tint - 0.5f) * 0.10f);

	const float rockFromSlope = smoothstep(terrain.rockSlope - 0.12f, terrain.rockSlope + 0.12f, slope);

	color = lerp(color, kRock, max(rockFromHeight, rockFromSlope));

	// Snow settles on what is flat enough to hold it, so the slope term takes it away again.
	const float snowAmount = smoothstep(terrain.snowHeight - 0.08f, terrain.snowHeight + 0.08f, height)
		* (1.0f - smoothstep(0.35f, 0.60f, slope));

	color = lerp(color, kSnow, snowAmount);

	// A little of the tint field everywhere, so no region is ever a single flat colour.
	color *= 0.86f + tint * 0.28f;

	// No sRGB encode. The palette is authored in sRGB space -- the CPU bake wrote these
	// same numbers straight into an sRGB texture, and writing them through the UNORM view a
	// UAV requires stores the identical bits. Encoding here would brighten the whole map.
	baseColorMap[id.xy] = float4(saturate(color), 1.0f);

	// --- normal ------------------------------------------------------------------

	// A tangent-space map has to carry the difference between the real surface and the one
	// the mesh already has, not the surface itself. Writing the world normal here and
	// letting the shader rotate it by the tangent frame applied every slope twice: flat
	// ground came out right and a hillside came out at double its pitch, which is what put
	// the black patches on the slopes.
	//
	// So the mesh-scale gradient is taken again -- at vertex spacing, exactly as
	// terrain_mesh.hlsl does -- and subtracted off. What is left is the detail, which is
	// what tangent space is for.
	const float vertexStep = 1.0f / float(max(terrain.resolution - 1u, 1u));
	const float vertexSpacing = vertexStep * terrain.worldSize;

	const float cL = mvSampleHeight(heights, terrain.fieldSize, u - vertexStep, v) * terrain.heightScale;
	const float cR = mvSampleHeight(heights, terrain.fieldSize, u + vertexStep, v) * terrain.heightScale;
	const float cD = mvSampleHeight(heights, terrain.fieldSize, u, v - vertexStep) * terrain.heightScale;
	const float cU = mvSampleHeight(heights, terrain.fieldSize, u, v + vertexStep) * terrain.heightScale;

	const float slopeCoarseX = (cR - cL) / (2.0f * vertexSpacing);
	const float slopeCoarseZ = (cU - cD) / (2.0f * vertexSpacing);

	// x runs along world x and y along world z, because that is how the terrain's uv is
	// laid out; z is the mesh normal. A residual of zero -- the mesh already describing the
	// surface exactly, which is what happens when the field and the grid are the same
	// resolution -- gives (0, 0, 1) and perturbs nothing.
	const float3 tangentNormal = normalize(float3(
		-(slopeFineX - slopeCoarseX),
		-(slopeFineZ - slopeCoarseZ),
		1.0f));

	normalMap[id.xy] = float4(tangentNormal * 0.5f + 0.5f, 1.0f);

	// --- metallic-roughness -------------------------------------------------------

	// glTF packs roughness in green and metallic in blue. None of this is metal.
	const float rockRoughness = lerp(0.95f, 0.62f, max(rockFromHeight, rockFromSlope));
	const float surfaceRoughness = lerp(rockRoughness, 0.32f, snowAmount);

	roughnessMap[id.xy] = float4(1.0f, saturate(surfaceRoughness + (tint - 0.5f) * 0.08f), 0.0f, 1.0f);
}
