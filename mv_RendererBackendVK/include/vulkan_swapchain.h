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

		class VulkanDevice;

		class VulkanSwapchain
		{
		public:
			void initialize(VulkanDevice* device, void* hwnd);
			void deinitialize();

			VkSwapchainKHR swapchain() const { return swapchain_; }
			VkSwapchainKHR oldSwapchain() const { return oldSwapchain_; }
			VkFormat format() const { return format_; }
			VkExtent2D extent() const { return extent_; }

			const std::vector<VkImage>& images() const { return images_; }
			const std::vector<VkImageView>& views() const { return views_; }

		private:
			VkSurfaceKHR surface_ = VK_NULL_HANDLE;
			VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
			VkSwapchainKHR oldSwapchain_ = VK_NULL_HANDLE;

			VkFormat format_ = VK_FORMAT_UNDEFINED;
			VkExtent2D extent_ = { 0, 0 };
			
			std::vector<VkImage> images_;
			std::vector<VkImageView> views_;

			u32 imageCount_ = 0;

			VulkanDevice* device_ = nullptr;
		};
	}
}

#endif