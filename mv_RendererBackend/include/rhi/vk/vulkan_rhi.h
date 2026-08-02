#ifndef _MV_VULKAN_RHI_H_
#define _MV_VULKAN_RHI_H_

#include "rhi/vk/vulkan_device.h"
#include "rhi/vk/vulkan_command.h"
#include "rhi/vk/vulkan_resource.h"
#include "rhi/vk/vulkan_descriptor.h"
#include "rhi/vk/vulkan_swapchain.h"
#include "rhi/vk/vulkan_memory.h"
#include "rhi/vk/vulkan_pipeline.h"
#include "rhi/vk/vulkan_frame_resource.h"

#include "rhi/rhi.h"

namespace mv
{
	namespace rhi
	{
		class VulkanRHI : public IRHI
		{
		public:
			void initialize(void* hwnd) override;
			void deinitialize() override;

			void waitIdle() override;

			FrameContext beginFrame() override;
			void endFrame() override;

			void clearRenderTarget(float clearColor[]) override;

			CommandBufferHandle allocateCommandBuffer(EQueueType queueType) override;

			BufferHandle createBuffer(const BufferDesc& desc) override;
			TextureHandle createTexture(const TextureDesc& desc) override;

			void textureBarrier(CommandBufferHandle cmd, const TextureBarrier& barrier) override;
			void bufferBarrier(CommandBufferHandle cmd, const BufferBarrier& barrier) override;

			CommandBufferHandle getCurrentCommandBuffer() const override;

			void freeImage(TextureHandle handle) override;
			void freeBuffer(BufferHandle handle) override;

			void releaseImage(TextureHandle handle) override;
			void releaseBuffer(BufferHandle handle) override;

		private:
			void createBackbuffer() override;

		private:
			backend::VulkanDevice device_;
			backend::VulkanSwapchain swapchain_;

			backend::VulkanDescriptorAllocator descriptorAllocator_;

			std::vector<backend::VulkanBuffer> buffers_;
			std::vector<backend::VulkanImage> images_;

			std::vector<TextureHandle> freeImageList_;
			std::vector<BufferHandle> freeBufferList_;

			std::vector<TextureHandle> backbuffers_;

			std::vector<backend::VulkanFrameResource> frameResources_;

			backend::VulkanMemoryAllocator memoryAllocator_[(u32)EMemoryType::eNum];

			backend::VulkanShaderManager shaderManager_;
			backend::VulkanBindGroupLayoutManager layoutManager_;
			backend::VulkanPipelineManager pipelineManager_;

			backend::VulkanCommandPool commandPool_[(u32)EQueueType::eNum];

			types::u32 currentFrame_ = 0;
		};
	}
}

#endif