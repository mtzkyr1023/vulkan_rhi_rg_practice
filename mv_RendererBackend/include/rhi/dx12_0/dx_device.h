#ifndef _MV_DX_DEVICE_H_
#define _MV_DX_DEVICE_H_

#include <d3d12.h>
#include <dxgi1_6.h>


#include <wrl/client.h>

namespace mv
{
	namespace backend
	{
		namespace dx12_0
		{
			namespace wrl = Microsoft::WRL;

			class DxDevice
			{
			public:
				void initialize();
				void deinitialize();

				void waitIdle();

				IDXGIFactory6* factory() const { return factory_.Get(); }
				IDXGIAdapter1* adapter() const { return adapter_.Get(); }
				ID3D12Device* device() const { return device_.Get(); }

				ID3D12CommandQueue* graphicsQueue() const {	return graphicsQueue_.Get(); }
				ID3D12CommandQueue* copyQueue() const { return copyQueue_.Get(); }
				ID3D12CommandQueue* computeQueue() const { return computeQueue_.Get(); }

				// Tier 1 forces buffers, RT/DS textures and other textures into separate
				// heaps; tier 2 lets one heap hold all of them.
				D3D12_RESOURCE_HEAP_TIER resourceHeapTier() const { return resourceHeapTier_; }

				bool supportsBindless() const { return supportsBindless_; }

			private:
				wrl::ComPtr<IDXGIFactory6> factory_;
				wrl::ComPtr<IDXGIAdapter1> adapter_;
				wrl::ComPtr<ID3D12Device> device_;

				wrl::ComPtr<ID3D12CommandQueue> graphicsQueue_;
				wrl::ComPtr<ID3D12CommandQueue> copyQueue_;
				wrl::ComPtr<ID3D12CommandQueue> computeQueue_;

				wrl::ComPtr<ID3D12Debug> debugController_;

				wrl::ComPtr<ID3D12Fence> idleFence_;
				UINT64 idleFenceValue_ = 0;

				D3D12_RESOURCE_HEAP_TIER resourceHeapTier_ = D3D12_RESOURCE_HEAP_TIER_1;
				D3D12_RESOURCE_BINDING_TIER resourceBindingTier_ = D3D12_RESOURCE_BINDING_TIER_1;

				bool supportsBindless_ = false;
			};
		}
	}
}

#endif