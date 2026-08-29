
#include "rhi/vk1_4/vulkan_swapchain.h"
#include "rhi/vk1_4/vulkan_device.h"

#include <stdexcept>

namespace mv::backend::vk1_4
{
	void VulkanSwapchain::initialize(VulkanDevice* device, void* hwnd)
	{
		device_ = device;
		hwnd_ = hwnd;

		VkWin32SurfaceCreateInfoKHR surfaceCI{};
		surfaceCI.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
		surfaceCI.hwnd = (HWND)hwnd;
		surfaceCI.hinstance = GetModuleHandle(nullptr);
		vkCreateWin32SurfaceKHR(device->instance(), &surfaceCI, nullptr, &surface_);

		createSwapchain();
	}

	void VulkanSwapchain::createSwapchain()
	{
		struct SwapchainSupport
		{
			VkSurfaceCapabilitiesKHR capabilities;
			std::vector<VkSurfaceFormatKHR> formats;
			std::vector<VkPresentModeKHR> presentModes;
		};

		SwapchainSupport s{};

		vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device_->physicalDevice(), surface_, &s.capabilities);

		uint32_t count = 0;

		vkGetPhysicalDeviceSurfaceFormatsKHR(device_->physicalDevice(), surface_, &count, nullptr);
		s.formats.resize(count);
		vkGetPhysicalDeviceSurfaceFormatsKHR(device_->physicalDevice(), surface_, &count, s.formats.data());

		vkGetPhysicalDeviceSurfacePresentModesKHR(device_->physicalDevice(), surface_, &count, nullptr);
		s.presentModes.resize(count);
		vkGetPhysicalDeviceSurfacePresentModesKHR(device_->physicalDevice(), surface_, &count, s.presentModes.data());

		VkSurfaceFormatKHR format = s.formats[0];
		for (const auto& f : s.formats)
		{
			if (f.format == VK_FORMAT_B8G8R8A8_UNORM &&
				f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
			{
				format = f;
				break;
			}
		}


		VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;
		for (auto m : s.presentModes)
		{
			if (m == VK_PRESENT_MODE_MAILBOX_KHR)
			{
				presentMode = m;
				break;
			}
		}

		// currentExtent is the size the surface is already at, which is what the driver
		// wants the swap chain to be. Windows reports the real client size here, and the
		// special 0xFFFFFFFF value that means "pick one" never comes up, but asking the
		// window is the fallback for the platforms where it does.
		VkExtent2D extent = s.capabilities.currentExtent;

		if (extent.width == UINT32_MAX || extent.height == UINT32_MAX)
		{
			RECT rect;
			GetClientRect((HWND)hwnd_, &rect);

			extent.width = rect.right - rect.left;
			extent.height = rect.bottom - rect.top;
		}

		extent.width = std::max(s.capabilities.minImageExtent.width,
			std::min(s.capabilities.maxImageExtent.width, extent.width));

		extent.height = std::max(s.capabilities.minImageExtent.height,
			std::min(s.capabilities.maxImageExtent.height, extent.height));

		u32 imageCount = s.capabilities.minImageCount + 1;

		if (s.capabilities.maxImageCount > 0 &&
			imageCount > s.capabilities.maxImageCount)
		{
			imageCount = s.capabilities.maxImageCount;
		}

		extent_ = extent;
		format_ = format.format;

		VkSwapchainCreateInfoKHR swapchainCI{};
		swapchainCI.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
		swapchainCI.surface = surface_;
		swapchainCI.minImageCount = imageCount;
		swapchainCI.imageFormat = format.format;
		swapchainCI.imageColorSpace = format.colorSpace;
		swapchainCI.imageExtent = extent;
		swapchainCI.imageArrayLayers = 1;
		swapchainCI.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

		swapchainCI.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;

		swapchainCI.preTransform = s.capabilities.currentTransform;
		swapchainCI.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
		swapchainCI.presentMode = presentMode;
		swapchainCI.clipped = VK_TRUE;

		// Handing the driver the chain being replaced lets it reuse the images behind it
		// instead of allocating a second full set and freeing the first.
		swapchainCI.oldSwapchain = swapchain_;

		VkSwapchainKHR swapchain = VK_NULL_HANDLE;
		if (vkCreateSwapchainKHR(device_->device(), &swapchainCI, nullptr, &swapchain) != VK_SUCCESS)
			throw std::runtime_error("Failed to create the swap chain");

		// Retired only once the new chain has been derived from it. The caller has waited
		// for the device to go idle, so nothing is still presenting out of it.
		if (swapchain_ != VK_NULL_HANDLE)
			vkDestroySwapchainKHR(device_->device(), swapchain_, nullptr);

		swapchain_ = swapchain;

		vkGetSwapchainImagesKHR(device_->device(), swapchain_, &imageCount, nullptr);
		imageCount_ = imageCount;

		images_.resize(imageCount_);
		vkGetSwapchainImagesKHR(device_->device(), swapchain_, &imageCount, images_.data());

		views_.reserve(imageCount_);
		renderFinishedSemaphores_.reserve(imageCount_);

		for (u32 i = 0; i < imageCount_; i++)
		{
			VkImageViewCreateInfo viewCI{};
			viewCI.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
			viewCI.image = images_[i];
			viewCI.viewType = VK_IMAGE_VIEW_TYPE_2D;
			viewCI.format = format.format;
			viewCI.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
			viewCI.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
			viewCI.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
			viewCI.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
			viewCI.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			viewCI.subresourceRange.baseMipLevel = 0;
			viewCI.subresourceRange.levelCount = 1;
			viewCI.subresourceRange.baseArrayLayer = 0;
			viewCI.subresourceRange.layerCount = 1;
			VkImageView view;
			vkCreateImageView(device_->device(), &viewCI, nullptr, &view);
			views_.push_back(view);

			VkSemaphoreCreateInfo semaphoreCI{};
			semaphoreCI.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
			VkSemaphore semaphore;
			vkCreateSemaphore(device_->device(), &semaphoreCI, nullptr, &semaphore);
			renderFinishedSemaphores_.push_back(semaphore);
		}

		imageIndex_ = 0;
	}

