
#include "rhi/dx12_0/dx_memory.h"
#include "rhi/dx12_0/dx_device.h"

namespace mv::backend::dx12_0
{
	void DxMemoryAllocator::initialize(DxDevice* device, u64 poolSize, rhi::EMemoryType type)
	{
		//for (u32 i = 0; i < (u32)device->memProps().memoryTypeCount; i++)
		//{
		//	pools_[i].tlsf.initialize(poolSize);
		//	pools_[i].memoryTypeIndex = i;

		//	VkMemoryAllocateInfo allocInfo{};
		//	allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		//	allocInfo.allocationSize = poolSize;
		//	allocInfo.memoryTypeIndex = i;


		//	vkAllocateMemory(device->device(), &allocInfo, nullptr, &pools_[i].memory);
		//}

		pool_.tlsf.initialize(poolSize);
		
		D3D12_HEAP_DESC desc{};
		desc.SizeInBytes = poolSize;
		desc.Flags = (type == rhi::EMemoryType::eDeviceLocalBuffer || type == rhi::EMemoryType::eHostVisibleBuffer) ? D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS : D3D12_HEAP_FLAG_;
		desc.Properties.Type =
			(type == rhi::EMemoryType::eDeviceLocalBuffer || type == rhi::EMemoryType::eDeviceLocalImage) ?
			D3D12_HEAP_TYPE_DEFAULT :
			D3D12_HEAP_TYPE_UPLOAD;

		device->device()->CreateHeap(&desc, IID_PPV_ARGS(&pool_.heap));

		device_ = device;
		type_ = type;
	}

	void DxMemoryAllocator::deinitialize()
	{
		//for (u32 i = 0; i < (u32)device_->memProps().memoryTypeCount; i++)
		//{
		//	vkFreeMemory(device_->device(), pools_[i].memory, nullptr);
		//}
	}

	Allocation DxMemoryAllocator::allocate(ID3D12Resource* resource)
	{
		VkMemoryRequirements memReqs;
		vkGetImageMemoryRequirements(device_->device(), image, &memReqs);

		u32 memoryTypeIndex = findMemoryTypeIndex(memReqs.memoryTypeBits, MemoryTypeTable[(u32)type_]);


		memory::Block* block = pools_[memoryTypeIndex].tlsf.allocate(memReqs.size, memReqs.alignment);
		if (!block)
			return {};

		return { pools_[memoryTypeIndex].memory, block, block->size, block->offset, memReqs.memoryTypeBits };
	}

	void DxMemoryAllocator::free(const Allocation& alloc)
	{
		for (auto& pool : pools_)
		{
			pool.tlsf.free(alloc.block);
		}
	}	
}