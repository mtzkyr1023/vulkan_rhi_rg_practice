#ifndef _MV_VULKAN_COMMAND_H_
#define _MV_VULKAN_COMMAND_H_

#include "vulkan/vulkan.h"

#include "util/types.h"

namespace mv
{
	namespace backend
	{
		struct VulkanCommandContext
		{
			VkCommandPool commandPool = VK_NULL_HANDLE;
			VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
		};
	}
}

#endif