
#ifndef _MV_DX_DESCRIPTOR_H_
#define _MV_DX_DESCRIPTOR_H_

#include <vector>

#include <d3d12.h>

#include <wrl/client.h>

#include <util/types.h>

namespace mv
{
	namespace backend
	{
		namespace dx12_0
		{
			using namespace types;
			namespace wrl = Microsoft::WRL;

			class DxDevice;

			class DxDescriptorAllocator
			{
			private:
				struct DescriptorHeapWrapper
				{
					wrl::ComPtr<ID3D12DescriptorHeap> heap;
					u32 usedDescs = 0;
					u32 maxDescs = 0;
				};

				struct DescriptorFrameContext
				{
					std::vector<DescriptorHeapWrapper> heaps;

					u32 currentHeap = 0;
				};

			public:
				void initialize(DxDevice* device, u32 framesInFlight, u32 maxDescriptors, D3D12_DESCRIPTOR_HEAP_TYPE type, bool global = false);
				void deinitialize();

				void beginFrame(u32 frameIndex);

				u32 allocate();

				// A descriptor table must point at a contiguous run, so a bind group's
				// descriptors have to be reserved in one go rather than one at a time.
				u32 allocateRange(u32 count);

				D3D12_CPU_DESCRIPTOR_HANDLE getCpuHandle(u32 index) const;
				D3D12_GPU_DESCRIPTOR_HANDLE getGpuHandle(u32 index) const;

				ID3D12DescriptorHeap* heap() const;

			private:
				DescriptorHeapWrapper createRtvHeap(u32 maxDescs);
				DescriptorHeapWrapper createDsvHeap(u32 maxDescs);
				DescriptorHeapWrapper createSrvHeap(u32 maxDescs);
				DescriptorHeapWrapper createGlobalHeap(u32 maxDescs);

			private:
				std::vector<DescriptorFrameContext> frames_;

				D3D12_DESCRIPTOR_HEAP_TYPE heapType_;

				u32 currentFrame_ = 0;
				u32 maxDescs_ = 0;
				bool global_ = false;

				DxDevice* device_;
			};
		}
	}
}

#endif