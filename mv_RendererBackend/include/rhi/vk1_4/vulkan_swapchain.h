#ifndef _MV_VULKAN_SWAPCHAIN_H_
#define _MV_VULKAN_SWAPCHAIN_H_

#include <vulkan/vulkan.h>

#include <vector>

#include "util/types.h"

namespace mv
{
	namespace backend
	{
		namespace vk1_4
		{
			using namespace types;

			class VulkanDevice;

			class VulkanSwapchain
			{
			public:
				void initialize(VulkanDevice* device, void* hwnd);
				void deinitialize();

				// Rebuilds at whatever size the window is now.
				void resize(void* hwnd);

				// Returns the acquire result so the caller can tell a usable image from a
				// chain the window has outgrown, which signals nothing and cannot be
				// submitted against.
				VkResult acquireNextImage(VkSemaphore semaphore);

				// False once the window is minimised: there is no extent to build at.
				bool surfaceHasArea() const;
				VkResult present(VkQueue queue);

				VkSwapchainKHR swapchain() const { return swapchain_; }
				VkFormat format() const { return format_; }
				VkExtent2D extent() const { return extent_; }

				const std::vector<VkImage>& images() const { return images_; }
				const std::vector<VkImageView>& views() const { return views_; }

				VkSemaphore renderFinishedSemaphore() const { return renderFinishedSemaphores_[imageIndex_]; }

				types::u32 imageCount() const { return imageCount_; }
				types::u32 imageIndex() const { return imageIndex_; }

			private:
				// Builds the chain, its views and its per-image semaphores at whatever size
				// the surface currently is, retiring the previous chain into it.
				void createSwapchain();

				// Tears down everything the chain owns except the chain itself, which
				// createSwapchain needs as the ancestor of its replacement.
				void destroyImageViews();

			private:
				VkSurfaceKHR surface_ = VK_NULL_HANDLE;
				VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;

				std::vector<VkSemaphore> renderFinishedSemaphores_;

				VkFormat format_ = VK_FORMAT_UNDEFINED;
				VkExtent2D extent_ = { 0, 0 };

				std::vector<VkImage> images_;
				std::vector<VkImageView> views_;

				u32 imageCount_ = 0;

				VulkanDevice* device_ = nullptr;
				void* hwnd_ = nullptr;

				u32 imageIndex_ = 0;
			};
		}
	}
}

#endif