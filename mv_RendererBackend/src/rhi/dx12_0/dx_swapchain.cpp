
#include "rhi/dx12_0/dx_swapchain.h"
#include "rhi/dx12_0/dx_device.h"

namespace mv::backend::dx12_0
{
	void DxSwapchain::initialize(DxDevice* device, void* hwnd)
	{
		RECT rect;
		GetClientRect((HWND)hwnd, &rect);

		DXGI_SWAP_CHAIN_DESC1 desc{};
		desc.Width = (UINT)(rect.right - rect.left);
		desc.Height = (UINT)(rect.bottom - rect.top);
		desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		desc.BufferCount = 2;
		desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
		desc.SampleDesc.Count = 1;

		wrl::ComPtr<IDXGISwapChain1> swapchain;
		device->factory()->CreateSwapChainForHwnd(device->graphicsQueue(), (HWND)hwnd, &desc, nullptr, nullptr, swapchain.GetAddressOf());
		swapchain.As(&swapchain_);

		imageCount_ = desc.BufferCount;
		format_ = desc.Format;
		width_ = desc.Width;
		height_ = desc.Height;
		imageIndex_ = swapchain_->GetCurrentBackBufferIndex();

		device_ = device;
	}

	void DxSwapchain::deinitialize()
	{
		swapchain_.Reset();
	}

	void DxSwapchain::present(ID3D12CommandQueue* queue)
	{
		swapchain_->Present(0, 0);
	}
}