	void VulkanSwapchain::destroyImageViews()
	{
		for (auto view : views_)
			vkDestroyImageView(device_->device(), view, nullptr);

		for (auto semaphore : renderFinishedSemaphores_)
			vkDestroySemaphore(device_->device(), semaphore, nullptr);

		// The views and semaphores are appended as the chain is built, so they have to go
		// from the vectors as well as be destroyed, or a rebuild would stack a second set
		// behind the first. imageCount_ counts them, so it goes with them.
		views_.clear();
		renderFinishedSemaphores_.clear();
		images_.clear();

		imageCount_ = 0;
	}

	void VulkanSwapchain::deinitialize()
	{
		destroyImageViews();

		vkDestroySwapchainKHR(device_->device(), swapchain_, nullptr);
		swapchain_ = VK_NULL_HANDLE;

		vkDestroySurfaceKHR(device_->instance(), surface_, nullptr);
		surface_ = VK_NULL_HANDLE;
	}

	void VulkanSwapchain::resize(void* hwnd)
	{
		// Vulkan has no equivalent of ResizeBuffers: a swap chain's extent is fixed at
		// creation, so a new one is built and the old one handed over as its ancestor. The
		// surface outlives it -- it belongs to the window, which has not gone anywhere.
		hwnd_ = hwnd;

		destroyImageViews();
		createSwapchain();
	}


	bool VulkanSwapchain::surfaceHasArea() const
	{
		VkSurfaceCapabilitiesKHR capabilities{};
		vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device_->physicalDevice(), surface_, &capabilities);

		return capabilities.currentExtent.width != 0 && capabilities.currentExtent.height != 0;
	}

	VkResult VulkanSwapchain::acquireNextImage(VkSemaphore semaphore)
	{
		return vkAcquireNextImageKHR(device_->device(), swapchain_, UINT64_MAX, semaphore, VK_NULL_HANDLE, &imageIndex_);
	}

	VkResult VulkanSwapchain::present(VkQueue queue)
	{
		VkPresentInfoKHR presentInfo{};
		presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
		presentInfo.waitSemaphoreCount = 1;
		presentInfo.pWaitSemaphores = &renderFinishedSemaphores_[imageIndex_];
		presentInfo.swapchainCount = 1;
		presentInfo.pSwapchains = &swapchain_;
		presentInfo.pImageIndices = &imageIndex_;
		presentInfo.pResults = nullptr;
		return vkQueuePresentKHR(queue, &presentInfo);
	}
}
