
#include "vulkan_device.h"

namespace mv::backend
{
	VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(
		VkDebugUtilsMessageSeverityFlagBitsEXT severity,
		VkDebugUtilsMessageTypeFlagsEXT type,
		const VkDebugUtilsMessengerCallbackDataEXT* data,
		void* userData);

	VulkanDevice::VulkanDevice()
	{

	}

	VulkanDevice::~VulkanDevice()
	{

	}

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

	void VulkanDevice::createLogicalDevice()
	{
		float priority = 1.0f;

		std::vector<VkDeviceQueueCreateInfo> queueCIs;
		{
			VkDeviceQueueCreateInfo q;
			q.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
			q.queueFamilyIndex = graphicsQueueFamilyIndex_;
			q.queueCount = 1;
			q.pQueuePriorities = &priority;

			queueCIs.push_back(q);
		}
		{
			VkDeviceQueueCreateInfo q;
			q.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
			q.queueFamilyIndex = computeQueueFamilyIndex_;
			q.queueCount = 1;
			q.pQueuePriorities = &priority;

			queueCIs.push_back(q);
		}
		{
			VkDeviceQueueCreateInfo q;
			q.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
			q.queueFamilyIndex = transferQueueFamilyIndex_;
			q.queueCount = 1;
			q.pQueuePriorities = &priority;

			queueCIs.push_back(q);
		}

		const char* extensions[] = {
			VK_KHR_SWAPCHAIN_EXTENSION_NAME
		};

		VkDeviceCreateInfo ci{};
		ci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
		ci.queueCreateInfoCount = queueCIs.size();
		ci.pQueueCreateInfos = queueCIs.data();
		ci.enabledExtensionCount = 1;
		ci.ppEnabledExtensionNames = extensions;

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