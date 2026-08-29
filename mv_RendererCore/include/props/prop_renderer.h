#ifndef _MV_PROP_RENDERER_H_
#define _MV_PROP_RENDERER_H_

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

		// Draws a loaded model wherever an entity says it stands: the one pass in the
		// renderer with a model matrix.
		//
		// Everything else here bakes its transforms into the vertices, because the
		// visibility buffer cannot recover a per-draw matrix from a triangle id. Entities
		// move at gameplay's whim, so they take the forward path instead -- after the
		// resolve, into the same colour, velocity and depth targets, where every later
		// pass treats them as scenery.
		//
		// What it does not do is cast shadows: the cascade pass draws the scene's own
		// geometry only. A prop shadows itself and receives the world's, which for a
		// handful of placed objects reads right long before anyone misses the rest.
		class PropRenderer
		{
		public:
			struct Shaders
			{
				const u32* vs = nullptr; u32 vsSize = 0;
				const u32* ps = nullptr; u32 psSize = 0;
			};

			// The scene and bindless layouts come along because the shading is the
			// material system's own: same material buffer, same bindless textures.
			bool initialize(
				const std::shared_ptr<rhi::IRHI>& rhi,
				const Shaders& shaders,
				const std::vector<rhi::ETextureFormat>& colorFormats,
				rhi::ETextureFormat depthFormat,
				rhi::BindGroupLayoutHandle sceneLayout,
				rhi::BindGroupLayoutHandle bindlessLayout);

			void deinitialize();

			bool isReady() const { return ready_; }

			// One model at one transform: binds the model's buffers and draws each of its
			// primitives with its own material. Call once per entity inside a render pass
			// with the scene targets bound.
			void record(
				rhi::CommandBufferHandle cmd,
				const asset::Model& model,
				const math::Mat4& transform,
				rhi::BindGroupHandle sceneGroup,
				rhi::BindGroupHandle bindlessGroup);

		private:
			std::shared_ptr<rhi::IRHI> rhi_;

			rhi::PipelineLayoutHandle pipelineLayout_ = INVALID_HANDLE;
			rhi::PipelineHandle pipeline_ = INVALID_HANDLE;

			bool ready_ = false;
		};
	}
}

#endif
