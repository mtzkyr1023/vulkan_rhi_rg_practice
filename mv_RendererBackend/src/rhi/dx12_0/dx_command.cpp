
#include "rhi/dx12_0/dx_command.h"
#include "rhi/dx12_0/dx_device.h"


namespace mv::backend::dx12_0
{
	void DxCommandList::initialize(DxDevice* device, DxCommandPool* pool)
	{
		if (commandList_)
			return;

		device->device()->CreateCommandList(0, pool->type(), pool->allocator(), nullptr, IID_PPV_ARGS(&commandList_));

		// Command lists are created in the recording state; close it immediately so the
		// first real use (which Resets it against the owning frame's own allocator) is valid.
		commandList_->Close();
	}

	void DxCommandList::deinitialize()
	{

	}



	void DxCommandPool::initialize(DxDevice* device, D3D12_COMMAND_LIST_TYPE type)
	{
		device->device()->CreateCommandAllocator(type, IID_PPV_ARGS(&allocator_));

		device_ = device;
	}

	void DxCommandPool::deinitialize()
	{

	}



	rhi::CommandBufferHandle DxCommandPool::createCommandBuffer()
	{
		std::shared_ptr<DxCommandList> cmd = std::make_shared<DxCommandList>();
		cmd->initialize(device_, this);

		rhi::CommandBufferHandle handle = releaseArray_.size();
		releaseArray_.push_back(cmd);

		return handle;
	}

	DxCommandList& DxCommandPool::getCommandList(rhi::CommandBufferHandle handle) const
	{
		return *releaseArray_[handle];
	}
}
