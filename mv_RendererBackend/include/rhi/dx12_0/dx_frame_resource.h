
#ifndef _MV_DX_FRAME_RESOURCE_H_
#define _MV_DX_FRAME_RESOURCE_H_

#include <d3d12.h>

#include <wrl/client.h>

#include "rhi/rhi.h"

namespace mv
{
	namespace backend
	{
		namespace dx12_0
		{
			namespace wrl = Microsoft::WRL;

			using namespace types;

			class DxDevice;
			class DxCommandPool;

			struct DxFrameResource
			{
				void initialize(DxDevice* device, DxCommandPool* commandPool);
				void deinitialize(DxDevice* device, DxCommandPool* commandPool);

				rhi::CommandBufferHandle commandBuffer = INVALID_HANDLE;
				wrl::ComPtr<ID3D12CommandAllocator> allocator;
				wrl::ComPtr<ID3D12Fence> inFlightFence;

				rhi::TextureHandle backbuffer = INVALID_HANDLE;

				u64 fenceValue = 0;
			};
		}
	}
}

#endif