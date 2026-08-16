
#include "rhi/dx12_0/dx_swapchain.h"
#include "rhi/dx12_0/dx_device.h"

namespace mv::backend::dx12_0
{
	void DxSwapchain::initialize(DxDevice* device, void* hwnd)
	{
		DXGI_SWAP_CHAIN_DESC1 desc{};
		desc.Width = (UINT)GetSystemMetrics(SM_CXSIZE);
		desc.Height = (UINT)GetSystemMetrics(SM_CYSIZE);
		desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		desc.BufferCount = 2;
		desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
		desc.SampleDesc.Count = 1;

		device->factory()->CreateSwapChainForHwnd(device->device(), (HWND)hwnd, &desc, nullptr, nullptr, swapchain_.ReleaseAndGetAddressOf());

		device_ = device;
	}

	void DxSwapchain::deinitialize()
	{

	}

	void DxSwapchain::present(ID3D12CommandQueue* queue)
	{
		swapchain_->Present(0, 0);
	}
}
