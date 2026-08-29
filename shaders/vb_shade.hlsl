
// Visibility buffer resolve: a fullscreen pass that shades exactly one fragment per
// pixel. For each pixel it reads the (drawIndex, primitiveID) written by vb.hlsl, fetches
// that triangle's three vertices out of the global geometry buffers, reconstructs the
// barycentric coordinates and their screen-space derivatives analytically, and runs the
// same BRDF the forward path uses.
//
// The derivatives are the whole difficulty: there are no quads here, so ddx/ddy of a UV
// would be meaningless, and without them every texture fetch would collapse to mip 0.

#include "pbr.hlsli"

// space2 == descriptor set 2, used only by this pass. The visibility buffer is
// deliberately not in set 0, so it is never both a render target and a bound resource.
struct DrawInfo
{
	uint firstIndex;
	uint materialIndex;
	uint2 _pad;
};

StructuredBuffer<ModelVertex> vertices    : register(t0, space2);
StructuredBuffer<uint>        indices     : register(t1, space2);
StructuredBuffer<DrawInfo>    draws       : register(t2, space2);
Texture2D<uint2>              visibility  : register(t3, space2);

// One entry per bindless texture, holding the finest mip anything on screen asked for.
// This is the portable stand-in for D3D12's sampler feedback, which Vulkan has no
// equivalent of. u4 rather than u0 so its Vulkan binding number does not collide with
// the SRVs above.
RWStructuredBuffer<uint>      textureFeedback : register(u4, space2);

struct VSOutput
{
	float4 position : SV_POSITION;
	float2 uv       : TEXCOORD0;
};

// One oversized triangle rather than a quad: no diagonal seam, and the whole screen is
// covered by three vertices with no vertex buffer.
VSOutput VSMain(uint vertexId : SV_VertexID)
{
	VSOutput output;
	output.uv = float2((vertexId << 1) & 2, vertexId & 2);
	output.position = float4(output.uv * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);

	return output;
}

struct BarycentricDeriv
{
	float3 lambda;
	float3 ddx;
	float3 ddy;
};

// Perspective-correct barycentrics and their screen-space gradients, from the triangle's
// clip-space positions alone.
BarycentricDeriv calcFullBary(float4 pt0, float4 pt1, float4 pt2, float2 pixelNdc, float2 winSize)
{
	BarycentricDeriv ret = (BarycentricDeriv)0;

	float3 invW = rcp(float3(pt0.w, pt1.w, pt2.w));

	float2 ndc0 = pt0.xy * invW.x;
	float2 ndc1 = pt1.xy * invW.y;
	float2 ndc2 = pt2.xy * invW.z;

	float invDet = rcp(determinant(float2x2(ndc2 - ndc1, ndc0 - ndc1)));

	ret.ddx = float3(ndc1.y - ndc2.y, ndc2.y - ndc0.y, ndc0.y - ndc1.y) * invDet * invW;
	ret.ddy = float3(ndc2.x - ndc1.x, ndc0.x - ndc2.x, ndc1.x - ndc0.x) * invDet * invW;

	float ddxSum = dot(ret.ddx, float3(1.0f, 1.0f, 1.0f));
	float ddySum = dot(ret.ddy, float3(1.0f, 1.0f, 1.0f));

	float2 deltaVec = pixelNdc - ndc0;
	float interpInvW = invW.x + deltaVec.x * ddxSum + deltaVec.y * ddySum;
	float interpW = rcp(interpInvW);

	ret.lambda = float3(
		interpW * (invW.x + deltaVec.x * ret.ddx.x + deltaVec.y * ret.ddy.x),
		interpW * (0.0f   + deltaVec.x * ret.ddx.y + deltaVec.y * ret.ddy.y),
		interpW * (0.0f   + deltaVec.x * ret.ddx.z + deltaVec.y * ret.ddy.z));

	// From NDC units to pixels. Y is negated because the framebuffer runs downwards while
	// NDC runs upwards.
	ret.ddx *= (2.0f / winSize.x);
	ret.ddy *= (2.0f / winSize.y);
	ddxSum  *= (2.0f / winSize.x);
	ddySum  *= (2.0f / winSize.y);

	ret.ddy *= -1.0f;
	ddySum  *= -1.0f;

	float interpWddx = 1.0f / (interpInvW + ddxSum);
	float interpWddy = 1.0f / (interpInvW + ddySum);

	ret.ddx = interpWddx * (ret.lambda * interpInvW + ret.ddx) - ret.lambda;
	ret.ddy = interpWddy * (ret.lambda * interpInvW + ret.ddy) - ret.lambda;

	return ret;
}

