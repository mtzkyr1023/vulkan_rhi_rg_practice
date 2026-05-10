
#include "rhi/commandbuffer.h"
#include "rhi/rhi.h"

namespace mv::rhi
{
	VulkanCommandBuffer::VulkanCommandBuffer(const std::shared_ptr<VulkanRHI>& rhi)
		: rhi_(rhi)
		, handle_(INVALID_HANDLE)
	{
	}

	VulkanCommandBuffer::~VulkanCommandBuffer()
	{
	}

	void VulkanCommandBuffer::begin()
	{
	}
}
