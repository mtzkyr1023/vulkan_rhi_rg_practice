

#include "rhi/rhi.h"

#include "vulkan_renderer.h"

namespace mv::rhi
{
	struct VulkanRHI::Impl
	{
		renderer::VulkanRenderer renderer;
	};

	VulkanRHI::VulkanRHI()
	{
		impl_ = std::make_shared<Impl>();
	}

	VulkanRHI::~VulkanRHI()
	{
	}

	void VulkanRHI::initialize(void* hwnd)
	{
		impl_->renderer.initialize(hwnd);
	}

	void VulkanRHI::deinitialize()
	{
		impl_->renderer.deinitialize();
	}

	BufferHandle VulkanRHI::createBuffer(const BufferDesc& desc)
	{

	}
}