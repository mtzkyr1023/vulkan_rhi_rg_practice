#ifndef _MV_MIP_GENERATOR_H_
#define _MV_MIP_GENERATOR_H_

#include <memory>
#include <vector>

#include "rhi/rhi.h"

#include "util/types.h"

namespace mv
{
	namespace compute
	{
		using namespace types;

		// Fills a texture's mip chain on the GPU from its top level.
		//
		// The chain used to be built on the CPU, level by level, because neither backend
		// offered a portable downsample: Vulkan has vkCmdBlitImage and D3D12 has nothing.
		// A compute shader is what both have, and it moves the work off the load thread and
		// out of the upload -- only level 0 is staged now, instead of the whole chain.
		//
		// A texture handed to this has to be created with eStorage as well as eSampled, so
		// the levels below the top can be written.
		class MipGenerator
		{
		public:
			bool initialize(const std::shared_ptr<rhi::IRHI>& rhi, const u32* shaderBytecode, u32 shaderSize);
			void deinitialize();

			bool isReady() const { return pipeline_ != INVALID_HANDLE; }

			// Records the whole chain into an existing command buffer, level 1 downwards.
			// The caller owns the barriers around the texture as a whole; this inserts the
			// ones between levels.
			void record(rhi::CommandBufferHandle cmd, rhi::TextureHandle texture, u32 width, u32 height, u32 mipLevels, bool srgb);

			// The same, on its own immediate submit. What an asset loader wants.
			void generate(rhi::TextureHandle texture, u32 width, u32 height, u32 mipLevels, bool srgb);

		private:
			// One bind group per level: a descriptor names the source and target levels, so
			// they cannot all share one that is rewritten between dispatches -- the
			// dispatches are still in flight.
			rhi::BindGroupHandle groupForLevel(u32 level);

		private:
			std::shared_ptr<rhi::IRHI> rhi_;

			rhi::BindGroupLayoutHandle layout_ = INVALID_HANDLE;
			rhi::PipelineLayoutHandle pipelineLayout_ = INVALID_HANDLE;
			rhi::PipelineHandle pipeline_ = INVALID_HANDLE;

			// Grown on demand and reused across textures, because the number of levels any
			// one texture needs is small and the same groups serve the next one.
			std::vector<rhi::BindGroupHandle> levelGroups_;
		};
	}
}

#endif
