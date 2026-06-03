#ifndef _MV_VULKAN_RHI_H_
#define _MV_VULKAN_RHI_H_

#include "vulkan_device.h"
#include "vulkan_command.h"
#include "vulkan_resource.h"
#include "vulkan_descriptor.h"
#include "vulkan_swapchain.h"
#include "vulkan_memory.h"
#include "vulkan_pipeline.h"
#include "vulkan_queue.h"

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

			virtual BufferHandle createBuffer(const BufferDesc& desc) override;
			virtual TextureHandle createTexture(const TextureDesc& desc) override;

		private:
			backend::VulkanDevice device_;

			backend::VulkanDescriptorAllocator descriptorAllocator_;

			std::vector<backend::VulkanBuffer> buffers_;
			std::vector<backend::VulkanImage> images_;

			backend::VulkanMemoryAllocator memoryAllocator_[(u32)backend::EMemoryType::eNum];

			BufferHandle nextBufferHandle_ = 0;
			TextureHandle nextTextureHandle_ = 0;
		};
	}
}

#endif