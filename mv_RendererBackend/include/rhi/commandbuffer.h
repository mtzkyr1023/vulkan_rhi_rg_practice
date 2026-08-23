#ifndef _MV_COMMANDBUFFER_H_
#define _MV_COMMANDBUFFER_H_

#include <vector>
#include <memory>

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

		struct RenderPassColorTarget
		{
			TextureHandle texture = INVALID_HANDLE;

			bool clear = true;
			f32 clearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
		};

		struct RenderPassDepthTarget
		{
			TextureHandle texture = INVALID_HANDLE;

			bool clear = true;
			f32 clearDepth = 1.0f;
		};

		struct RenderPassDesc
		{
			std::vector<RenderPassColorTarget> colorTargets;

			// Optional: leave the texture INVALID_HANDLE for a colour-only pass.
			RenderPassDepthTarget depthTarget;
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
