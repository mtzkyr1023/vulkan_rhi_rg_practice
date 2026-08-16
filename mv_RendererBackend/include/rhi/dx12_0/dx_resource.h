
#ifndef _MV_DX_RESOURCE_H_
#define _MV_DX_RESOURCE_H_

#include <d3d12.h>

#include <wrl/client.h>

#include "rhi/rhi.h"

#include "util/types.h"

#include "rhi/dx12_0/dx_memory.h"

namespace mv
{
	namespace backend
	{
		namespace dx12_0
		{
			using namespace types;

			namespace wrl = Microsoft::WRL;

			struct DxBuffer
			{
				rhi::BufferDesc desc;
				wrl::ComPtr<ID3D12Resource> resource;

				Allocation alloc;

				u32 descriptorIndex = INVALID_HANDLE;
				D3D12_CPU_DESCRIPTOR_HANDLE cpu;
				D3D12_GPU_DESCRIPTOR_HANDLE gpu;

				bool imported = false;
			};

			struct DxImage
			{
				rhi::TextureDesc desc;
				wrl::ComPtr<ID3D12Resource> resource;

				Allocation alloc;

				u32 descriptorIndex = INVALID_HANDLE;
				D3D12_CPU_DESCRIPTOR_HANDLE cpu;
				D3D12_GPU_DESCRIPTOR_HANDLE gpu;

				bool imported = false;
			};
		}
	}
}

#endif
