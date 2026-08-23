
#ifndef _MV_VIRTUAL_TEXTURE_H_
#define _MV_VIRTUAL_TEXTURE_H_

#include <memory>
#include <vector>

#include "asset/gltf_loader.h"
#include "material/material_system.h"

#include "rhi/rhi.h"

#include "util/types.h"

namespace mv
{
	namespace vt
	{
		using namespace types;

		// Every one of these has a twin in vt.hlsli and the two must agree.
		//
		// A page is the unit of residency. The border exists because neighbouring pages in
		// the atlas come from unrelated textures, so a bilinear or anisotropic tap near a
		// page edge would otherwise read a stranger's texels. Eight texels bounds the
		// anisotropy this can support at roughly 8:1; beyond that the border runs out and
		// the tap bleeds. It is not free, but at this page size it costs no extra atlas:
		// 28 tiles per side instead of 30 still covers the scene in the same eight.
		constexpr u32 kPageSize = 128;
		constexpr u32 kPageBorder = 8;
		constexpr u32 kTileSize = kPageSize + 2 * kPageBorder;

		// 4096 divided by a 144 texel tile leaves 28 tiles per side and 64 texels of slack.
		constexpr u32 kAtlasSize = 4096;
		constexpr u32 kTilesPerSide = kAtlasSize / kTileSize;
		constexpr u32 kTilesPerAtlas = kTilesPerSide * kTilesPerSide;

		// Sized for the whole bindless array, so a virtual texture can be looked up by the
		// same index that selects its fallback texture.
		constexpr u32 kMaxVirtualTextures = 4096;

		// 68 Sponza textures need 17k entries. A megabyte of them costs 4MB and removes the
		// question.
		constexpr u32 kMaxPageTableEntries = 1u << 20;

		// One virtual texture, indexed by its bindless texture index. levelCount == 0 means
		// the texture was never virtualised and the shader should sample it directly.
		struct GpuVirtualTextureInfo
		{
			u32 pageTableOffset = 0;
			u32 pagesX = 0;
			u32 pagesY = 0;
			u32 levelCount = 0;
		};

		// Software virtual texturing: a page table in a buffer plus a physical page atlas,
		// with the virtual-to-physical translation done in the shader.
		//
		// Nothing here streams yet. Every page is made resident up front, which costs the
		// full source footprint in VRAM and proves the translation is right before residency
		// is allowed to change underneath it.
		class VirtualTextureSystem
		{
		public:
			struct Stats
			{
				u32 virtualTextureCount = 0;
				u32 pageCount = 0;
				u32 atlasCount = 0;
				u64 atlasBytes = 0;

				// Pages that could not be placed because every atlas was full.
				u32 droppedPages = 0;
			};

			bool initialize(const std::shared_ptr<rhi::IRHI>& rhi, material::MaterialSystem& materialSystem);
			void deinitialize();

			// Slices one source texture into pages and makes all of them resident. The
			// source's bindless index doubles as its virtual texture index.
			void add(const asset::TextureSource& source);

			// Uploads whatever is left in the partially filled atlases and publishes the
			// page table. Call once, after the last add().
			void finalize();

			rhi::BufferHandle infoBuffer() const { return infoBuffer_; }
			rhi::BufferHandle pageTableBuffer() const { return pageTableBuffer_; }

			const Stats& stats() const { return stats_; }

		private:
			// An atlas being filled: its pixels live in RAM until it is full, then go to the
			// GPU and the RAM is handed back.
			struct Atlas
			{
				rhi::TextureHandle texture = INVALID_HANDLE;

				// Index in the bindless array, which is what a page table entry stores.
				u32 bindlessIndex = 0;

				u32 usedTiles = 0;

				std::vector<u8> pixels;
			};

			// Returns the atlas with room for one more tile, creating one if needed.
			// Separate chains per colour space: an atlas has a single format and mixing
			// sRGB and linear source data in one would decode half of it wrongly.
			Atlas* acquireAtlas(bool srgb);

			void uploadAtlas(Atlas& atlas);

			// Copies one page out of a source level and into a tile, wrapping the border
			// around the level so the seam between pages filters correctly.
			void blitPage(
				Atlas& atlas, u32 tileIndex,
				const u8* level, u32 levelWidth, u32 levelHeight,
				u32 pageX, u32 pageY);

		private:
			std::shared_ptr<rhi::IRHI> rhi_;

			material::MaterialSystem* materialSystem_ = nullptr;

			rhi::BufferHandle infoBuffer_ = INVALID_HANDLE;
			rhi::BufferHandle pageTableBuffer_ = INVALID_HANDLE;

			std::vector<u32> pageTable_;

			// Index 0 is sRGB, index 1 is linear.
			std::vector<std::unique_ptr<Atlas>> atlases_[2];

			Stats stats_{};
		};
	}
}

#endif
