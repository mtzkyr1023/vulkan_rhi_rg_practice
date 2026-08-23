
// Software virtual texturing: virtual UV -> page table -> physical atlas UV.
//
// A "virtual texture" here is one ordinary source texture that has been sliced into pages
// and scattered across a small set of atlases. Its virtual texture index is the same
// number as its bindless index, so one material field reaches both the page table and the
// original texture.
//
// The original texture is kept and used as the tail of the mip chain. Below one page the
// levels are smaller than a page and are not worth virtualising, and anything that fails
// to resolve falls back to it as well, so a missing page degrades to the right colour
// rather than to nothing.

#ifndef _MV_VT_HLSLI_
#define _MV_VT_HLSLI_

#include "common.hlsli"

// Must match virtual_texture.h.
#define MV_VT_PAGE_SIZE   128.0f
#define MV_VT_BORDER      8.0f
#define MV_VT_TILE_SIZE   144.0f
#define MV_VT_ATLAS_SIZE  4096.0f

// The physical page an entry names, unpacked from the single uint the table stores.
struct PageLocation
{
	uint2 tile;
	uint  atlas;
	bool  resident;
};

PageLocation unpackPage(uint entry)
{
	PageLocation page;
	page.tile = uint2(entry & 0xFFu, (entry >> 8) & 0xFFu);
	page.atlas = (entry >> 16) & 0xFFu;
	page.resident = (entry >> 24) != 0u;

	return page;
}

// The mip the gradients ask for, in the virtual texture's own level numbering. Identical
// to what the hardware would compute for the source texture, which is the point: the
// virtualised path has to choose the same level the direct path would have.
float virtualLod(VirtualTextureInfo vt, float2 uvDdx, float2 uvDdy)
{
	float2 virtualSize = float2(vt.pagesX, vt.pagesY) * MV_VT_PAGE_SIZE;

	float2 dx = uvDdx * virtualSize;
	float2 dy = uvDdy * virtualSize;

	return 0.5f * log2(max(dot(dx, dx), dot(dy, dy)));
}

// Translates a virtual coordinate into the atlas. Returns false when the page is not
// resident, leaving the caller to fall back.
bool virtualToAtlas(VirtualTextureInfo vt, float2 uv, uint level, out float2 atlasUv, out uint atlasIndex, out float2 gradientScale)
{
	atlasUv = 0.0f.xx;
	atlasIndex = 0;
	gradientScale = 0.0f.xx;

	uint pagesX = max(vt.pagesX >> level, 1u);
	uint pagesY = max(vt.pagesY >> level, 1u);

	// Repeat addressing is applied here rather than by the sampler: a tile holds texels
	// from one page only, so the sampler has to be clamped and the wrap has to happen in
	// the virtual coordinate space. frac handles negative UVs the way repeat should.
	float2 wrapped = frac(uv);

	float2 pageCoord = wrapped * float2(pagesX, pagesY);
	uint2 page = min((uint2)pageCoord, uint2(pagesX - 1u, pagesY - 1u));

	// Where inside the page the sample lands, in [0, 1).
	float2 inPage = pageCoord - float2(page);

	// The row stride is the level-0 page count, not this level's, so that every level of a
	// virtual texture occupies the same footprint and can be indexed without a running sum.
	uint stride = vt.pagesX * vt.pagesY;
	uint entry = vtPageTable[vt.pageTableOffset + level * stride + page.y * vt.pagesX + page.x];

	PageLocation location = unpackPage(entry);
	if (!location.resident)
		return false;

	float2 texel = float2(location.tile) * MV_VT_TILE_SIZE + MV_VT_BORDER + inPage * MV_VT_PAGE_SIZE;

	atlasUv = texel / MV_VT_ATLAS_SIZE;
	atlasIndex = location.atlas;

	// d(atlasUv)/d(uv). The frac above is a discontinuity, but it does not appear here:
	// the derivative of the page-local coordinate with respect to uv is the same on both
	// sides of a page boundary, so the gradients stay continuous across the seam.
	gradientScale = float2(pagesX, pagesY) * MV_VT_PAGE_SIZE / MV_VT_ATLAS_SIZE;

	return true;
}

