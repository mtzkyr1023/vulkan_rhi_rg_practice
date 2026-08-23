
#include "virtual_texture/virtual_texture.h"

#include <algorithm>
#include <cstring>

namespace mv::vt
{
	namespace
	{
		// A page table entry, packed so the whole table is one uint per page:
		//   bits  0..7   tile x within the atlas
		//   bits  8..15  tile y
		//   bits 16..23  the index of the atlas in the bindless texture array
		//   bit  24      set when the page is resident
		//
		// 8 bits per tile coordinate is enough while an atlas is 30 tiles per side, and 8
		// bits of atlas index caps the atlas count at 256.
		u32 packEntry(u32 tileX, u32 tileY, u32 atlasIndex)
		{
			return (tileX & 0xFFu) | ((tileY & 0xFFu) << 8) | ((atlasIndex & 0xFFu) << 16) | (1u << 24);
		}

		// How many mip levels of this texture are worth virtualising: every level down to
		// the one that is a single page. Below that a level is smaller than a page and the
		// original texture serves as the tail of the chain.
		// Repeat addressing for a coordinate that may sit before or after the level.
		u32 wrap(s32 v, u32 extent)
		{
			return (u32)((v % (s32)extent + (s32)extent) % (s32)extent);
		}

		u32 virtualLevelCount(u32 width, u32 height)
		{
			if (width < kPageSize || height < kPageSize)
				return 0;

			u32 levels = 0;
			while ((width >> levels) >= kPageSize && (height >> levels) >= kPageSize)
			{
				levels++;
			}

			return levels;
		}
	}

	bool VirtualTextureSystem::initialize(const std::shared_ptr<rhi::IRHI>& rhi, material::MaterialSystem& materialSystem)
	{
		rhi_ = rhi;
		materialSystem_ = &materialSystem;

		rhi::BufferDesc infoDesc{};
		infoDesc.size = (u64)kMaxVirtualTextures * sizeof(GpuVirtualTextureInfo);
		infoDesc.usage = rhi::EBufferUsage::eStorage;
		infoDesc.memoryType = rhi::EMemoryType::eHostVisibleBuffer;

		infoBuffer_ = rhi_->createBuffer(infoDesc);

		// Zeroed rather than left as whatever the heap held: a levelCount of zero is what
		// tells the shader a texture is not virtualised, so every slot has to start there.
		const std::vector<GpuVirtualTextureInfo> emptyInfos(kMaxVirtualTextures);
		rhi_->writeBuffer(infoBuffer_, emptyInfos.data(), infoDesc.size, 0);

		rhi::BufferDesc pageTableDesc{};
		pageTableDesc.size = (u64)kMaxPageTableEntries * sizeof(u32);
		pageTableDesc.usage = rhi::EBufferUsage::eStorage;
		pageTableDesc.memoryType = rhi::EMemoryType::eHostVisibleBuffer;

		pageTableBuffer_ = rhi_->createBuffer(pageTableDesc);

		return true;
	}

	void VirtualTextureSystem::deinitialize()
	{
		if (!rhi_) return;

		if (infoBuffer_ != INVALID_HANDLE)
		{
			rhi_->freeBuffer(infoBuffer_);
			infoBuffer_ = INVALID_HANDLE;
		}

		if (pageTableBuffer_ != INVALID_HANDLE)
		{
			rhi_->freeBuffer(pageTableBuffer_);
			pageTableBuffer_ = INVALID_HANDLE;
		}

		atlases_[0].clear();
		atlases_[1].clear();
		pageTable_.clear();

		rhi_.reset();
	}

	VirtualTextureSystem::Atlas* VirtualTextureSystem::acquireAtlas(bool srgb)
	{
		auto& chain = atlases_[srgb ? 0 : 1];

		if (!chain.empty() && chain.back()->usedTiles < kTilesPerAtlas)
			return chain.back().get();

		// 8 bits of atlas index in a page table entry.
		if (stats_.atlasCount >= 256)
			return nullptr;

		rhi::TextureDesc desc{};
		desc.width = kAtlasSize;
		desc.height = kAtlasSize;
		desc.depth = 1;
		desc.usage = rhi::ETextureUsage::eSampled | rhi::ETextureUsage::eTransferDst;
		// No mip chain: the page table already selects the level, and a mip of the atlas
		// would blend across tile boundaries that have nothing to do with each other.
		desc.mipLevels = 1;
		desc.format = srgb ? rhi::ETextureFormat::eR8G8B8A8_SRGB : rhi::ETextureFormat::eR8G8B8A8_UNORM;
		desc.memoryType = rhi::EMemoryType::eDeviceLocalImage;

		auto atlas = std::make_unique<Atlas>();
		atlas->texture = rhi_->createTexture(desc);
		atlas->bindlessIndex = materialSystem_->registerTexture(atlas->texture);
		atlas->pixels.assign((size_t)kAtlasSize * kAtlasSize * 4, 0);

		stats_.atlasCount++;
		stats_.atlasBytes += (u64)kAtlasSize * kAtlasSize * 4;

		chain.push_back(std::move(atlas));

		return chain.back().get();
	}

