#ifndef _MV_VULKAN_PIPELINE_H_
#define _MV_VULKAN_PIPELINE_H_

#include "vulkan/vulkan.h"

#include "util/types.h"

namespace mv
{
	namespace backend
	{
		class VulkanPipeline
		{
		public:
			VulkanPipeline();
			~VulkanPipeline();



		private:
			VkPipeline pipeline_ = VK_NULL_HANDLE;
			VkPipelineLayout layout_ = VK_NULL_HANDLE;
		};
	}
}

#endif