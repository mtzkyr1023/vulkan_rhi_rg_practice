
#include "vulkan_command.h"

namespace mv::backend
{
	VulkanCommandBuffer::VulkanCommandBuffer()
	{
	}

	VulkanCommandBuffer::~VulkanCommandBuffer()
	{
	}

	void VulkanCommandBuffer::initialize(VkDevice device, VkCommandPool commandPool)
	{
		if (commandBuffer_ != VK_NULL_HANDLE)
			return;

		VkCommandBufferAllocateInfo allocInfo{};
		allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		allocInfo.commandPool = commandPool;
		allocInfo.commandBufferCount = 1;
		allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	}

	void VulkanCommandBuffer::deinitialize(VkDevice device, VkCommandPool commandPool)
	{
		vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer_);
	}

	void VulkanCommandBuffer::begin()
	{
	}

	void VulkanCommandBuffer::end()
	{
	}

	void VulkanCommandBuffer::bindVertexBuffer(rhi::BufferHandle buffer)
	{
	}

	void VulkanCommandBuffer::bindIndexBuffer(rhi::BufferHandle buffer)
	{
	}

	void VulkanCommandBuffer::draw(u32 vertexCount, u32 instanceCount, u32 firstVertex, u32 firstInstance)
	{
	}

	void VulkanCommandBuffer::drawIndexed(u32 indexCount, u32 instanceCount, u32 firstIndex, s32 vertexOffset, u32 firstInstance)
	{
	}


	VulkanCommandPool::VulkanCommandPool()
	{

	}

	VulkanCommandPool::~VulkanCommandPool()
	{

	}

	void VulkanCommandPool::initialize(VkDevice device, u32 queueFamilyIndex)
	{
		if (commandPool_ != VK_NULL_HANDLE)
			return;
		VkCommandPoolCreateInfo poolCI{};
		poolCI.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
		poolCI.queueFamilyIndex = queueFamilyIndex;
		poolCI.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
		vkCreateCommandPool(device, &poolCI, nullptr, &commandPool_);
		device_ = device;
	}

	void VulkanCommandPool::deinitialize(VkDevice device)
	{
		for (const auto& cmd : releaseArray_)
		{
			cmd->deinitialize(device, commandPool_);
		}

		vkDestroyCommandPool(device, commandPool_, nullptr);
		commandPool_ = VK_NULL_HANDLE;
	}

	rhi::CommandBufferHandle VulkanCommandPool::createCommandBuffer()
	{
		std::shared_ptr<VulkanCommandBuffer> cmd = std::make_shared<VulkanCommandBuffer>();
		cmd->initialize(device_, commandPool_);

		releaseArray_.push_back(cmd);

		return nextHandleIndex_;
	}
}
