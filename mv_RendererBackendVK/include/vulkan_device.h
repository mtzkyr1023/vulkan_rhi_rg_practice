
#ifndef _MV_VULKAN_DEVICE_H_
#define _MV_VULKAN_DEVICE_H_

#include "windows.h"

#include <vector>

#include "vulkan/vulkan.h"

#include "util/types.h"

namespace mv
{
	namespace backend
	{
		using namespace types;

		struct VulkanDevice
		{
			VkInstance instance = VK_NULL_HANDLE;
			VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
			VkDevice device = VK_NULL_HANDLE;
		};
	}
}

#endif
