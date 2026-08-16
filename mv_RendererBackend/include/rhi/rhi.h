
#ifndef _MV_RHI_H_
#define _MV_RHI_H_

#include <memory>

#include "util/types.h"

#include "rhi/resource.h"
#include "rhi/commandbuffer.h"

namespace mv
{
	namespace rhi
	{
		struct FrameContext
		{
			TextureHandle backbuffer;
			CommandBufferHandle cmd;
			u32 currentFrameIndex;
		};

		class ICommandBuffer;
		enum class EQueueType;

		class IRHI
		{
		public:
			virtual ~IRHI() {}

			virtual void initialize(void* hwnd) = 0;
			virtual void deinitialize() = 0;

			virtual void waitIdle() = 0;

			virtual FrameContext beginFrame() = 0;
			virtual void endFrame() = 0;

			virtual void clearRenderTarget(float clearColor[]) = 0;

			virtual CommandBufferHandle allocateCommandBuffer(EQueueType type) = 0;


			virtual BufferHandle createBuffer(const BufferDesc& desc) = 0;
			virtual TextureHandle createTexture(const TextureDesc& desc) = 0;

			virtual void textureBarrier(CommandBufferHandle cmd, const TextureBarrier& barrier) = 0;
			virtual void bufferBarrier(CommandBufferHandle cmd, const BufferBarrier& barrier) = 0;

			virtual CommandBufferHandle getCurrentCommandBuffer() const = 0;

			virtual void freeImage(TextureHandle handle) = 0;
			virtual void freeBuffer(BufferHandle handle) = 0;

			virtual void releaseImage(TextureHandle handle) = 0;
			virtual void releaseBuffer(BufferHandle handle) = 0;

			static std::shared_ptr<IRHI> createVulkanRHI();

		protected:
			virtual void createBackbuffer() = 0;

		protected:
			static constexpr types::u32 framesInFlight_ = 2;
		};
	}
}

#endif