
#include "rhi/vk/vulkan_command.h"

#include "rhi/vk/vulkan_device.h"
#include "rhi/vk/vulkan_pipeline.h"
#include "rhi/vk/vulkan_resource.h"

namespace mv::backend
{
	VkPipelineStageFlags getPipelineStageFlags(rhi::EResourceState state)
	{
		switch (state)
		{
		case rhi::EResourceState::eUndefined: return VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
		case rhi::EResourceState::eColorAttachment: return VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		case rhi::EResourceState::eCopySrc: return VK_PIPELINE_STAGE_TRANSFER_BIT;
		case rhi::EResourceState::eCopyDst: return VK_PIPELINE_STAGE_TRANSFER_BIT;
		case rhi::EResourceState::eVertexBuffer: return VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;
		case rhi::EResourceState::eIndexBuffer: return VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;
		case rhi::EResourceState::eConstantBuffer: return VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		case rhi::EResourceState::eShaderRead: return VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		case rhi::EResourceState::eShaderWrite: return VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
		case rhi::EResourceState::eRenderTarget: return VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		case rhi::EResourceState::eDepthStencilWrite: return VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
		case rhi::EResourceState::eDepthStencilRead: return VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
		case rhi::EResourceState::ePresent: return VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
		default:
			return 0;
		}
	}

	VkAccessFlags getAccessFlags(rhi::EResourceState state)
	{
		switch (state)
		{
		case rhi::EResourceState::eUndefined: return 0;
		case rhi::EResourceState::eColorAttachment: return VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		case rhi::EResourceState::eCopySrc: return VK_ACCESS_TRANSFER_READ_BIT;
		case rhi::EResourceState::eCopyDst: return VK_ACCESS_TRANSFER_WRITE_BIT;
		case rhi::EResourceState::eVertexBuffer: return VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
		case rhi::EResourceState::eIndexBuffer: return VK_ACCESS_INDEX_READ_BIT;
		case rhi::EResourceState::eConstantBuffer: return VK_ACCESS_UNIFORM_READ_BIT;
		case rhi::EResourceState::eShaderRead: return VK_ACCESS_SHADER_READ_BIT;
		case rhi::EResourceState::eShaderWrite: return VK_ACCESS_SHADER_WRITE_BIT;
		case rhi::EResourceState::eRenderTarget: return VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		case rhi::EResourceState::eDepthStencilWrite: return VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
		case rhi::EResourceState::eDepthStencilRead: return VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
		case rhi::EResourceState::ePresent: return VK_ACCESS_MEMORY_READ_BIT;
		default:
			return 0;
		}
	}

