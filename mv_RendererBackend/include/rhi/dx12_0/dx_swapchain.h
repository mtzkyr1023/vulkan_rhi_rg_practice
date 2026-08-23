
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

				// DXGI decides which buffer is next after each Present (and FLIP_DISCARD does not
				// guarantee a plain round robin), so the index must be queried, never counted.
				void acquireNextImage() { imageIndex_ = swapchain_->GetCurrentBackBufferIndex(); }

				IDXGISwapChain4* swapchain() const { return swapchain_.Get(); }

				types::u32 imageCount() const { return imageCount_; }
				types::u32 imageIndex() const { return imageIndex_; }

				DXGI_FORMAT format() const { return format_; }
				types::u32 width() const { return width_; }
				types::u32 height() const { return height_; }

			private:
				wrl::ComPtr<IDXGISwapChain4> swapchain_;


				u32 imageCount_ = 0;

				DXGI_FORMAT format_ = DXGI_FORMAT_UNKNOWN;
				u32 width_ = 0;
				u32 height_ = 0;

				DxDevice* device_ = nullptr;

				u32 imageIndex_ = 0;
			};
		}
	}
}

#endif