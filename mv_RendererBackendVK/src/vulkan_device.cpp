
#include "vulkan_renderer.h"

namespace mv::renderer
{
	VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(
		VkDebugUtilsMessageSeverityFlagBitsEXT severity,
		VkDebugUtilsMessageTypeFlagsEXT type,
		const VkDebugUtilsMessengerCallbackDataEXT* data,
		void* userData);

	bool VulkanRenderer::initialize(void* hwnd)
	{
		createInstance();
		createSurface(hwnd);
		pickPhysicalDevice();
		createDevice();
		createSwapChain(hwnd);
		createCommandSystem();
		createSyncObjects();
		return true;
	}
	void VulkanRenderer::deinitialize()
	{
	}
	void VulkanRenderer::shutdown()
	{
	}
	void VulkanRenderer::render()
	{
		beginFrame();
		endFrame();
		present();
	}

	void VulkanRenderer::createInstance()
	{
		VkApplicationInfo app{};
		app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
		app.apiVersion = VK_API_VERSION_1_4;

		const char* extensions[] = {
			VK_KHR_SURFACE_EXTENSION_NAME,
			VK_KHR_WIN32_SURFACE_EXTENSION_NAME
		};

#ifdef _DEBUG
		const char* layers[] = {
			"VK_LAYER_KHRONOS_validation"
		};
#endif
		VkDebugUtilsMessengerCreateInfoEXT debugCI{};
		debugCI.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
		debugCI.messageSeverity =
			VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT |
			VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT;
		debugCI.messageType =
			VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
			VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
			VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
		debugCI.pfnUserCallback = DebugCallback;

		VkInstanceCreateInfo ci{};
		ci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
		ci.pApplicationInfo = &app;
		ci.enabledExtensionCount = _countof(extensions);
		ci.ppEnabledExtensionNames = extensions;

#ifdef _DEBUG
		ci.enabledLayerCount = 1;
		ci.ppEnabledLayerNames = layers;
		ci.pNext = &debugCI;
#endif

		vkCreateInstance(&ci, nullptr, &instance_);
	}

	void VulkanRenderer::createSurface(void* hwnd)
	{
		VkWin32SurfaceCreateInfoKHR ci{};
		ci.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
		ci.hwnd = (HWND)hwnd;
		ci.hinstance = GetModuleHandle(nullptr);
		vkCreateWin32SurfaceKHR(instance_, &ci, nullptr, &surface_);
	}

	void VulkanRenderer::pickPhysicalDevice()
	{
		uint32_t deviceCount = 0;
		vkEnumeratePhysicalDevices(instance_, &deviceCount, nullptr);
		std::vector<VkPhysicalDevice> devices(deviceCount);
		vkEnumeratePhysicalDevices(instance_, &deviceCount, devices.data());
		for (const auto& device : devices)
		{
			VkPhysicalDeviceProperties props;
			vkGetPhysicalDeviceProperties(device, &props);
			uint32_t queueFamilyCount = 0;
			vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);
			std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
			vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());
			for (uint32_t i = 0; i < queueFamilies.size(); i++)
			{
				if (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
				{
					graphicsQueueFamilyIndex_ = i;
					break;
				}
			}
			if (graphicsQueueFamilyIndex_ != -1)
			{
				physicalDevice_ = device;
				break;
			}
		}
	}

	void VulkanRenderer::createDevice()
	{
		float priority = 1.0f;

		VkDeviceQueueCreateInfo q{};
		q.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
		q.queueFamilyIndex = graphicsQueueFamilyIndex_;
		q.queueCount = 1;
		q.pQueuePriorities = &priority;

		const char* extensions[] = {
			VK_KHR_SWAPCHAIN_EXTENSION_NAME
		};

		VkDeviceCreateInfo ci{};
		ci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
		ci.queueCreateInfoCount = 1;
		ci.pQueueCreateInfos = &q;
		ci.enabledExtensionCount = 1;
		ci.ppEnabledExtensionNames = extensions;

		vkCreateDevice(physicalDevice_, &ci, nullptr, &device_);

		vkGetDeviceQueue(device_, graphicsQueueFamilyIndex_, 0, &graphicsQueue_);
	}

	void VulkanRenderer::createSwapChain(void* hwnd)
	{
		struct SwapchainSupport
		{
			VkSurfaceCapabilitiesKHR capabilities;
			std::vector<VkSurfaceFormatKHR> formats;
			std::vector<VkPresentModeKHR> presentModes;
		};

		SwapchainSupport s{};

		vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice_, surface_, &s.capabilities);

		uint32_t count = 0;

		vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice_, surface_, &count, nullptr);
		s.formats.resize(count);
		vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice_, surface_, &count, s.formats.data());

		vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice_, surface_, &count, nullptr);
		s.presentModes.resize(count);
		vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice_, surface_, &count, s.presentModes.data());

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

		uint32_t imageCount = s.capabilities.minImageCount + 1;

		if (s.capabilities.maxImageCount > 0 &&
			imageCount > s.capabilities.maxImageCount)
		{
			imageCount = s.capabilities.maxImageCount;
		}

		VkSwapchainCreateInfoKHR ci{};
		ci.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
		ci.surface = surface_;
		ci.minImageCount = imageCount;
		ci.imageFormat = format.format;
		ci.imageColorSpace = format.colorSpace;
		ci.imageExtent = extent;
		ci.imageArrayLayers = 1;
		ci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

			ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;

		ci.preTransform = s.capabilities.currentTransform;
		ci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
		ci.presentMode = presentMode;
		ci.clipped = VK_TRUE;
		ci.oldSwapchain = oldSwapchain_;

		VkSwapchainKHR swapchain;
		vkCreateSwapchainKHR(device_, &ci, nullptr, &swapchain);
	}



	void VulkanRenderer::createCommandSystem()
	{

	}

	void VulkanRenderer::createSyncObjects()
	{
	}

	void VulkanRenderer::beginFrame()
	{

	}

	void VulkanRenderer::endFrame()
	{
	}

	void VulkanRenderer::present()
	{
	}


	VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(
		VkDebugUtilsMessageSeverityFlagBitsEXT severity,
		VkDebugUtilsMessageTypeFlagsEXT type,
		const VkDebugUtilsMessengerCallbackDataEXT* data,
		void* userData)
	{
		OutputDebugStringA(data->pMessage);
		OutputDebugStringA("\n");
		return VK_FALSE;
	}
}