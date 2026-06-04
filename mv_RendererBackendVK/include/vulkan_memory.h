#ifndef _MV_VULKAN_MEMORY_H_
#define _MV_VULKAN_MEMORY_H_

#include "array"

#include "vulkan/vulkan.h"

#include "util/types.h"
#include "memory/tlsf_allocator.h"

namespace mv
{
	namespace backend
	{
		using namespace types;

		class VulkanDevice;
		
		enum class EMemoryType : u8
		{
			eDeviceLocalImage = 0,
			eDeviceLocalBuffer,
			eHostVisibleImage,
			eHostVisibleBuffer,
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

			u32 memoryTypeIndex = -1;
		};

		class VulkanMemoryAllocator
		{
		public:

			void initialize(VulkanDevice* device, u64 poolSize, EMemoryType type);
			void deinitialize();

			Allocation allocate(VkImage image);
			Allocation allocate(VkBuffer buffer);
			void free(const Allocation& alloc);

		private:
			u32 findMemoryTypeIndex(u32 typeBits, VkMemoryPropertyFlags properties);

		private:
			std::array<MemoryPool, VK_MAX_MEMORY_TYPES> pools_;

			VulkanDevice* device_ = nullptr;

			EMemoryType type_;
		};

	}
}

#endif