	VkImageLayout getImageLayout(rhi::EResourceState state)
	{
		switch (state)
		{
		case rhi::EResourceState::eUndefined: return VK_IMAGE_LAYOUT_UNDEFINED;
		case rhi::EResourceState::eColorAttachment: return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		case rhi::EResourceState::eCopySrc: return VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
		case rhi::EResourceState::eCopyDst: return VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		case rhi::EResourceState::eVertexBuffer: return VK_IMAGE_LAYOUT_UNDEFINED;
		case rhi::EResourceState::eIndexBuffer: return VK_IMAGE_LAYOUT_UNDEFINED;
		case rhi::EResourceState::eConstantBuffer: return VK_IMAGE_LAYOUT_UNDEFINED;
		case rhi::EResourceState::eShaderRead: return VK_IMAGE_LAYOUT_GENERAL;
		case rhi::EResourceState::eShaderWrite: return VK_IMAGE_LAYOUT_GENERAL;
		case rhi::EResourceState::eRenderTarget: return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		case rhi::EResourceState::eDepthStencilWrite: return VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		case rhi::EResourceState::eDepthStencilRead: return VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
		case rhi::EResourceState::ePresent: return VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
		default:
			return VK_IMAGE_LAYOUT_UNDEFINED;
		}
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
		vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer_);
	}

	void VulkanCommandBuffer::deinitialize(VkDevice device, VkCommandPool commandPool)
	{
		vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer_);
	}

	void VulkanCommandBuffer::begin()
	{
		VkCommandBufferBeginInfo bi{};
		vkBeginCommandBuffer(commandBuffer_, &bi);
	}

	void VulkanCommandBuffer::end()
	{
		vkEndCommandBuffer(commandBuffer_);
	}

	void VulkanCommandBuffer::bindGraphicsPipeline(const VulkanPipeline& pipeline)
	{
		vkCmdBindPipeline(commandBuffer_, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.pipeline());
	}

	void VulkanCommandBuffer::bindVertexBuffer(const VulkanBuffer& buffer)
	{
		VkBuffer vkBuffer = buffer.buffer;
		VkDeviceSize offset = 0;
		vkCmdBindVertexBuffers(commandBuffer_, 0, 1, &vkBuffer, &offset);
	}

	void VulkanCommandBuffer::bindIndexBuffer(const VulkanBuffer& buffer)
	{
		VkBuffer vkBuffer = buffer.buffer;
		VkDeviceSize offset = 0;
		vkCmdBindIndexBuffer(commandBuffer_, vkBuffer, offset, VK_INDEX_TYPE_UINT32);
	}

	void VulkanCommandBuffer::draw(u32 vertexCount, u32 instanceCount, u32 firstVertex, u32 firstInstance)
	{
		vkCmdDraw(commandBuffer_, vertexCount, instanceCount, firstVertex, firstInstance);
	}

	void VulkanCommandBuffer::drawIndexed(u32 indexCount, u32 instanceCount, u32 firstIndex, s32 vertexOffset, u32 firstInstance)
	{
		vkCmdDrawIndexed(commandBuffer_, indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
	}

	void VulkanCommandBuffer::pipelineBarrier(const VulkanStateInfo& before, const VulkanStateInfo& after, const VulkanImage& image)
	{
		VkImageMemoryBarrier barrier{};
		barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		barrier.srcAccessMask = before.access;
		barrier.dstAccessMask = after.access;
		barrier.oldLayout = before.layout;
		barrier.newLayout = after.layout;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.image = image.image;
		barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT; // ToDo: Support different aspect masks
		barrier.subresourceRange.baseMipLevel = 0;
		barrier.subresourceRange.levelCount = 1; // ToDo: Support different mip levels
		barrier.subresourceRange.baseArrayLayer = 0;
		barrier.subresourceRange.layerCount = 1; // ToDo: Support different array layers
		vkCmdPipelineBarrier(
			commandBuffer_,
			before.stage,
			after.stage,
			0,
			0, nullptr,
			0, nullptr,
			1, &barrier);
	}

	void VulkanCommandPool::initialize(VulkanDevice* device, u32 queueFamilyIndex)
	{
		if (commandPool_ != VK_NULL_HANDLE)
			return;

		VkCommandPoolCreateInfo poolCI{};
		poolCI.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
		poolCI.queueFamilyIndex = queueFamilyIndex;
		poolCI.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
		vkCreateCommandPool(device->device(), &poolCI, nullptr, &commandPool_);
		device_ = device;
	}

	void VulkanCommandPool::deinitialize()
	{
		for (const auto& cmd : releaseArray_)
		{
			cmd->deinitialize(device_->device(), commandPool_);
		}

		vkDestroyCommandPool(device_->device(), commandPool_, nullptr);
		commandPool_ = VK_NULL_HANDLE;
	}

	rhi::CommandBufferHandle VulkanCommandPool::createCommandBuffer()
	{
		std::shared_ptr<VulkanCommandBuffer> cmd = std::make_shared<VulkanCommandBuffer>();
		cmd->initialize(device_->device(), commandPool_);

		releaseArray_.push_back(cmd);

		return nextHandleIndex_;
	}

	VulkanCommandBuffer& VulkanCommandPool::getCommandBuffer(rhi::CommandBufferHandle handle) const
	{
		return *releaseArray_[handle];
	}
}
