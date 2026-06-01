#ifndef _MV_VULKAN_RESOURCE_H_
#define _MV_VULKAN_RESOURCE_H_

#include "vulkan/vulkan.h"

#include "util/types.h"

#include "vulkan_memory.h"

namespace mv
{
	namespace backend
	{
		using namespace types;

		struct VulkanBuffer
		{
			VkBuffer buffer = VK_NULL_HANDLE;
			Allocation alloc;
		};

		struct VulkanImage
		{
			VkImage image = VK_NULL_HANDLE;
			VkImageView view = VK_NULL_HANDLE;
			Allocation alloc;
		};
	}
}

#endif