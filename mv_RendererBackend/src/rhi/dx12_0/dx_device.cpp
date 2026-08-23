
#include "rhi/dx12_0/dx_device.h"

#include <cstdio>

namespace mv::backend::dx12_0
{
	void DxDevice::initialize()
	{
		HRESULT res;

		res = D3D12GetDebugInterface(IID_PPV_ARGS(&debugController_));

		debugController_->EnableDebugLayer();

		res = CreateDXGIFactory2(0, IID_PPV_ARGS(&factory_));

		for (UINT i = 0; factory_->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&adapter_)) != DXGI_ERROR_NOT_FOUND; i++)
		{
			DXGI_ADAPTER_DESC1 desc{};
			adapter_->GetDesc1(&desc);

			if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
				continue;

			break;
		}

		res = D3D12CreateDevice(adapter_.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&device_));

		D3D12_FEATURE_DATA_D3D12_OPTIONS options{};
		if (SUCCEEDED(device_->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &options, sizeof(options))))
		{
			resourceHeapTier_ = options.ResourceHeapTier;
			resourceBindingTier_ = options.ResourceBindingTier;
		}

		// Unbounded descriptor ranges need binding tier 2; tier 3 also lifts the limit on
		// how many descriptors a heap can hold.
		supportsBindless_ = (resourceBindingTier_ >= D3D12_RESOURCE_BINDING_TIER_2);

		DXGI_ADAPTER_DESC1 adapterDesc{};
		adapter_->GetDesc1(&adapterDesc);

		char message[512];
		sprintf_s(message,
			"D3D12 device: %ls\n  resource binding tier: %d, heap tier: %d\n  bindless (unbounded descriptor range): %s\n",
			adapterDesc.Description,
			(int)resourceBindingTier_,
			(int)resourceHeapTier_,
			supportsBindless_ ? "yes" : "NO");
		OutputDebugStringA(message);

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

		device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&idleFence_));
	}

	void DxDevice::deinitialize()
	{
		idleFence_.Reset();

		graphicsQueue_.Reset();
		computeQueue_.Reset();
		copyQueue_.Reset();

		device_.Reset();
		adapter_.Reset();
		factory_.Reset();
		debugController_.Reset();
	}

	void DxDevice::waitIdle()
	{
		auto waitForQueue = [&](ID3D12CommandQueue* queue)
		{
			idleFenceValue_++;
			queue->Signal(idleFence_.Get(), idleFenceValue_);

			if (idleFence_->GetCompletedValue() < idleFenceValue_)
			{
				HANDLE fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
				idleFence_->SetEventOnCompletion(idleFenceValue_, fenceEvent);
				WaitForSingleObject(fenceEvent, INFINITE);
				CloseHandle(fenceEvent);
			}
		};

		waitForQueue(graphicsQueue_.Get());
		waitForQueue(computeQueue_.Get());
		waitForQueue(copyQueue_.Get());
	}
}