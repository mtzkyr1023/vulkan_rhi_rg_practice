
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
			eColor = (1 << 0),
			eDepth = (1 << 1),
			eSampled = (1 << 2),
			eStorage = (1 << 3),
		};

		enum class EResourceState
		{
			eUndefines = 0,

			eCopySrc,
			eCopyDst,

			eVertexBuffer,
			eIndexBuffer,

			eConstantBuffer,

			eShaderRead,
			eShaderWrite,

			eRenderTarget,

			eDepthWrite,
			eDepthRead,

			ePresent,
		};

		enum class ETextureFormat
		{

		};

		struct BufferDesc
		{
			u64 size;
			EBufferUsage usage;
		};

		struct TextureDesc
		{
			u32 width;
			u32 height;
			u32 depth;
			ETextureUsage usage;

			ETextureFormat foramt;
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
}

#endif