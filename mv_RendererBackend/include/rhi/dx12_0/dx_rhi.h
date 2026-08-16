
#ifndef _MV_DX_RHI_H_
#define _MV_DX_RHI_H_

#include "rhi/dx12_0/dx_device.h"
#include "rhi/dx12_0/dx_swapchain.h"
#include "rhi/dx12_0/dx_command.h"
#include "rhi/dx12_0/dx_frame_resource.h"
#include "rhi/dx12_0/dx_descriptor.h"
#include "rhi/dx12_0/dx_resource.h"
#include "rhi/dx12_0/dx_memory.h"

#include "rhi/rhi.h"

namespace mv
{
	namespace backend
	{
		namespace dx12_0
		{
			using namespace types;

			class DxRHI : public rhi::IRHI
			{
			public:
				void initialize(void* hwnd) override;
				void deinitialize() override;

				void waitIdle() override;

				rhi::FrameContext beginFrame() override;
				void endFrame() override;

				void clearRenderTarget(float clearColor[]) override;

				rhi::CommandBufferHandle allocateCommandBuffer(rhi::EQueueType queueType) override;

				rhi::BufferHandle createBuffer(const rhi::BufferDesc& desc) override;
				rhi::TextureHandle createTexture(const rhi::TextureDesc& desc) override;

				void textureBarrier(rhi::CommandBufferHandle cmd, const rhi::TextureBarrier& barrier) override;
				void bufferBarrier(rhi::CommandBufferHandle cmd, const rhi::BufferBarrier& barrier) override;

				rhi::CommandBufferHandle getCurrentCommandBuffer() const override;

				void freeImage(rhi::TextureHandle handle) override;
				void freeBuffer(rhi::BufferHandle handle) override;

				void releaseImage(rhi::TextureHandle handle) override;
				void releaseBuffer(rhi::BufferHandle handle) override;

			private:
				void createBackbuffer() override;

			private:
				DxDevice device_;
				DxSwapchain swapchain_;

				DxDescriptorAllocator rtvDescriptorAllocator_;
				DxDescriptorAllocator dsvDescriptorAllocator_;
				DxDescriptorAllocator srvDescriptorAllocator_;
				DxDescriptorAllocator globalDescriptorAllocator_;

				std::vector<DxBuffer> buffers_;
				std::vector<DxImage> images_;

				std::vector<rhi::TextureHandle> freeImageList_;
				std::vector<rhi::BufferHandle> freeBufferList_;

				std::vector<rhi::TextureHandle> backbuffers_;

				std::vector<DxFrameResource> frameResources_;

				DxMemoryAllocator memoryAllocator_[(u32)rhi::EMemoryType::eNum];

				DxCommandPool commandPool_[(u32)rhi::EQueueType::eNum];

				types::u32 currentFrame_ = 0;
			};
		}
	}
}

#endif