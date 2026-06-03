#include "rhi/commandbuffer.h"

namespace mv::rhi
{
	CommandBufferHandle ICommandPool::allocate()
	{
		if (!freeList_.empty())
		{
			CommandBufferHandle handle = freeList_.back();
			freeList_.pop_back();
			return handle;
		}
		
		CommandBufferHandle handle = createCommandBuffer();
		nextHandleIndex_++;
		return handle;
	}

	void ICommandPool::free(CommandBufferHandle handle)
	{
		freeList_.push_back(handle);
	}
}