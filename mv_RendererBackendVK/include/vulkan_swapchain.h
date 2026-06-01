#ifndef _MV_VULKAN_SWAPCHAIN_H_
#define _MV_VULKAN_SWAPCHAIN_H_

#include "vulkan/vulkan.h"

#include "util/types.h"

namespace mv
{
	namespace backend
	{
		struct VulkanSwapChain
		{
			VkSwapchainKHR swapchain = VK_NULL_HANDLE;
			VkFormat format = VK_FORMAT_UNDEFINED;
			VkExtent2D extent = { 0, 0 };
			std::vector<VkImage> images;
			std::vector<VkImageView> views;
		};
	}
}

#endif