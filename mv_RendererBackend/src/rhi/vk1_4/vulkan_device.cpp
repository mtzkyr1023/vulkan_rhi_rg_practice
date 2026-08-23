
#include "rhi/vk1_4/vulkan_device.h"

#include <cstdio>

namespace mv::backend::vk1_4
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
		app.apiVersion = VK_API_VERSION_1_3;

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

		// Bindless needs descriptor indexing: an unbounded array that can be indexed with a
		// value that varies across a wave, updated after binding, and only partially filled.
		VkPhysicalDeviceVulkan12Features supported12{};
		supported12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;

		VkPhysicalDeviceFeatures2 features2{};
		features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
		features2.pNext = &supported12;

		vkGetPhysicalDeviceFeatures2(physicalDevice_, &features2);

		supportsBindless_ =
			supported12.runtimeDescriptorArray &&
			supported12.shaderSampledImageArrayNonUniformIndexing &&
			supported12.descriptorBindingPartiallyBound &&
			supported12.descriptorBindingSampledImageUpdateAfterBind &&
			supported12.descriptorBindingVariableDescriptorCount;

		VkPhysicalDeviceProperties props{};
		vkGetPhysicalDeviceProperties(physicalDevice_, &props);

		maxSamplerAnisotropy_ = features2.features.samplerAnisotropy ? props.limits.maxSamplerAnisotropy : 1.0f;

		char message[512];
		sprintf_s(message,
			"Vulkan device: %s\n  bindless (descriptor indexing): %s\n",
			props.deviceName,
			supportsBindless_ ? "yes" : "NO");
		OutputDebugStringA(message);
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
		dynamicRendering.dynamicRendering = VK_TRUE;

		// Descriptor indexing is opt-in, unlike D3D12 where an unbounded table only needs a
		// high enough binding tier.
		VkPhysicalDeviceVulkan12Features features12{};
		features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
		features12.pNext = &dynamicRendering;

		if (supportsBindless_)
		{
			features12.runtimeDescriptorArray = VK_TRUE;
			features12.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
			features12.descriptorBindingPartiallyBound = VK_TRUE;
			features12.descriptorBindingSampledImageUpdateAfterBind = VK_TRUE;
			features12.descriptorBindingVariableDescriptorCount = VK_TRUE;
		}

		// The shaders are compiled with D3D packing so their structs match the CPU ones
		// byte for byte. That produces vectors straddling 16-byte boundaries, which Vulkan
		// only accepts under scalar block layout.
		features12.scalarBlockLayout = VK_TRUE;

		// Anisotropic filtering is an optional core feature and has to be asked for.
		VkPhysicalDeviceFeatures2 enabledFeatures{};
		enabledFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
		enabledFeatures.pNext = &features12;
		enabledFeatures.features.samplerAnisotropy = VK_TRUE;
		// Reading SV_PrimitiveID from a fragment shader compiles to SPIR-V that declares
		// the Geometry capability, which this feature gates even though no geometry shader
		// is involved. The visibility buffer pass needs it.
		enabledFeatures.features.geometryShader = VK_TRUE;
		// The streaming feedback buffer is written by a fragment shader, which D3D12 allows
		// unconditionally but Vulkan gates behind this.
		enabledFeatures.features.fragmentStoresAndAtomics = VK_TRUE;

		ci.pNext = &enabledFeatures;

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