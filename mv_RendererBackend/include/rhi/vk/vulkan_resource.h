#ifndef _MV_VULKAN_RESOURCE_H_
#define _MV_VULKAN_RESOURCE_H_

#include "vulkan/vulkan.h"

#include "util/types.h"

#include "vulkan_memory.h"

namespace mv
{
	namespace backend
	{
		using namespace types;

		struct VulkanBuffer
		{
			rhi::BufferDesc desc;
			VkBuffer buffer = VK_NULL_HANDLE;
			Allocation alloc;

			bool imported = false;
		};

		struct VulkanImage
		{
			rhi::TextureDesc desc;
			VkImage image = VK_NULL_HANDLE;
			VkImageView view = VK_NULL_HANDLE;
			Allocation alloc;

			bool imported = false;
		};
	}
}

#endif