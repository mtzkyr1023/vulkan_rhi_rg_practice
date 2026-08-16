
#include "rhi/rhi.h"
#include "rhi/vk1_4/vulkan_rhi.h"


namespace mv
{
	namespace rhi
	{
		std::shared_ptr<IRHI> IRHI::createVulkanRHI()
		{
			return std::make_shared<backend::vk1_4::VulkanRHI>();
		}
	}
}