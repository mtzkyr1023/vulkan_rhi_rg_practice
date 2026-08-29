
// NOTE: the guard is _MV_MATERIAL_PIPELINE_H_, not _MV_PIPELINE_H_, which rhi/pipeline.h
// already uses. Sharing it would make whichever header came second expand to nothing.
#ifndef _MV_MATERIAL_PIPELINE_H_
#define _MV_MATERIAL_PIPELINE_H_

#include <memory>
#include <vector>

#include "rhi/rhi.h"

#include "util/types.h"

namespace mv
{
	namespace material
	{
		using namespace types;

		enum class EAlphaMode : u8
		{
			eOpaque = 0,
			eMask,
			eBlend,
		};

		// Everything about a material that has to be baked into a pipeline rather than
		// into its constant buffer. Two materials that agree here can share a pipeline.
		struct MaterialRenderState
		{
			EAlphaMode alphaMode = EAlphaMode::eOpaque;
			bool doubleSided = false;

			bool operator==(const MaterialRenderState& other) const
			{
				return alphaMode == other.alphaMode && doubleSided == other.doubleSided;
			}
		};

		// Creates one pipeline per distinct render state, on demand. Sponza-class scenes
		// have hundreds of materials but only a handful of distinct states between them.
		class MaterialPipelineCache
		{
		public:
			struct Desc
			{
				rhi::ShaderHandle vs = INVALID_HANDLE;
				rhi::ShaderHandle ps = INVALID_HANDLE;

				rhi::PipelineLayoutHandle layout = INVALID_HANDLE;

				rhi::VertexLayout vertexLayout;

				// Every colour target the pass writes, in order. The forward path writes shaded
				// colour and screen-space velocity, so a single format is no longer enough.
				std::vector<rhi::ETextureFormat> colorFormats;
				rhi::ETextureFormat depthFormat = rhi::ETextureFormat::eUndefined;
			};

			void initialize(const std::shared_ptr<rhi::IRHI>& rhi, const Desc& desc);
			void deinitialize();

			rhi::PipelineHandle get(const MaterialRenderState& state);

		private:
			struct Entry
			{
				MaterialRenderState state;
				rhi::PipelineHandle pipeline = INVALID_HANDLE;
			};

			std::shared_ptr<rhi::IRHI> rhi_;

			Desc desc_;

			std::vector<Entry> entries_;
		};
	}
}

#endif
