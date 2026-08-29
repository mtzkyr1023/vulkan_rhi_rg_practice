#ifndef _MV_BUFFER_FILL_H_
#define _MV_BUFFER_FILL_H_

#include <memory>

#include "rhi/rhi.h"

#include "util/types.h"

namespace mv
{
	namespace compute
	{
		using namespace types;

		// Writes a constant 32-bit value across a device-local buffer, on the GPU.
		//
		// The alternative for a buffer that has to be reset every frame is to upload a
		// cleared copy, which means a host-visible staging buffer, a transfer, and a
		// transition either side of it -- per frame, for something a dispatch does in
		// microseconds without leaving device memory.
		class BufferFill
		{
		public:
			bool initialize(const std::shared_ptr<rhi::IRHI>& rhi, const u32* shaderBytecode, u32 shaderSize);
			void deinitialize();

			bool isReady() const { return pipeline_ != INVALID_HANDLE; }

			// Names the buffer this instance fills. Done once rather than per frame,
			// because rewriting a descriptor a frame in flight may still be reading is not
			// something to do casually.
			void setTarget(rhi::BufferHandle buffer, u32 elementCount);

			// Records the fill. Leaves the buffer in eShaderWrite with a barrier after it,
			// so whatever reads or atomically updates it next is ordered against this.
			void record(rhi::CommandBufferHandle cmd, u32 value);

		private:
			std::shared_ptr<rhi::IRHI> rhi_;

			rhi::BindGroupLayoutHandle layout_ = INVALID_HANDLE;
			rhi::PipelineLayoutHandle pipelineLayout_ = INVALID_HANDLE;
			rhi::PipelineHandle pipeline_ = INVALID_HANDLE;

			rhi::BindGroupHandle group_ = INVALID_HANDLE;

			rhi::BufferHandle target_ = INVALID_HANDLE;
			u32 elementCount_ = 0;
		};
	}
}

#endif