	void VirtualTextureSystem::uploadAtlas(Atlas& atlas)
	{
		if (atlas.pixels.empty())
			return;

		const rhi::TextureUpload upload{ atlas.pixels.data(), atlas.pixels.size() };
		rhi_->uploadTexture(atlas.texture, &upload, 1);

		// 64MB per atlas, and there are several. Handing it back as each one fills keeps
		// the peak well below holding all of them at once.
		atlas.pixels.clear();
		atlas.pixels.shrink_to_fit();
	}

	void VirtualTextureSystem::blitPage(
		Atlas& atlas, u32 tileIndex,
		const u8* level, u32 levelWidth, u32 levelHeight,
		u32 pageX, u32 pageY)
	{
		const u32 tileX = tileIndex % kTilesPerSide;
		const u32 tileY = tileIndex / kTilesPerSide;

		const u32 dstOriginX = tileX * kTileSize;
		const u32 dstOriginY = tileY * kTileSize;

		// The texels of the page itself start at the border offset inside the tile, so a
		// source coordinate of -kPageBorder lands on the first row of the tile.
		const s32 srcOriginX = (s32)(pageX * kPageSize) - (s32)kPageBorder;
		const s32 srcOriginY = (s32)(pageY * kPageSize) - (s32)kPageBorder;

		for (u32 y = 0; y < kTileSize; y++)
		{
			// Wrapping rather than clamping: it makes the border correct across page seams
			// in the interior, and correct at the outer edge too for the repeat addressing
			// that a scene like Sponza uses throughout. A clamped texture picks up the
			// opposite edge instead, which is wrong along one row of texels.
			const u32 sy = wrap(srcOriginY + (s32)y, levelHeight);

			u8* dstRow = atlas.pixels.data() + ((size_t)(dstOriginY + y) * kAtlasSize + dstOriginX) * 4;
			const u8* srcRow = level + (size_t)sy * levelWidth * 4;

			// Copied as runs rather than texel by texel. A row only wraps where it crosses
			// the edge of the level, so it is at most three memcpys instead of 136.
			for (u32 x = 0; x < kTileSize; )
			{
				const u32 sx = wrap(srcOriginX + (s32)x, levelWidth);
				const u32 run = std::min(kTileSize - x, levelWidth - sx);

				memcpy(dstRow + (size_t)x * 4, srcRow + (size_t)sx * 4, (size_t)run * 4);

				x += run;
			}
		}
	}

	void VirtualTextureSystem::add(const asset::TextureSource& source)
	{
		const u32 levelCount = std::min(virtualLevelCount(source.width, source.height), (u32)source.levels.size());
		if (levelCount == 0)
			return;

		// The bindless index doubles as the virtual texture index: the shader needs both
		// the page table entry and the original texture as a fallback, and one number
		// reaches both.
		const u32 index = materialSystem_->registerTexture(source.texture);
		if (index >= kMaxVirtualTextures)
			return;

		const u32 pagesX = source.width / kPageSize;
		const u32 pagesY = source.height / kPageSize;

		// Every level is given the level-0 footprint so an entry can be addressed without
		// summing the levels below it. The unused tail of each level costs a few kilobytes.
		const u32 stride = pagesX * pagesY;

		const u32 pageTableOffset = (u32)pageTable_.size();
		if (pageTableOffset + levelCount * stride > kMaxPageTableEntries)
			return;

		pageTable_.resize((size_t)pageTableOffset + levelCount * stride, 0);

		for (u32 level = 0; level < levelCount; level++)
		{
			const u32 levelWidth = source.width >> level;
			const u32 levelHeight = source.height >> level;

			const u32 levelPagesX = levelWidth / kPageSize;
			const u32 levelPagesY = levelHeight / kPageSize;

			const u8* pixels = source.levels[level].data();

			for (u32 py = 0; py < levelPagesY; py++)
			{
				for (u32 px = 0; px < levelPagesX; px++)
				{
					Atlas* atlas = acquireAtlas(source.srgb);
					if (!atlas)
					{
						stats_.droppedPages++;
						continue;
					}

					const u32 tileIndex = atlas->usedTiles++;
					blitPage(*atlas, tileIndex, pixels, levelWidth, levelHeight, px, py);

					// The row stride is the level-0 width in pages rather than the width of
					// this level, so the shader can index with a single multiply.
					pageTable_[(size_t)pageTableOffset + level * stride + py * pagesX + px] =
						packEntry(tileIndex % kTilesPerSide, tileIndex / kTilesPerSide, atlas->bindlessIndex);

					stats_.pageCount++;

					if (atlas->usedTiles == kTilesPerAtlas)
					{
						uploadAtlas(*atlas);
					}
				}
			}
		}

		GpuVirtualTextureInfo info{};
		info.pageTableOffset = pageTableOffset;
		info.pagesX = pagesX;
		info.pagesY = pagesY;
		info.levelCount = levelCount;

		rhi_->writeBuffer(infoBuffer_, &info, sizeof(info), (u64)index * sizeof(GpuVirtualTextureInfo));

		stats_.virtualTextureCount++;
	}

	void VirtualTextureSystem::finalize()
	{
		for (auto& chain : atlases_)
		{
			for (auto& atlas : chain)
			{
				uploadAtlas(*atlas);
			}
		}

		if (!pageTable_.empty())
		{
			rhi_->writeBuffer(pageTableBuffer_, pageTable_.data(), pageTable_.size() * sizeof(u32), 0);
		}
	}
}
