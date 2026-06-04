
#ifndef _MV_VULKAN_DEVICE_H_
#define _MV_VULKAN_DEVICE_H_

#include "windows.h"

#include <vector>

#include "vulkan/vulkan.h"

#include "util/types.h"

namespace mv
{
	namespace backend
	{
		using namespace types;

		class VulkanDevice
		{
		public:

			void initialize();
			void deinitialize();

			void waitIdle();

			VkInstance instance() const { return instance_;}
			VkPhysicalDevice physicalDevice() const { return physicalDevice_; }
			VkDevice device() const { return device_; }

			VkQueue graphicsQueue() const { return graphicsQueue_; }
			VkQueue computeQueue() const { return computeQueue_; }
			VkQueue transferQueue() const { return transferQueue_; }

			u32 graphicsQueueFamilyIndex() const { return graphicsQueueFamilyIndex_; }
			u32 computeQueueFamilyIndex() const { return computeQueueFamilyIndex_; }
			u32 transferQueueFamilyIndex() const { return transferQueueFamilyIndex_; }

			const VkPhysicalDeviceMemoryProperties& memProps() { return memProps_; }

		private:
			void createInstance();
			void pickPhysicalDevice();
			void createLogicalDevice();

		private:
			VkInstance instance_ = VK_NULL_HANDLE;
			VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
			VkDevice device_ = VK_NULL_HANDLE;

			VkQueue graphicsQueue_ = VK_NULL_HANDLE;
			VkQueue computeQueue_ = VK_NULL_HANDLE;
			VkQueue transferQueue_ = VK_NULL_HANDLE;

			VkPhysicalDeviceMemoryProperties memProps_;

			u32 graphicsQueueFamilyIndex_ = -1;
			u32 computeQueueFamilyIndex_ = -1;
			u32 transferQueueFamilyIndex_ = -1;
		};
	}
}

#endif
