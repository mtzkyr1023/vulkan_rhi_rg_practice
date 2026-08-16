
#ifndef _MV_DX_SWAPCHAIN_H_
#define _MV_DX_SWAPCHAIN_H_

#include <d3d12.h>
#include <dxgi1_6.h>

#include <wrl/client.h>

#include "util/types.h"

namespace mv
{
	namespace backend
	{
		namespace dx12_0
		{
			using namespace types;
			namespace wrl = Microsoft::WRL;

			class DxDevice;

			class DxSwapchain
			{
			public:
				void initialize(DxDevice* device, void* hwnd);
				void deinitialize();

				void present(ID3D12CommandQueue* queue);

				void acquireNextImage() { imageIndex_ = (imageIndex_ + 1) % imageCount_; }

				IDXGISwapChain1* swapchain() const { return swapchain_.Get(); }

				types::u32 imageCount() const { return imageCount_; }
				types::u32 imageIndex() const { return imageIndex_; }

			private:
				wrl::ComPtr<IDXGISwapChain1> swapchain_;


				u32 imageCount_ = 0;

				DxDevice* device_ = nullptr;

				u32 imageIndex_ = 0;
			};
		}
	}
}

#endif