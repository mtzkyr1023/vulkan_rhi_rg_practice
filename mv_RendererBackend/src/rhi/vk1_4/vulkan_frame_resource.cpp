
#include "rhi/vk1_4/vulkan_frame_resource.h"
#include "rhi/vk1_4/vulkan_device.h"
#include "rhi/vk1_4/vulkan_command.h"

namespace mv::backend::vk1_4
{
	void VulkanFrameResource::initialize(VulkanDevice* device, VulkanCommandPool* commandPool)
	{
		commandBuffer = commandPool->allocate();
		VkSemaphoreCreateInfo semaphoreCI{};
		semaphoreCI.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
		vkCreateSemaphore(device->device(), &semaphoreCI, nullptr, &imageAvailableSemaphore);

		VkFenceCreateInfo fenceCI{};
		fenceCI.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
		fenceCI.flags = VK_FENCE_CREATE_SIGNALED_BIT;
		vkCreateFence(device->device(), &fenceCI, nullptr, &inFlightFence);
	}
	void VulkanFrameResource::deinitialize(VulkanDevice* device, VulkanCommandPool* commandPool)
	{
		vkDestroySemaphore(device->device(), imageAvailableSemaphore, nullptr);
		vkDestroyFence(device->device(), inFlightFence, nullptr);
	}
}