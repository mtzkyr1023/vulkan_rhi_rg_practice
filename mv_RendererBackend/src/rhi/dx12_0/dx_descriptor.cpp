
#include "rhi/dx12_0/dx_descriptor.h"
#include "rhi/dx12_0/dx_device.h"

namespace mv::backend::dx12_0
{
	void DxDescriptorAllocator::initialize(DxDevice* device, u32 frameInFlight, D3D12_DESCRIPTOR_HEAP_TYPE type, bool global)
	{
		frames_.resize(frameInFlight);

		heapType_ = type;
		device_ = device;
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

		desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		desc.NumDescriptors = maxDescs;

		device_->device()->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&wrapper.heap));

		return wrapper;
	}
}