// Returns value, d/dx, d/dy for one interpolated scalar.
float3 interpolateWithDeriv(BarycentricDeriv deriv, float v0, float v1, float v2)
{
	float3 merged = float3(v0, v1, v2);

	return float3(
		dot(merged, deriv.lambda),
		dot(merged, deriv.ddx),
		dot(merged, deriv.ddy));
}

float3 interpolate3(BarycentricDeriv deriv, float3 v0, float3 v1, float3 v2)
{
	return v0 * deriv.lambda.x + v1 * deriv.lambda.y + v2 * deriv.lambda.z;
}

// Discrete colours so the boundary between mip levels is obvious rather than a gradient.
float3 mipLevelColor(float lod)
{
	const float3 palette[8] =
	{
		float3(1.0f, 0.0f, 0.0f),
		float3(1.0f, 0.5f, 0.0f),
		float3(1.0f, 1.0f, 0.0f),
		float3(0.0f, 1.0f, 0.0f),
		float3(0.0f, 1.0f, 1.0f),
		float3(0.0f, 0.4f, 1.0f),
		float3(0.6f, 0.0f, 1.0f),
		float3(1.0f, 1.0f, 1.0f),
	};

	return palette[clamp((int)lod, 0, 7)];
}

// The level the hardware would pick for these gradients, derived the same way it does.
float textureLod(uint textureIndex, float2 uvDdx, float2 uvDdy)
{
	float2 size;
	textures[NonUniformResourceIndex(textureIndex)].GetDimensions(size.x, size.y);

	float2 dx = uvDdx * size;
	float2 dy = uvDdy * size;

	return 0.5f * log2(max(dot(dx, dx), dot(dy, dy)));
}

// Two targets: the shaded colour, and where this pixel was on screen last frame.
//
// Velocity could be derived from depth alone while every mesh is static, which is what
// the temporal pass used to do. Writing it out instead moves the question from "where was
// this point in space" to "where was this surface", which is the only form that survives
// anything animated: a skinned or moving mesh occupies a world position this frame that
// belonged to something else in the last one.
struct ShadeOutput
{
	float4 color    : SV_TARGET0;
	float2 velocity : SV_TARGET1;
};

static float2 gVelocity;

ShadeOutput makeOutput(float4 color)
{
	ShadeOutput output;
	output.color = color;
	output.velocity = gVelocity;

	return output;
}

