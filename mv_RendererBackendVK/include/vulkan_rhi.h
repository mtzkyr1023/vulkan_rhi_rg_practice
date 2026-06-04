#ifndef _MV_VULKAN_RHI_H_
#define _MV_VULKAN_RHI_H_

#include "vulkan_device.h"
#include "vulkan_command.h"
#include "vulkan_resource.h"
#include "vulkan_descriptor.h"
#include "vulkan_swapchain.h"
#include "vulkan_memory.h"
#include "vulkan_pipeline.h"

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

			CommandBufferHandle allocateCommandBuffer(EQueueType queueType) override;

			virtual BufferHandle createBuffer(const BufferDesc& desc) override;
			virtual TextureHandle createTexture(const TextureDesc& desc) override;

		private:
			backend::VulkanDevice device_;
			backend::VulkanSwapchain swapchain_;

			backend::VulkanDescriptorAllocator descriptorAllocator_;

			std::vector<backend::VulkanBuffer> buffers_;
			std::vector<backend::VulkanImage> images_;

			backend::VulkanMemoryAllocator memoryAllocator_[(u32)EMemoryType::eNum];

			backend::VulkanShaderManager shaderManager_;
			backend::VulkanBindGroupLayoutManager layoutManager_;
			backend::VulkanPipelineManager pipelineManager_;

			backend::VulkanCommandPool commandPool_;

			BufferHandle nextBufferHandle_ = 0;
			TextureHandle nextTextureHandle_ = 0;
		};
	}
}

#endif