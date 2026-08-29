
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

				// Only populated for depth-stencil textures.
				D3D12_CPU_DESCRIPTOR_HANDLE dsv{};

				// Only populated for colour attachments. Kept apart from `cpu` because a
				// render target that is also sampled needs both an RTV and an SRV.
				D3D12_CPU_DESCRIPTOR_HANDLE rtv{};

				// The state the resource was last left in. D3D12 checks every transition's
				// "before" against what it actually tracks, so a path that transitions a
				// texture more than once in its life cannot assume the creation state.
				// Vulkan needs none of this: a transition out of UNDEFINED is always legal
				// when the contents are about to be overwritten.
				D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;

				bool imported = false;
			};
		}
	}
}

#endif
