#include "vulkan_memory.h"

namespace mv::backend
{
	void VulkanMemoryAllocator::initialize(VkDevice device, u64 poolSize, u32 memoryTypeIndex)
	{
		pool_.tlsf.initialize(poolSize);

		VkMemoryAllocateInfo allocInfo{};
		allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		allocInfo.allocationSize = poolSize;
		allocInfo.memoryTypeIndex = memoryTypeIndex;


		vkAllocateMemory(device, &allocInfo, nullptr, &pool_.memory);
	}

	void VulkanMemoryAllocator::deinitialize(VkDevice device)
	{
		vkFreeMemory(device, pool_.memory, nullptr);
	}

	Allocation VulkanMemoryAllocator::allocate(VkDevice device, VkImage image)
	{
		VkMemoryRequirements memReqs;
		vkGetImageMemoryRequirements(device, image, &memReqs);

		memory::Block* block = pool_.tlsf.allocate(memReqs.size, memReqs.alignment);
		if (!block)
			return {};

		return { pool_.memory, block, block->size, block->offset, memReqs.memoryTypeBits };
	}

	Allocation VulkanMemoryAllocator::allocate(VkDevice device, VkBuffer buffer)
	{
		VkMemoryRequirements memReqs;
		vkGetBufferMemoryRequirements(device, buffer, &memReqs);

		memory::Block* block = pool_.tlsf.allocate(memReqs.size, memReqs.alignment);
		if (!block)
			return {};

		return { pool_.memory, block, block->size, block->offset, memReqs.memoryTypeBits };
	}

	void VulkanMemoryAllocator::free(const Allocation& alloc)
	{
		pool_.tlsf.free(alloc.block);
	}
}