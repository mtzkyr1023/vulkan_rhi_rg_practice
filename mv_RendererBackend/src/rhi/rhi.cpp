
#include "rhi/rhi.h"
#include "rhi/vk/vulkan_rhi.h"


namespace mv
{
	namespace rhi
	{
		std::shared_ptr<IRHI> IRHI::createVulkanRHI()
		{
			return std::make_shared<VulkanRHI>();
		}
	}
}