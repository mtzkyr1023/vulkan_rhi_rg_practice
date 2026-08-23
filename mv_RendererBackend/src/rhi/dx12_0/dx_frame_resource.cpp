
#include "rhi/dx12_0/dx_frame_resource.h"
#include "rhi/dx12_0/dx_device.h"
#include "rhi/dx12_0/dx_command.h"

namespace mv::backend::dx12_0
{
	void DxFrameResource::initialize(DxDevice* device, DxCommandPool* commandPool)
	{
		// Each frame in flight needs its own allocator: D3D12 forbids resetting an
		// allocator while any command list created from it may still be executing on
		// the GPU, so a single allocator can't safely be shared across frames in flight.
		device->device()->CreateCommandAllocator(commandPool->type(), IID_PPV_ARGS(&allocator));

		commandBuffer = commandPool->allocate();

		device->device()->CreateFence(fenceValue, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&inFlightFence));
	}

	void DxFrameResource::deinitialize(DxDevice* device, DxCommandPool* commandPool)
	{
		inFlightFence.Reset();
		allocator.Reset();
	}
}
