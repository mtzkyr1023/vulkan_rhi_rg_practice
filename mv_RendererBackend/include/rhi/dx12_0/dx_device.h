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

			private:
				wrl::ComPtr<IDXGIFactory6> factory_;
				wrl::ComPtr<IDXGIAdapter1> adapter_;
				wrl::ComPtr<ID3D12Device> device_;

				wrl::ComPtr<ID3D12CommandQueue> graphicsQueue_;
				wrl::ComPtr<ID3D12CommandQueue> copyQueue_;
				wrl::ComPtr<ID3D12CommandQueue> computeQueue_;

				wrl::ComPtr<ID3D12Debug> debugController_;
			};
		}
	}
}

#endif