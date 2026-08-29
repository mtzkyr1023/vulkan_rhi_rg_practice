#ifndef _MV_VULKAN_RESOURCE_H_
#define _MV_VULKAN_RESOURCE_H_

#include <vulkan/vulkan.h>

#include "util/types.h"

#include "vulkan_memory.h"

namespace mv
{
	namespace backend
	{
		namespace vk1_4
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

				// The layout the image was last transitioned to. Vulkan offers no way to ask,
				// and a barrier naming the wrong source layout is undefined, so the backend
				// keeps its own record for callers that cannot know -- a texture written by
				// compute before it has ever been uploaded has no state its owner could name.
				VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;

				bool imported = false;
			};
		}
	}
}

#endif