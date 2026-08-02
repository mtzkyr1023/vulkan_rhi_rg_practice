
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
			eR8G8B8A8_UNORM,
			eR8G8B8A8_SRGB,
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

			ETextureFormat format;
			EMemoryType memoryType;
		};

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
}

#endif