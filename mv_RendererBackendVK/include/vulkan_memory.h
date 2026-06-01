#ifndef _MV_VULKAN_MEMORY_H_
#define _MV_VULKAN_MEMORY_H_

#include "vulkan/vulkan.h"

#include "util/types.h"
#include "memory/tlsf_allocator.h"

namespace mv
{
	namespace backend
	{
		using namespace types;
		
		enum class EMemoryType : u8
		{
			eDeviceLocalImage = 0,
			eDeviceLocalBuffer,
			eHostVisible,
			eHostCoherent,
			eHostCached,
			eNum,
		};

		struct Allocation
		{
			VkDeviceMemory memory = VK_NULL_HANDLE;

			memory::Block* block = nullptr;

			u64 size = 0;
			u64 offset = 0;
			u32 memoryTypeIndex = 0;
		};

		struct MemoryPool
		{
			VkDeviceMemory memory = VK_NULL_HANDLE;
			memory::TLSF tlsf;
		};

		class VulkanMemoryAllocator
		{
		public:
			VulkanMemoryAllocator();
			~VulkanMemoryAllocator();

			void initialize(VkDevice device, u64 poolSize, u32 memoryTypeIndex);
			void deinitialize(VkDevice device);

			Allocation allocate(VkDevice device, VkImage image);
			Allocation allocate(VkDevice device, VkBuffer buffer);
			void free(const Allocation& alloc);

		private:
			MemoryPool pool_;
		};
	}
}

#endif