ShadeOutput PSMain(VSOutput input)
{
	int2 pixel = int2(input.position.xy);
	uint2 packed = visibility.Load(int3(pixel, 0));

	// Zero means the visibility buffer was never written here.
	if (packed.x == 0)
	{
		discard;
	}

	uint drawIndex = packed.x - 1;
	uint primitiveID = packed.y;

	DrawInfo draw = draws[drawIndex];

	uint base = draw.firstIndex + primitiveID * 3;
	ModelVertex v0 = vertices[indices[base + 0]];
	ModelVertex v1 = vertices[indices[base + 1]];
	ModelVertex v2 = vertices[indices[base + 2]];

	// Positions are already in world space, so this is the same transform the raster pass
	// applied and the clip-space triangle is reproduced exactly.
	float4 clip0 = mul(float4(v0.position, 1.0f), viewProj);
	float4 clip1 = mul(float4(v1.position, 1.0f), viewProj);
	float4 clip2 = mul(float4(v2.position, 1.0f), viewProj);

	float2 winSize;
	visibility.GetDimensions(winSize.x, winSize.y);

	// Pixel centre to NDC, flipping Y back the way clip space expects it.
	float2 pixelNdc = ((input.position.xy / winSize) * 2.0f - 1.0f) * float2(1.0f, -1.0f);

	BarycentricDeriv bary = calcFullBary(clip0, clip1, clip2, pixelNdc, winSize);

	float3 u = interpolateWithDeriv(bary, v0.uv.x, v1.uv.x, v2.uv.x);
	float3 v = interpolateWithDeriv(bary, v0.uv.y, v1.uv.y, v2.uv.y);

	SurfaceInput surface;
	surface.material = materials[draw.materialIndex];
	surface.uv = float2(u.x, v.x);
	surface.uvDdx = float2(u.y, v.y);
	surface.uvDdy = float2(u.z, v.z);
	surface.worldPosition = interpolate3(bary, v0.position, v1.position, v2.position);
	surface.geometricNormal = interpolate3(bary, v0.normal, v1.normal, v2.normal);

	// Position gradients for the tangent frame, from the same barycentric derivatives.
	float3 px = interpolateWithDeriv(bary, v0.position.x, v1.position.x, v2.position.x);
	float3 py = interpolateWithDeriv(bary, v0.position.y, v1.position.y, v2.position.y);
	float3 pz = interpolateWithDeriv(bary, v0.position.z, v1.position.z, v2.position.z);

	surface.positionDdx = float3(px.y, py.y, pz.y);
	surface.positionDdy = float3(px.z, py.z, pz.z);

	// Set once, for every path out of this shader including the debug views, so the
	// temporal pass sees a complete velocity buffer whatever is being displayed.
	gVelocity = computeVelocity(surface.worldPosition, input.position.xy / winSize);

	// Report the mip this pixel would like for its base colour map. InterlockedMin keeps
	// the finest request across every pixel that touched the texture this frame.
	{
		// GetDimensions reports the size of the view's own level 0, so a texture whose
		// finest levels are not resident reports a level relative to what it does have.
		// Adding the base mip back makes the request absolute, which is the only form a
		// streaming system can act on: it has to know which level to fetch, not how many
		// more it would like than it was given.
		float lod = textureLod(surface.material.baseColorTexture, surface.uvDdx, surface.uvDdy) + forcedBaseMip;

		uint previous;
		InterlockedMin(textureFeedback[surface.material.baseColorTexture], (uint)clamp(lod, 0.0f, 15.0f), previous);
	}

	// The debug views deliberately skip tone mapping and the sRGB encode so what reaches
	// the screen is the raw value being inspected.
	switch (debugMode)
	{
	case MV_DEBUG_DRAW_ID:
		return makeOutput(float4(debugIdColor(drawIndex), 1.0f));

	case MV_DEBUG_PRIMITIVE_ID:
		return makeOutput(float4(debugIdColor(primitiveID), 1.0f));

	case MV_DEBUG_MATERIAL_ID:
		return makeOutput(float4(debugIdColor(draw.materialIndex), 1.0f));

	// Should read as a smooth red/green/blue gradient across every triangle. Banding or
	// discontinuities inside a triangle mean the reconstruction is wrong.
	case MV_DEBUG_BARYCENTRIC:
		return makeOutput(float4(bary.lambda, 1.0f));

	case MV_DEBUG_UV:
		return makeOutput(float4(frac(surface.uv), 0.0f, 1.0f));

	case MV_DEBUG_NORMAL:
		return makeOutput(float4(normalize(surface.geometricNormal) * 0.5f + 0.5f, 1.0f));

	// Checks the analytic gradients: without them every surface would show level 0.
	case MV_DEBUG_MIP_LEVEL:
		return makeOutput(float4(mipLevelColor(textureLod(surface.material.baseColorTexture, surface.uvDdx, surface.uvDdy)), 1.0f));

	// Which virtual page each pixel resolves to. Should read as a grid that stays glued to
	// the surface, and the cells should quadruple in size at every level boundary.
	case MV_DEBUG_VT_PAGE:
	{
		uint level;
		uint2 page = virtualDebugPage(surface.material.baseColorTexture, surface.uv, surface.uvDdx, surface.uvDdy, level);

		// Black where the sample fell through to the source texture instead.
		if (level == 0xFFFFFFFFu)
			return makeOutput(float4(0.0f, 0.0f, 0.0f, 1.0f));

		return makeOutput(float4(debugIdColor(page.x * 71u + page.y * 3u + level * 7919u), 1.0f));
	}

	// The virtualised level actually used, in the same palette as the mip view, so the two
	// can be flipped between to check that virtual selection matches the hardware.
	case MV_DEBUG_VT_LEVEL:
	{
		uint level;
		virtualDebugPage(surface.material.baseColorTexture, surface.uv, surface.uvDdx, surface.uvDdy, level);

		if (level == 0xFFFFFFFFu)
			return makeOutput(float4(0.0f, 0.0f, 0.0f, 1.0f));

		return makeOutput(float4(mipLevelColor((float)level), 1.0f));
	}

	// Which cascade each pixel falls into. The bands should sit at increasing distances
	// and stay put as the camera moves, not swim about.
	case MV_DEBUG_CASCADE:
		return makeOutput(float4(cascadeDebugColor(surface.worldPosition), 1.0f));

	default:
		break;
	}

	float4 baseColor = sampleBaseColor(surface);

	return makeOutput(shadeSurface(surface, baseColor));
}
