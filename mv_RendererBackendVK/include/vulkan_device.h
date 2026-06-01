
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
			VulkanDevice();
			~VulkanDevice();

			void initialize();
			void deinitialize();

			VkInstance instance() { return instance_;}
			VkPhysicalDevice physicalDevice() { return physicalDevice_; }
			VkDevice device() { return device_; }

		private:
			VkInstance instance_ = VK_NULL_HANDLE;
			VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
			VkDevice device_ = VK_NULL_HANDLE;
		};
	}
}

#endif
