#ifndef _MV_VULKAN_QUEUE_H_
#define _MV_VULKAN_QUEUE_H_

#include "vulkan/vulkan.h"

#include "util/types.h"

namespace mv
{
	namespace backend
	{
		struct VulkanQueue
		{
			VkQueue queue = VK_NULL_HANDLE;
			VkFence fence = VK_NULL_HANDLE;
		};
	}
}

#endif