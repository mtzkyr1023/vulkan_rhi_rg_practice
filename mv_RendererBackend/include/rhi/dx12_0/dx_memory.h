
#ifndef _MV_DX_ALLOCATOR_H_
#define _MV_DX_ALLOCATOR_H_

#include <memory>
#include <vector>

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

				// Which pool the block came from; free() must return it to that one.
				u32 poolIndex = 0;
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

				Allocation allocate(const D3D12_RESOURCE_DESC& desc);
				void free(const Allocation& alloc);

			private:
				// Pools are added on demand. A single fixed pool cannot hold a real model:
				// one 2048x2048 RGBA8 texture alone is 16 MB.
				MemoryPool& addPool(u64 minimumSize);

			private:
				// unique_ptr, not by value: growing the vector would relocate the TLSF, whose
				// free lists point into its own block storage.
				std::vector<std::unique_ptr<MemoryPool>> pools_;

				DxDevice* device_ = nullptr;

				u64 poolSize_ = 0;

				rhi::EMemoryType type_;
			};
		}
	}
}

#endif
