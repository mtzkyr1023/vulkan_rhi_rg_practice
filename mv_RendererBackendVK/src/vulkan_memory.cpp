#include "vulkan_memory.h"
#include "vulkan_device.h"

#include "cassert"

namespace mv::backend
{
	static const VkMemoryPropertyFlags MemoryTypeTable[] =
	{
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
	};

	void VulkanMemoryAllocator::initialize(VulkanDevice* device, u64 poolSize, EMemoryType type)
	{
		for (u32 i = 0; i < (u32)pools_.size(); i++)
		{
			pools_[i].tlsf.initialize(poolSize);
			pools_[i].memoryTypeIndex = i;

			VkMemoryAllocateInfo allocInfo{};
			allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
			allocInfo.allocationSize = poolSize;
			allocInfo.memoryTypeIndex = i;


			vkAllocateMemory(device->device(), &allocInfo, nullptr, &pools_[i].memory);
		}

		device_ = device;
		type_ = type;
	}

	void VulkanMemoryAllocator::deinitialize()
	{
		for (u32 i = 0; i < (u32)pools_.size(); i++)
		{
			vkFreeMemory(device_->device(), pools_[i].memory, nullptr);
		}
	}

	Allocation VulkanMemoryAllocator::allocate(VkImage image)
	{
		VkMemoryRequirements memReqs;
		vkGetImageMemoryRequirements(device_->device(), image, &memReqs);

		u32 memoryTypeIndex = findMemoryTypeIndex(memReqs.memoryTypeBits, MemoryTypeTable[(u32)type_]);


		memory::Block* block = pools_[memoryTypeIndex].tlsf.allocate(memReqs.size, memReqs.alignment);
		if (!block)
			return {};

		return { pools_[memoryTypeIndex].memory, block, block->size, block->offset, memReqs.memoryTypeBits};
	}

	Allocation VulkanMemoryAllocator::allocate(VkBuffer buffer)
	{
		VkMemoryRequirements memReqs;
		vkGetBufferMemoryRequirements(device_->device(), buffer, &memReqs);

		u32 memoryTypeIndex = findMemoryTypeIndex(memReqs.memoryTypeBits, MemoryTypeTable[(u32)type_]);


		memory::Block* block = pools_[memoryTypeIndex].tlsf.allocate(memReqs.size, memReqs.alignment);
		if (!block)
			return {};

		return { pools_[memoryTypeIndex].memory, block, block->size, block->offset, memReqs.memoryTypeBits };
	}

	void VulkanMemoryAllocator::free(const Allocation& alloc)
	{
		for (auto& pool : pools_)
		{
			pool.tlsf.free(alloc.block);
		}
	}

	u32 VulkanMemoryAllocator::findMemoryTypeIndex(u32 typeBits, VkMemoryPropertyFlags properties)
	{
		const VkPhysicalDeviceMemoryProperties& memProps = device_->memProps();
		for (u32 i = 0; i < device_->memProps().memoryTypeCount; i++)
		{
			if (typeBits & (i << 1))
			{
				if ((memProps.memoryTypes[i].propertyFlags & properties) == properties)
				{
					return i;
				}
			}
		}

		assert(false);

		return 0;
	}
}