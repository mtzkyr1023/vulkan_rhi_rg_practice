
#include "rhi/dx12_0/dx_descriptor.h"
#include "rhi/dx12_0/dx_device.h"

namespace mv::backend::dx12_0
{
	void DxDescriptorAllocator::initialize(DxDevice* device, u32 framesInFlight, u32 maxDescriptors, D3D12_DESCRIPTOR_HEAP_TYPE type, bool global)
	{
		heapType_ = type;
		maxDescs_ = maxDescriptors;
		global_ = global;
		device_ = device;

		frames_.resize(framesInFlight);
		for (auto& frame : frames_)
		{
			DescriptorHeapWrapper heap;
			switch (heapType_)
			{
			case D3D12_DESCRIPTOR_HEAP_TYPE_RTV:
				heap = createRtvHeap(maxDescs_);
				break;
			case D3D12_DESCRIPTOR_HEAP_TYPE_DSV:
				heap = createDsvHeap(maxDescs_);
				break;
			case D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER:
				heap = createGlobalHeap(maxDescs_);
				break;
			default:
				heap = global_ ? createGlobalHeap(maxDescs_) : createSrvHeap(maxDescs_);
				break;
			}

			frame.heaps.push_back(heap);
			frame.currentHeap = 0;
		}
	}

	void DxDescriptorAllocator::deinitialize()
	{

	}

	void DxDescriptorAllocator::beginFrame(u32 frameIndex)
	{
		currentFrame_ = frameIndex;

		auto& frame = frames_[currentFrame_];

		frame.currentHeap = 0;

		for (auto& heap : frame.heaps)
		{
			heap.usedDescs = 0;
		}
	}

	u32 DxDescriptorAllocator::allocate()
	{
		return allocateRange(1);
	}

	u32 DxDescriptorAllocator::allocateRange(u32 count)
	{
		auto& frame = frames_[currentFrame_];
		DescriptorHeapWrapper* heap = &frame.heaps[frame.currentHeap];

		if (heap->usedDescs + count > heap->maxDescs)
		{
			DescriptorHeapWrapper newHeap;
			switch (heapType_)
			{
			case D3D12_DESCRIPTOR_HEAP_TYPE_RTV:
				newHeap = createRtvHeap(maxDescs_);
				break;
			case D3D12_DESCRIPTOR_HEAP_TYPE_DSV:
				newHeap = createDsvHeap(maxDescs_);
				break;
			default:
				newHeap = global_ ? createGlobalHeap(maxDescs_) : createSrvHeap(maxDescs_);
				break;
			}

			frame.heaps.push_back(newHeap);
			frame.currentHeap = (u32)frame.heaps.size() - 1;
			heap = &frame.heaps[frame.currentHeap];
		}

		const u32 first = heap->usedDescs;
		heap->usedDescs += count;

		return first;
	}

	ID3D12DescriptorHeap* DxDescriptorAllocator::heap() const
	{
		const auto& frame = frames_[currentFrame_];
		return frame.heaps[frame.currentHeap].heap.Get();
	}

	D3D12_CPU_DESCRIPTOR_HANDLE DxDescriptorAllocator::getCpuHandle(u32 index) const
	{
		const auto& frame = frames_[currentFrame_];
		const auto& heap = frame.heaps[frame.currentHeap];

		D3D12_CPU_DESCRIPTOR_HANDLE handle = heap.heap->GetCPUDescriptorHandleForHeapStart();
		handle.ptr += (SIZE_T)index * device_->device()->GetDescriptorHandleIncrementSize(heapType_);
		return handle;
	}

	D3D12_GPU_DESCRIPTOR_HANDLE DxDescriptorAllocator::getGpuHandle(u32 index) const
	{
		const auto& frame = frames_[currentFrame_];
		const auto& heap = frame.heaps[frame.currentHeap];

		D3D12_GPU_DESCRIPTOR_HANDLE handle = heap.heap->GetGPUDescriptorHandleForHeapStart();
		handle.ptr += (UINT64)index * device_->device()->GetDescriptorHandleIncrementSize(heapType_);
		return handle;
	}

	DxDescriptorAllocator::DescriptorHeapWrapper DxDescriptorAllocator::createRtvHeap(u32 maxDescs)
	{
		DescriptorHeapWrapper wrapper;

		wrapper.maxDescs = maxDescs;

		D3D12_DESCRIPTOR_HEAP_DESC desc{};

		desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
		desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		desc.NumDescriptors = maxDescs;

		device_->device()->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&wrapper.heap));

		return wrapper;
	}

	DxDescriptorAllocator::DescriptorHeapWrapper DxDescriptorAllocator::createDsvHeap(u32 maxDescs)
	{
		DescriptorHeapWrapper wrapper;

		wrapper.maxDescs = maxDescs;

		D3D12_DESCRIPTOR_HEAP_DESC desc{};

		desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
		desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		desc.NumDescriptors = maxDescs;

		device_->device()->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&wrapper.heap));

		return wrapper;
	}

	DxDescriptorAllocator::DescriptorHeapWrapper DxDescriptorAllocator::createSrvHeap(u32 maxDescs)
	{
		DescriptorHeapWrapper wrapper;

		wrapper.maxDescs = maxDescs;

		D3D12_DESCRIPTOR_HEAP_DESC desc{};

		desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		desc.NumDescriptors = maxDescs;

		device_->device()->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&wrapper.heap));

		return wrapper;
	}

	DxDescriptorAllocator::DescriptorHeapWrapper DxDescriptorAllocator::createGlobalHeap(u32 maxDescs)
	{
		DescriptorHeapWrapper wrapper;

		wrapper.maxDescs = maxDescs;

		D3D12_DESCRIPTOR_HEAP_DESC desc{};

		// Follows heapType_ so the same path can produce a shader-visible sampler heap;
		// samplers may not share a heap with CBV/SRV/UAV descriptors.
		desc.Type = heapType_;
		desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		desc.NumDescriptors = maxDescs;

		device_->device()->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&wrapper.heap));

		return wrapper;
	}
}