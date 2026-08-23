
#include "rhi/dx12_0/dx_memory.h"
#include "rhi/dx12_0/dx_device.h"

namespace mv::backend::dx12_0
{
	void DxMemoryAllocator::initialize(DxDevice* device, u64 poolSize, rhi::EMemoryType type)
	{
		device_ = device;
		poolSize_ = poolSize;
		type_ = type;
	}

	MemoryPool& DxMemoryAllocator::addPool(u64 minimumSize)
	{
		// A single allocation may be larger than the default pool size, so the new pool has
		// to be at least big enough for the request that triggered it.
		const u64 size = (minimumSize > poolSize_) ? minimumSize : poolSize_;

		const bool isReadback = (type_ == rhi::EMemoryType::eReadback);
		const bool isBuffer = (type_ == rhi::EMemoryType::eDeviceLocalBuffer || type_ == rhi::EMemoryType::eHostVisibleBuffer || isReadback);
		const bool isDeviceLocal = (type_ == rhi::EMemoryType::eDeviceLocalBuffer || type_ == rhi::EMemoryType::eDeviceLocalImage);

		D3D12_HEAP_DESC desc{};
		desc.SizeInBytes = size;
		desc.Properties.Type =
			isReadback ? D3D12_HEAP_TYPE_READBACK :
			isDeviceLocal ? D3D12_HEAP_TYPE_DEFAULT : D3D12_HEAP_TYPE_UPLOAD;

		// An UPLOAD heap can only ever hold buffers, so a host-visible "image" pool is still
		// a buffer heap — that is what texture staging uploads need anyway.
		if (isBuffer || !isDeviceLocal)
		{
			desc.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS;
		}
		else if (device_->resourceHeapTier() >= D3D12_RESOURCE_HEAP_TIER_2)
		{
			// Depth targets are RT/DS textures and cannot live in a NON_RT_DS heap, so the
			// device-local image pool has to accept everything.
			desc.Flags = D3D12_HEAP_FLAG_ALLOW_ALL_BUFFERS_AND_TEXTURES;
		}
		else
		{
			throw std::exception("Resource heap tier 1 is not supported: RT/DS and other textures would need separate pools");
		}

		auto pool = std::make_unique<MemoryPool>();
		pool->tlsf.initialize(size);

		if (FAILED(device_->device()->CreateHeap(&desc, IID_PPV_ARGS(&pool->heap))))
		{
			throw std::exception("Failed to create memory heap");
		}

		pools_.push_back(std::move(pool));

		return *pools_.back();
	}

	void DxMemoryAllocator::deinitialize()
	{
		pools_.clear();
	}

	Allocation DxMemoryAllocator::allocate(const D3D12_RESOURCE_DESC& desc)
	{
		const D3D12_RESOURCE_ALLOCATION_INFO info = device_->device()->GetResourceAllocationInfo(0, 1, &desc);

		for (u32 i = 0; i < (u32)pools_.size(); i++)
		{
			memory::Block* block = pools_[i]->tlsf.allocate(info.SizeInBytes, info.Alignment);
			if (!block)
				continue;

			Allocation alloc{};
			alloc.heap = pools_[i]->heap;
			alloc.block = block;
			alloc.size = block->size;
			alloc.offset = block->offset;
			alloc.poolIndex = i;

			return alloc;
		}

		// Nothing had room: grow rather than fail. Returning an empty Allocation here would
		// only move the failure to CreatePlacedResource as a corrupt-parameter error.
		MemoryPool& pool = addPool(memory::TLSF::requiredPoolSize(info.SizeInBytes, info.Alignment));

		memory::Block* block = pool.tlsf.allocate(info.SizeInBytes, info.Alignment);
		if (!block)
		{
			throw std::exception("Out of pool memory");
		}

		Allocation alloc{};
		alloc.heap = pool.heap;
		alloc.block = block;
		alloc.size = block->size;
		alloc.offset = block->offset;
		alloc.poolIndex = (u32)pools_.size() - 1;

		return alloc;
	}

	void DxMemoryAllocator::free(const Allocation& alloc)
	{
		if (!alloc.block) return;

		pools_[alloc.poolIndex]->tlsf.free(alloc.block);
	}
}
