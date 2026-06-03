
#include "vulkan_swapchain.h"

namespace mv::backend
{
	VulkanSwapchain::VulkanSwapchain()
	{

	}

	VulkanSwapchain::~VulkanSwapchain()
	{

	}

	void VulkanSwapchain::initialize(VkInstance instance, VkPhysicalDevice physicalDevice, VkDevice logicalDevice, void* hwnd)
	{
		for (u32 i = 0; i < imageCount_; i++)
		{
			vkDestroyImageView(logicalDevice, views_[i], nullptr);
			vkDestroyImage(logicalDevice, images_[i], nullptr);
		}

		VkWin32SurfaceCreateInfoKHR surfaceCI{};
		surfaceCI.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
		surfaceCI.hwnd = (HWND)hwnd;
		surfaceCI.hinstance = GetModuleHandle(nullptr);
		vkCreateWin32SurfaceKHR(instance, &surfaceCI, nullptr, &surface_);

		struct SwapchainSupport
		{
			VkSurfaceCapabilitiesKHR capabilities;
			std::vector<VkSurfaceFormatKHR> formats;
			std::vector<VkPresentModeKHR> presentModes;
		};

		SwapchainSupport s{};

		vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface_, &s.capabilities);

		uint32_t count = 0;

		vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface_, &count, nullptr);
		s.formats.resize(count);
		vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface_, &count, s.formats.data());

		vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface_, &count, nullptr);
		s.presentModes.resize(count);
		vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface_, &count, s.presentModes.data());

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

		VkExtent2D extents = s.capabilities.currentExtent;

		RECT rect;
		GetClientRect((HWND)hwnd, &rect);

		VkExtent2D extent{};
		extent.width = rect.right - rect.left;
		extent.height = rect.bottom - rect.top;

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
		swapchainCI.oldSwapchain = oldSwapchain_;

		VkSwapchainKHR swapchain;
		vkCreateSwapchainKHR(logicalDevice, &swapchainCI, nullptr, &swapchain);

		imageCount_ = imageCount;
		oldSwapchain_ = swapchain_;
		swapchain_ = swapchain;

		images_.resize(imageCount_);
		vkGetSwapchainImagesKHR(logicalDevice, swapchain_, &imageCount, images_.data());

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
			vkCreateImageView(logicalDevice, &viewCI, nullptr, &view);
			views_.push_back(view);
		}
	}

	void VulkanSwapchain::deinitialize(VkInstance instance, VkDevice logicalDevice)
	{
		for (u32 i = 0; i < imageCount_; i++)
		{
			vkDestroyImageView(logicalDevice, views_[i], nullptr);
			vkDestroyImage(logicalDevice, images_[i], nullptr);
		}

		vkDestroySwapchainKHR(logicalDevice, swapchain_, nullptr);
		vkDestroySurfaceKHR(instance, surface_, nullptr);
	}
}