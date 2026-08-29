#ifndef _MV_DEBUG_LINE_RENDERER_H_
#define _MV_DEBUG_LINE_RENDERER_H_

#include <memory>
#include <vector>

#include "rhi/rhi.h"

#include "util/math.h"
#include "util/types.h"

namespace mv
{
	namespace debugdraw
	{
		using namespace types;

		// World-space line segments drawn over the scene: the physics wireframes, and
		// whatever else ever needs debug ink.
		//
		// The vertices are rewritten from the CPU every frame -- debug geometry has no
		// stable identity worth caching -- so the buffer is host-visible and doubled per
		// frame in flight, the same pattern every per-frame constant here uses: the GPU
		// may still be reading last frame's lines while this frame writes its own.
		class DebugLineRenderer
		{
		public:
			// Position and colour, nothing else: a line has no material.
			struct Vertex
			{
				f32 position[3];
				f32 color[3];
			};

			struct Shaders
			{
				const u32* vs = nullptr; u32 vsSize = 0;
				const u32* ps = nullptr; u32 psSize = 0;
			};

			static constexpr u32 kMaxVertices = 65536;

			bool initialize(
				const std::shared_ptr<rhi::IRHI>& rhi,
				const Shaders& shaders,
				rhi::ETextureFormat colorFormat,
				rhi::ETextureFormat depthFormat,
				rhi::BindGroupLayoutHandle sceneLayout,
				rhi::BindGroupLayoutHandle bindlessLayout,
				u32 framesInFlight);

			void deinitialize();

			bool isReady() const { return ready_; }

			// This frame's lines into this frame's buffer slot; anything past the cap is
			// dropped. Call before the graph executes, draw with record() inside a pass.
			void upload(u32 frameIndex, const Vertex* vertices, u32 vertexCount);

			void record(
				rhi::CommandBufferHandle cmd,
				u32 frameIndex,
				rhi::BindGroupHandle sceneGroup,
				rhi::BindGroupHandle bindlessGroup);

			u32 vertexCount(u32 frameIndex) const
			{
				return frameIndex < counts_.size() ? counts_[frameIndex] : 0;
			}

		private:
			std::shared_ptr<rhi::IRHI> rhi_;

			rhi::PipelineLayoutHandle pipelineLayout_ = INVALID_HANDLE;
			rhi::PipelineHandle pipeline_ = INVALID_HANDLE;

			std::vector<rhi::BufferHandle> buffers_;
			std::vector<u32> counts_;

			bool ready_ = false;
		};
	}
}

#endif
