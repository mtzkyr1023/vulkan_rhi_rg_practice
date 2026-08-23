#ifndef _MV_VULKAN_MEMORY_H_
#define _MV_VULKAN_MEMORY_H_

#include <memory>
#include <vector>

#include <vulkan/vulkan.h>

#include "util/types.h"
#include "memory/tlsf_allocator.h"

#include "rhi/resource.h"

namespace mv
{
	namespace backend
	{
		namespace vk1_4
		{
			using namespace types;

			class VulkanDevice;

			struct Allocation
			{
				VkDeviceMemory memory = VK_NULL_HANDLE;

				memory::Block* block = nullptr;

				u64 size = 0;
				u64 offset = 0;
				u32 memoryTypeIndex = 0;

				// Which pool the block came from; free() and map() must use that one.
				u32 poolIndex = 0;
			};

			struct MemoryPool
			{
				VkDeviceMemory memory = VK_NULL_HANDLE;
				memory::TLSF tlsf;

				// Host-visible pools are mapped once for their whole lifetime; per-allocation
				// pointers are just an offset from here.
				void* mapped = nullptr;

				u32 memoryTypeIndex = -1;
			};

			class VulkanMemoryAllocator
			{
			public:

				void initialize(VulkanDevice* device, u64 poolSize, rhi::EMemoryType type);
				void deinitialize();

				Allocation allocate(VkImage image);
				Allocation allocate(VkBuffer buffer);
				void free(const Allocation& alloc);

				// Returns null for allocations that came from a pool that is not host visible.
				void* map(const Allocation& alloc);

			private:
				u32 findMemoryTypeIndex(u32 typeBits, VkMemoryPropertyFlags properties);

				// Finds room in an existing pool of the right memory type, adding one if
				// none has space. Pools are created on demand rather than one per memory
				// type up front, which would reserve poolSize x memoryTypeCount per
				// allocator, and a single fixed pool cannot hold a real model.
				Allocation allocateFromPools(const VkMemoryRequirements& memReqs);

				u32 addPool(u32 memoryTypeIndex, u64 minimumSize);

			private:
				// unique_ptr, not by value: growing the vector would relocate the TLSF, whose
				// free lists point into its own block storage.
				std::vector<std::unique_ptr<MemoryPool>> pools_;

				VulkanDevice* device_ = nullptr;

				u64 poolSize_ = 0;

				rhi::EMemoryType type_;
			};
		}
	}
}

#endif