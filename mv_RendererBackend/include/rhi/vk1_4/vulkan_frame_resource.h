
#ifndef _MV_VULKAN_FRAME_RESOURCE_H_
#define _MV_VULKAN_FRAME_RESOURCE_H_

#include <vulkan/vulkan.h>

#include "rhi/rhi.h"

namespace mv
{
	namespace backend
	{
		namespace vk1_4
		{
			class VulkanDevice;
			class VulkanCommandPool;

			struct VulkanFrameResource
			{
				void initialize(VulkanDevice* device, VulkanCommandPool* commandPool);
				void deinitialize(VulkanDevice* device, VulkanCommandPool* commandPool);

				rhi::CommandBufferHandle commandBuffer = INVALID_HANDLE;
				VkSemaphore imageAvailableSemaphore = VK_NULL_HANDLE;
				VkFence inFlightFence = VK_NULL_HANDLE;

				rhi::TextureHandle backbuffer = INVALID_HANDLE;
			};
		}
	}
}


#endif
