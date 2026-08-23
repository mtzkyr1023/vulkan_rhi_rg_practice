#include "rhi/vk1_4/vulkan_memory.h"
#include "rhi/vk1_4/vulkan_device.h"

#include <cassert>

namespace mv::backend::vk1_4
{
	static const VkMemoryPropertyFlags MemoryTypeTable[] =
	{
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
		// Readback: cached as well as visible, so the CPU is not reading over the bus one
		// uncached word at a time.
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT,
	};

	void VulkanMemoryAllocator::initialize(VulkanDevice* device, u64 poolSize, rhi::EMemoryType type)
	{
		device_ = device;
		poolSize_ = poolSize;
		type_ = type;
	}

	u32 VulkanMemoryAllocator::addPool(u32 memoryTypeIndex, u64 minimumSize)
	{
		// A single allocation may be larger than the default pool size, so the new pool has
		// to be at least big enough for the request that triggered it.
		const u64 size = (minimumSize > poolSize_) ? minimumSize : poolSize_;

		auto pool = std::make_unique<MemoryPool>();
		pool->tlsf.initialize(size);
		pool->memoryTypeIndex = memoryTypeIndex;

		VkMemoryAllocateInfo allocInfo{};
		allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		allocInfo.allocationSize = size;
		allocInfo.memoryTypeIndex = memoryTypeIndex;

		if (vkAllocateMemory(device_->device(), &allocInfo, nullptr, &pool->memory) != VK_SUCCESS)
		{
			throw std::exception("Failed to allocate device memory pool");
		}

		// Persistently map host-visible pools: Vulkan allows a VkDeviceMemory to stay
		// mapped for its whole lifetime, and doing it once avoids a map/unmap per write.
		if (device_->memProps().memoryTypes[memoryTypeIndex].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)
		{
			vkMapMemory(device_->device(), pool->memory, 0, size, 0, &pool->mapped);
		}

		pools_.push_back(std::move(pool));

		return (u32)pools_.size() - 1;
	}

	Allocation VulkanMemoryAllocator::allocateFromPools(const VkMemoryRequirements& memReqs)
	{
		const u32 memoryTypeIndex = findMemoryTypeIndex(memReqs.memoryTypeBits, MemoryTypeTable[(u32)type_]);

		for (u32 i = 0; i < (u32)pools_.size(); i++)
		{
			if (pools_[i]->memoryTypeIndex != memoryTypeIndex)
				continue;

			memory::Block* block = pools_[i]->tlsf.allocate(memReqs.size, memReqs.alignment);
			if (!block)
				continue;

			return { pools_[i]->memory, block, block->size, block->offset, memoryTypeIndex, i };
		}

		// Nothing had room: grow rather than fail.
		const u32 poolIndex = addPool(memoryTypeIndex, memory::TLSF::requiredPoolSize(memReqs.size, memReqs.alignment));

		memory::Block* block = pools_[poolIndex]->tlsf.allocate(memReqs.size, memReqs.alignment);
		if (!block)
		{
			throw std::exception("Out of pool memory");
		}

		return { pools_[poolIndex]->memory, block, block->size, block->offset, memoryTypeIndex, poolIndex };
	}

	void VulkanMemoryAllocator::deinitialize()
	{
		for (auto& pool : pools_)
		{
			if (pool->mapped)
			{
				vkUnmapMemory(device_->device(), pool->memory);
				pool->mapped = nullptr;
			}

			vkFreeMemory(device_->device(), pool->memory, nullptr);
			pool->memory = VK_NULL_HANDLE;
		}

		pools_.clear();
	}

	Allocation VulkanMemoryAllocator::allocate(VkImage image)
	{
		VkMemoryRequirements memReqs;
		vkGetImageMemoryRequirements(device_->device(), image, &memReqs);

		return allocateFromPools(memReqs);
	}

	Allocation VulkanMemoryAllocator::allocate(VkBuffer buffer)
	{
		VkMemoryRequirements memReqs;
		vkGetBufferMemoryRequirements(device_->device(), buffer, &memReqs);

		return allocateFromPools(memReqs);
	}

	void VulkanMemoryAllocator::free(const Allocation& alloc)
	{
		if (!alloc.block) return;

		// The block belongs to exactly one pool; handing it to every pool's TLSF corrupts
		// the free lists of all the others.
		pools_[alloc.poolIndex]->tlsf.free(alloc.block);
	}

	void* VulkanMemoryAllocator::map(const Allocation& alloc)
	{
		if (alloc.poolIndex >= pools_.size()) return nullptr;

		u8* base = static_cast<u8*>(pools_[alloc.poolIndex]->mapped);
		if (!base) return nullptr;

		return base + alloc.offset;
	}

	u32 VulkanMemoryAllocator::findMemoryTypeIndex(u32 typeBits, VkMemoryPropertyFlags properties)
	{
		const VkPhysicalDeviceMemoryProperties& memProps = device_->memProps();
		for (u32 i = 0; i < device_->memProps().memoryTypeCount; i++)
		{
			// typeBits is a bitmask of allowed memory type indices: bit i means type i.
			if (typeBits & (1u << i))
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