// One level of a virtual texture. Returns false when the page is not resident, leaving the
// caller to fall back to the source.
//
// The atlas has no mip chain, so the level comes from the page table and the sampler only
// filters within it. SampleGrad rather than SampleLevel because a mipless texture still
// filters anisotropically, which is most of what a scene like Sponza needs; the eight
// texel border bounds that at roughly 8:1 before a tap reads the neighbouring tile.
bool sampleVirtualLevel(VirtualTextureInfo vt, float2 uv, float2 uvDdx, float2 uvDdy, uint level, out float4 result)
{
	result = 0.0f.xxxx;

	float2 atlasUv;
	uint atlasIndex;
	float2 gradientScale;

	if (!virtualToAtlas(vt, uv, level, atlasUv, atlasIndex, gradientScale))
		return false;

	// Clamped, because the wrap already happened in virtual space and letting the sampler
	// repeat would send an edge tap to the far side of the atlas.
	SamplerState atlasSampler = samplers[MV_SAMPLER_LINEAR_CLAMP];

	result = textures[NonUniformResourceIndex(atlasIndex)]
		.SampleGrad(atlasSampler, atlasUv, uvDdx * gradientScale, uvDdy * gradientScale);

	return true;
}

// Samples through the page table, falling back to the source texture when the coordinate
// lands outside what the atlas holds.
float4 sampleVirtual(uint textureIndex, float2 uv, float2 uvDdx, float2 uvDdy)
{
	SamplerState sourceSampler = samplers[MV_SAMPLER_LINEAR_REPEAT];

	if (!vtEnabled)
		return textures[NonUniformResourceIndex(textureIndex)].SampleGrad(sourceSampler, uv, uvDdx, uvDdy);

	VirtualTextureInfo vt = vtInfos[NonUniformResourceIndex(textureIndex)];
	if (vt.levelCount == 0u)
		return textures[NonUniformResourceIndex(textureIndex)].SampleGrad(sourceSampler, uv, uvDdx, uvDdy);

	float lod = virtualLod(vt, uvDdx, uvDdy);

	// Past the last virtualised level the source texture is the rest of the chain, and it
	// still has its own mips, so handing it the original gradients gives the right result.
	if (lod > (float)(vt.levelCount - 1u))
		return textures[NonUniformResourceIndex(textureIndex)].SampleGrad(sourceSampler, uv, uvDdx, uvDdy);

	// Two lookups and a blend, because the hardware would have blended between two mips
	// here and picking the nearer single level shows as banding wherever the level changes.
	// A mipped atlas cannot do this for us: its mips would filter across tile boundaries.
	lod = max(lod, 0.0f);

	uint level0 = (uint)floor(lod);
	uint level1 = min(level0 + 1u, vt.levelCount - 1u);

	float4 sample0;
	float4 sample1;

	if (!sampleVirtualLevel(vt, uv, uvDdx, uvDdy, level0, sample0) ||
		!sampleVirtualLevel(vt, uv, uvDdx, uvDdy, level1, sample1))
	{
		return textures[NonUniformResourceIndex(textureIndex)].SampleGrad(sourceSampler, uv, uvDdx, uvDdy);
	}

	return lerp(sample0, sample1, frac(lod));
}

// Which page a coordinate resolves to, for the debug views. Returns level 0xFFFFFFFF when
// the sample fell through to the source texture.
uint2 virtualDebugPage(uint textureIndex, float2 uv, float2 uvDdx, float2 uvDdy, out uint level)
{
	level = 0xFFFFFFFFu;

	VirtualTextureInfo vt = vtInfos[NonUniformResourceIndex(textureIndex)];
	if (vt.levelCount == 0u)
		return uint2(0, 0);

	float lod = virtualLod(vt, uvDdx, uvDdy);
	if (lod > (float)(vt.levelCount - 1u))
		return uint2(0, 0);

	level = (uint)clamp(round(lod), 0.0f, (float)(vt.levelCount - 1u));

	uint pagesX = max(vt.pagesX >> level, 1u);
	uint pagesY = max(vt.pagesY >> level, 1u);

	float2 pageCoord = frac(uv) * float2(pagesX, pagesY);

	return min((uint2)pageCoord, uint2(pagesX - 1u, pagesY - 1u));
}

#endif
