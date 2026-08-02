
#include "rhi/vk/vulkan_device.h"

namespace mv::backend
{
	VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(
		VkDebugUtilsMessageSeverityFlagBitsEXT severity,
		VkDebugUtilsMessageTypeFlagsEXT type,
		const VkDebugUtilsMessengerCallbackDataEXT* data,
		void* userData);


	void VulkanDevice::initialize()
	{
		createInstance();
		pickPhysicalDevice();
		createLogicalDevice();
	}

	void VulkanDevice::deinitialize()
	{
		vkDestroyDevice(device_, nullptr);
		
		vkDestroyInstance(instance_, nullptr);
	}

	void VulkanDevice::waitIdle()
	{
		vkQueueWaitIdle(graphicsQueue_);
		vkQueueWaitIdle(computeQueue_);
		vkQueueWaitIdle(transferQueue_);
		vkDeviceWaitIdle(device_);
	}

	void VulkanDevice::createInstance()
	{
		VkApplicationInfo app{};
		app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
		app.apiVersion = VK_API_VERSION_1_4;

		const char* extensions[] = {
			VK_KHR_SURFACE_EXTENSION_NAME,
			VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
#ifdef _DEBUG
			VK_EXT_DEBUG_UTILS_EXTENSION_NAME,
#endif
		};

#ifdef _DEBUG
		const char* layers[] = {
			"VK_LAYER_KHRONOS_validation",
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

	void VulkanDevice::pickPhysicalDevice()
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

			u32 graphicsFamily = static_cast<u32>(-1);
			u32 computeFamily = static_cast<u32>(-1);
			u32 transferFamily = static_cast<u32>(-1);

			for (uint32_t i = 0; i < queueFamilies.size(); i++)
			{
				const VkQueueFlags flags = queueFamilies[i].queueFlags;

				if (((flags & VK_QUEUE_GRAPHICS_BIT) != 0) && graphicsFamily == static_cast<u32>(-1))
				{
					graphicsFamily = i;
				}

				// Prefer a queue family dedicated to compute (no graphics bit).
				if (((flags & VK_QUEUE_COMPUTE_BIT) != 0) &&
					(computeFamily == static_cast<u32>(-1) || ((flags & VK_QUEUE_GRAPHICS_BIT) == 0)))
				{
					computeFamily = i;
				}

				// Prefer a queue family dedicated to transfer (no graphics/compute bit).
				if (((flags & VK_QUEUE_TRANSFER_BIT) != 0) &&
					(transferFamily == static_cast<u32>(-1) || ((flags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) == 0)))
				{
					transferFamily = i;
				}
			}

			if (graphicsFamily != static_cast<u32>(-1))
			{
				physicalDevice_ = device;
				graphicsQueueFamilyIndex_ = graphicsFamily;
				computeQueueFamilyIndex_ = (computeFamily != static_cast<u32>(-1)) ? computeFamily : graphicsFamily;
				transferQueueFamilyIndex_ = (transferFamily != static_cast<u32>(-1)) ? transferFamily : graphicsFamily;
				break;
			}
		}

		vkGetPhysicalDeviceMemoryProperties(physicalDevice_, &memProps_);
	}

	void VulkanDevice::createLogicalDevice()
	{
		float priority = 1.0f;

		const u32 uniqueFamilies[] = { graphicsQueueFamilyIndex_, computeQueueFamilyIndex_, transferQueueFamilyIndex_ };

		std::vector<VkDeviceQueueCreateInfo> queueCIs;
		for (u32 family : uniqueFamilies)
		{
			bool alreadyAdded = false;
			for (const auto& q : queueCIs)
			{
				if (q.queueFamilyIndex == family)
				{
					alreadyAdded = true;
					break;
				}
			}
			if (alreadyAdded) continue;

			VkDeviceQueueCreateInfo q{};
			q.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
			q.queueFamilyIndex = family;
			q.queueCount = 1;
			q.pQueuePriorities = &priority;

			queueCIs.push_back(q);
		}

		const char* extensions[] = {
			VK_KHR_SWAPCHAIN_EXTENSION_NAME
		};

		VkDeviceCreateInfo ci{};
		ci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
		ci.queueCreateInfoCount = static_cast<uint32_t>(queueCIs.size());
		ci.pQueueCreateInfos = queueCIs.data();
		ci.enabledExtensionCount = 1;
		ci.ppEnabledExtensionNames = extensions;

		VkPhysicalDeviceDynamicRenderingFeatures dynamicRendering{};
		dynamicRendering.sType =
			VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES;

		VkPhysicalDeviceFeatures2 features2{};
		features2.sType =
			VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
		features2.pNext = &dynamicRendering;

		vkGetPhysicalDeviceFeatures2(
			physicalDevice_,
			&features2);

		dynamicRendering.dynamicRendering = VK_TRUE;
		ci.pNext = &dynamicRendering;

		vkCreateDevice(physicalDevice_, &ci, nullptr, &device_);

		vkGetDeviceQueue(device_, graphicsQueueFamilyIndex_, 0, &graphicsQueue_);
		vkGetDeviceQueue(device_, computeQueueFamilyIndex_, 0, &computeQueue_);
		vkGetDeviceQueue(device_, transferQueueFamilyIndex_, 0, &transferQueue_);
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