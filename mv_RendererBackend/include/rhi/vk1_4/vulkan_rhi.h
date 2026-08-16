#ifndef _MV_VULKAN_RHI_H_
#define _MV_VULKAN_RHI_H_

#include "rhi/vk1_4/vulkan_device.h"
#include "rhi/vk1_4/vulkan_command.h"
#include "rhi/vk1_4/vulkan_resource.h"
#include "rhi/vk1_4/vulkan_descriptor.h"
#include "rhi/vk1_4/vulkan_swapchain.h"
#include "rhi/vk1_4/vulkan_memory.h"
#include "rhi/vk1_4/vulkan_pipeline.h"
#include "rhi/vk1_4/vulkan_frame_resource.h"

#include "rhi/rhi.h"

namespace mv
{
	namespace backend
	{
		namespace vk1_4
		{
			class VulkanRHI : public rhi::IRHI
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
				VulkanDevice device_;
				VulkanSwapchain swapchain_;

				VulkanDescriptorAllocator descriptorAllocator_;

				std::vector<VulkanBuffer> buffers_;
				std::vector<VulkanImage> images_;

				std::vector<rhi::TextureHandle> freeImageList_;
				std::vector<rhi::BufferHandle> freeBufferList_;

				std::vector<rhi::TextureHandle> backbuffers_;

				std::vector<VulkanFrameResource> frameResources_;

				VulkanMemoryAllocator memoryAllocator_[(u32)rhi::EMemoryType::eNum];

				VulkanShaderManager shaderManager_;
				VulkanBindGroupLayoutManager layoutManager_;
				VulkanPipelineManager pipelineManager_;

				VulkanCommandPool commandPool_[(u32)rhi::EQueueType::eNum];

				types::u32 currentFrame_ = 0;
			};
		}
	}
}

#endif