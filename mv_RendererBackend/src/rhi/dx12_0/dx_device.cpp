
#include "rhi/dx12_0/dx_device.h"

namespace mv::backend::dx12_0
{
	void DxDevice::initialize()
	{
		HRESULT res;

		res = D3D12GetDebugInterface(IID_PPV_ARGS(&debugController_));

		debugController_->EnableDebugLayer();

		res = CreateDXGIFactory2(0, IID_PPV_ARGS(&factory_));

		for (UINT i = 0; i < factory_->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&adapter_)) != DXGI_ERROR_NOT_FOUND; i++)
		{
			DXGI_ADAPTER_DESC1 desc{};
			adapter_->GetDesc1(&desc);

			if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
				continue;

			break;
		}

		res = D3D12CreateDevice(adapter_.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&device_));

		{
			D3D12_COMMAND_QUEUE_DESC desc{};
			desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
			desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;

			device_->CreateCommandQueue(&desc, IID_PPV_ARGS(&graphicsQueue_));
		}
		{
			D3D12_COMMAND_QUEUE_DESC desc{};
			desc.Type = D3D12_COMMAND_LIST_TYPE_COMPUTE;
			desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;

			device_->CreateCommandQueue(&desc, IID_PPV_ARGS(&computeQueue_));
		}
		{
			D3D12_COMMAND_QUEUE_DESC desc{};
			desc.Type = D3D12_COMMAND_LIST_TYPE_COPY;
			desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;

			device_->CreateCommandQueue(&desc, IID_PPV_ARGS(&copyQueue_));
		}
	}

	void DxDevice::deinitialize()
	{

	}

	void DxDevice::waitIdle()
	{
	}
}