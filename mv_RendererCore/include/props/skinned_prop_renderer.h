#ifndef _MV_SKINNED_PROP_RENDERER_H_
#define _MV_SKINNED_PROP_RENDERER_H_

#include <memory>
#include <vector>

#include "asset/gltf_loader.h"
#include "rhi/rhi.h"

#include "util/math.h"
#include "util/types.h"

namespace mv
{
	namespace props
	{
		using namespace types;

		// The prop pass's deforming sibling: same forward targets, same material
		// shading, one extra multiply per vertex out of a joint palette.
		//
		// The palette lives in a per-frame storage buffer (a set of matrices will
		// not fit push constants), rewritten from the animator every frame and bound
		// as this pass's own set. One skinned model on screen is the current shape;
		// the day there are many, the palette buffer grows offsets, not concepts.
		class SkinnedPropRenderer
		{
		public:
			struct Shaders
			{
				const u32* vs = nullptr; u32 vsSize = 0;
				const u32* ps = nullptr; u32 psSize = 0;
			};

			static constexpr u32 kMaxJoints = 256;

			bool initialize(
				const std::shared_ptr<rhi::IRHI>& rhi,
				const Shaders& shaders,
				const std::vector<rhi::ETextureFormat>& colorFormats,
				rhi::ETextureFormat depthFormat,
				rhi::BindGroupLayoutHandle sceneLayout,
				rhi::BindGroupLayoutHandle bindlessLayout,
				u32 framesInFlight);

			void deinitialize();

			bool isReady() const { return ready_; }

			// This frame's joint matrices; anything past kMaxJoints is dropped.
			void setPalette(u32 frameIndex, const math::Mat4* matrices, u32 count);

			void record(
				rhi::CommandBufferHandle cmd,
				const asset::SkinnedModel& model,
				const math::Mat4& transform,
				u32 frameIndex,
				rhi::BindGroupHandle sceneGroup,
				rhi::BindGroupHandle bindlessGroup);

		private:
			std::shared_ptr<rhi::IRHI> rhi_;

			rhi::PipelineLayoutHandle pipelineLayout_ = INVALID_HANDLE;
			rhi::PipelineHandle pipeline_ = INVALID_HANDLE;

			rhi::BindGroupLayoutHandle paletteLayout_ = INVALID_HANDLE;

			std::vector<rhi::BufferHandle> paletteBuffers_;
			std::vector<rhi::BindGroupHandle> paletteGroups_;

			bool ready_ = false;
		};
	}
}

#endif
