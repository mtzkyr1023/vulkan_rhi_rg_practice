#ifndef _MV_VULKAN_PIPELINE_H_
#define _MV_VULKAN_PIPELINE_H_

#include "vulkan/vulkan.h"

#include "util/types.h"

namespace mv
{
	namespace backend
	{
		struct VulkanPipeline
		{
			VkPipeline pipeline = VK_NULL_HANDLE;
			VkPipelineLayout layout = VK_NULL_HANDLE;
		};
	}
}

#endif