
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

			VkInstance instance() { return instance_;}
			VkPhysicalDevice physicalDevice() { return physicalDevice_; }
			VkDevice device() { return device_; }

			VkQueue graphicsQueue() { return graphicsQueue_; }
			VkQueue computeQueue() { return computeQueue_; }
			VkQueue transferQueue() { return transferQueue_; }

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

			u32 graphicsQueueFamilyIndex_ = -1;
			u32 computeQueueFamilyIndex_ = -1;
			u32 transferQueueFamilyIndex_ = -1;
		};
	}
}

#endif
