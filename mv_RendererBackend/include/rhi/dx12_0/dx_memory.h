
#ifndef _MV_DX_ALLOCATOR_H_
#define _MV_DX_ALLOCATOR_H_

#include <d3d12.h>

#include <wrl/client.h>

#include "memory/tlsf_allocator.h"

#include "rhi/resource.h"

namespace mv
{
	namespace backend
	{
		namespace dx12_0
		{
			namespace wrl = Microsoft::WRL;
			using namespace types;

			class DxDevice;

			struct Allocation
			{
				wrl::ComPtr<ID3D12Heap> heap;

				memory::Block* block = nullptr;

				u64 size = 0;
				u64 offset = 0;
			};

			struct MemoryPool
			{
				wrl::ComPtr<ID3D12Heap> heap;
				memory::TLSF tlsf;
			};

			class DxMemoryAllocator
			{
			public:

				void initialize(DxDevice* device, u64 poolSize, rhi::EMemoryType type);
				void deinitialize();

				Allocation allocate(ID3D12Resource* resource);
				void free(const Allocation& alloc);

			private:
				MemoryPool pool_;

				DxDevice* device_ = nullptr;

				rhi::EMemoryType type_;
			};
		}
	}
}

#endif
