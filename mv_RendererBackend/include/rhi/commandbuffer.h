#ifndef _MV_COMMANDBUFFER_H_
#define _MV_COMMANDBUFFER_H_

#include "vector"
#include "memory"

#include "rhi/resource.h"
#include "util/types.h"

namespace mv
{
	namespace rhi
	{
		using namespace types;

		using CommandBufferHandle = u32;

		enum class EQueueType
		{
			eGraphics = 0,
			eCompute,
			eTransfer,

			eNum,
		};

		class ICommandPool
		{
		public:
			virtual ~ICommandPool() {}

			CommandBufferHandle allocate();
			void free(CommandBufferHandle handle);

		protected:
			virtual CommandBufferHandle createCommandBuffer() = 0;

		protected:
			std::vector<CommandBufferHandle> freeList_;

			CommandBufferHandle nextHandleIndex_ = 0;
		};
	}
}

#endif
