#ifndef _DX12_COMMAND_H_
#define _DX12_COMMAND_H_

#include <d3d12.h>

#include <wrl/client.h>

#include "rhi/rhi.h"
#include "rhi/commandbuffer.h"

#include "util/types.h"


namespace mv
{
	namespace backend
	{
		namespace dx12_0
		{
			namespace wrl = Microsoft::WRL;

			using namespace types;

			class DxDevice;
			class DxCommandPool;

			class DxCommandList
			{
			public:
				void initialize(DxDevice* device, DxCommandPool* commandPool);
				void deinitialize();

				ID3D12GraphicsCommandList* commandList() { return commandList_.Get(); }

			private:
				wrl::ComPtr<ID3D12GraphicsCommandList> commandList_;
			};

			class DxCommandPool : public rhi::ICommandPool
			{
			public:
				void initialize(DxDevice* device, D3D12_COMMAND_LIST_TYPE type);
				void deinitialize();

				DxCommandList& getCommandList(rhi::CommandBufferHandle handle) const;

				D3D12_COMMAND_LIST_TYPE type() const { return type_; }

				ID3D12CommandAllocator* allocator() const { return allocator_.Get(); }

			private:
				rhi::CommandBufferHandle createCommandBuffer() override;

			private:
				wrl::ComPtr<ID3D12CommandAllocator> allocator_;

				std::vector<std::shared_ptr<DxCommandList>> releaseArray_;

				DxDevice* device_;

				D3D12_COMMAND_LIST_TYPE type_;
			};
		}
	}
}

#endif