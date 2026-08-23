
#ifndef _MV_RHI_RESOURCE_H_
#define _MV_RHI_RESOURCE_H_

#include "util/types.h"


namespace mv
{
	namespace rhi
	{
		using namespace types;

		using BufferHandle = u32;
		using TextureHandle = u32;

		enum class EMemoryType : u8
		{
			eDeviceLocalImage = 0,
			eDeviceLocalBuffer,
			eHostVisibleImage,
			eHostVisibleBuffer,

			// CPU-readable and cached, for getting results back off the GPU. Host-visible
			// memory is write-combined and painfully slow to read, so it is not a substitute.
			eReadback,

			eNum,
		};

		enum class EBufferUsage
		{
			eVertex = (1 << 0),
			eIndex = (1 << 1),
			eUniform = (1 << 2),
			eStorage = (1 << 3),
			eIndirectArgs = (1 << 4),
			eTransferSrc = (1 << 5),
			eTransferDst = (1 << 6),

			// Written from a shader, not just read. D3D12 needs the UAV flag for this, and
			// a buffer carrying it cannot live on an upload heap.
			eStorageReadWrite = (1 << 7),
		};

		enum class ETextureUsage
		{
			eColorAttachment = (1 << 0),
			eDepthStencilAttachment = (1 << 1),
			eSampled = (1 << 2),
			eStorage = (1 << 3),
			eTransferSrc = (1 << 4),
			eTransferDst = (1 << 5),
		};

		enum class EIndexFormat
		{
			eUint16 = 0,
			eUint32,
		};

		enum class EResourceState
		{
			eUndefined = 0,

			eColorAttachment,

			eCopySrc,
			eCopyDst,

			eVertexBuffer,
			eIndexBuffer,

			eConstantBuffer,

			eShaderRead,
			eShaderWrite,

			eRenderTarget,

			eDepthStencilWrite,
			eDepthStencilRead,

			eTransferSrc,
			eTransferDst,

			ePresent,
		};

		// all of the texture formats that we support
		enum class ETextureFormat
		{
			eUndefined = 0,

			eR8G8B8A8_UNORM,
			eR8G8B8A8_SRGB,
			eB8G8R8A8_UNORM,

			eD32_SFLOAT,
			eD24_UNORM_S8_UINT,

			// Visibility buffer target: an integer pair, never filtered or blended.
			eR32G32_UINT,
		};

		struct BufferDesc
		{
			u64 size;
			EBufferUsage usage;

			EMemoryType memoryType;
		};

		struct TextureDesc
		{
			u32 width;
			u32 height;
			u32 depth;
			ETextureUsage usage;

			// 0 means a full chain down to 1x1; 1 means no mips at all.
			u32 mipLevels = 1;

			ETextureFormat format;
			EMemoryType memoryType;
		};

		// One mip level of source data for uploadTexture.
		struct TextureUpload
		{
			const void* data = nullptr;
			u64 size = 0;
		};

		inline u32 mipLevelsFor(u32 width, u32 height)
		{
			u32 levels = 1;
			while (width > 1 || height > 1)
			{
				width = (width > 1) ? width / 2 : 1;
				height = (height > 1) ? height / 2 : 1;
				levels++;
			}

			return levels;
		}

		struct TextureBarrier
		{
			TextureHandle texture;

			EResourceState before;
			EResourceState after;
		};

		struct BufferBarrier
		{
			BufferHandle buffer;

			EResourceState before;
			EResourceState after;
		};
	}

	namespace enum_concept
	{
		template<> struct has_and_or_operators<rhi::EBufferUsage> : std::true_type {};
		template<> struct has_and_or_operators<rhi::ETextureUsage> : std::true_type {};
		template<> struct has_and_or_operators<rhi::EResourceState> : std::true_type {};
	}

	namespace rhi
	{
		// The operators live in namespace mv, but ADL on an mv::rhi enum only searches
		// mv::rhi. Without these, the flag operators resolve only for code that happens to
		// be nested inside namespace mv, and fail for every outside caller.
		using mv::operator&;
		using mv::operator|;
		using mv::operator&=;
		using mv::operator|=;
	}
}

#endif