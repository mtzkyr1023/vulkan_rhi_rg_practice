#include "vulkan_rhi.h"

namespace mv::rhi
{
	void VulkanRHI::initialize(void* hwnd)
	{
		device_.initialize();
		swapchain_.initialize(&device_, hwnd);
		
		for (u32 i = 0; i < (u32)EMemoryType::eNum; i++)
		{
			memoryAllocator_[i].initialize(&device_, 128 * 1024, (EMemoryType)i);
		}

		shaderManager_.initialize(&device_);
		layoutManager_.initialize(&device_);
		pipelineManager_.initialize(&device_, &shaderManager_, &layoutManager_);

		commandPool_.initialize(&device_, device_.graphicsQueueFamilyIndex());
	}
	void VulkanRHI::deinitialize()
	{
		device_.waitIdle();

		shaderManager_.deinitialize();
		layoutManager_.deinitialize();
		pipelineManager_.deinitialize();

		for (u32 i = 0; i < (u32)EMemoryType::eNum; i++)
		{
			memoryAllocator_[i].deinitialize();
		}
		swapchain_.deinitialize();

		device_.deinitialize();
	}

	CommandBufferHandle VulkanRHI::allocateCommandBuffer(EQueueType queueType)
	{
		return commandPool_.allocate();
	}

	BufferHandle VulkanRHI::createBuffer(const BufferDesc& desc)
	{
		return 0;
	}
	TextureHandle VulkanRHI::createTexture(const TextureDesc& desc)
	{
		return 0;
	}
}