#ifndef _MV_VULKAN_SWAPCHAIN_H_
#define _MV_VULKAN_SWAPCHAIN_H_

#include "vulkan/vulkan.h"

#include "vector"

#include "util/types.h"

namespace mv
{
	namespace backend
	{
		using namespace types;

		class VulkanSwapchain
		{
		public:
			VulkanSwapchain();
			~VulkanSwapchain();

			void initialize(VkInstance instance, VkPhysicalDevice physicalDevice, VkDevice logicalDevice, void* hwnd);
			void deinitialize(VkInstance instance, VkDevice logicalDevice);

			VkSwapchainKHR swapchain() { return swapchain_; }
			VkSwapchainKHR oldSwapchain() { return oldSwapchain_; }
			VkFormat format() { return format_; }
			VkExtent2D extent() { return extent_; }

			const std::vector<VkImage>& images() { return images_; }
			const std::vector<VkImageView>& views() { return views_; }

		private:
			VkSurfaceKHR surface_ = VK_NULL_HANDLE;
			VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
			VkSwapchainKHR oldSwapchain_ = VK_NULL_HANDLE;

			VkFormat format_ = VK_FORMAT_UNDEFINED;
			VkExtent2D extent_ = { 0, 0 };
			
			std::vector<VkImage> images_;
			std::vector<VkImageView> views_;

			u32 imageCount_ = 0;
		};
	}
}

#endif