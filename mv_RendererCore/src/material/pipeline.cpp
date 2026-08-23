
#include "material/pipeline.h"

namespace mv::material
{
	void MaterialPipelineCache::initialize(const std::shared_ptr<rhi::IRHI>& rhi, const Desc& desc)
	{
		rhi_ = rhi;
		desc_ = desc;
	}

	void MaterialPipelineCache::deinitialize()
	{
		entries_.clear();
		rhi_.reset();
	}

	rhi::PipelineHandle MaterialPipelineCache::get(const MaterialRenderState& state)
	{
		for (const auto& entry : entries_)
		{
			if (entry.state == state)
				return entry.pipeline;
		}

		rhi::GraphicsPipelineDesc pipelineDesc{};
		pipelineDesc.vs = desc_.vs;
		pipelineDesc.ps = desc_.ps;
		pipelineDesc.layoutHandle = desc_.layout;
		pipelineDesc.topology = rhi::EPrimitiveTopology::eTriangleList;
		pipelineDesc.vertexLayout = desc_.vertexLayout;
		pipelineDesc.colorFormats.push_back(desc_.colorFormat);
		pipelineDesc.depthFormat = desc_.depthFormat;

		// glTF's winding is counter-clockwise when a front face is viewed from outside.
		pipelineDesc.rasterizer.frontFace = rhi::EFrontFace::eCounterClockwise;
		pipelineDesc.rasterizer.cullMode = state.doubleSided ? rhi::ECullMode::eNone : rhi::ECullMode::eBack;

		pipelineDesc.depth.depthTestEnable = true;
		pipelineDesc.depth.depthCompareOp = rhi::ECompareOp::eLess;

		if (state.alphaMode == EAlphaMode::eBlend)
		{
			// Blended surfaces must not occlude what is drawn after them, so they test
			// depth but do not write it.
			pipelineDesc.depth.depthWriteEnable = false;

			pipelineDesc.blend.blendEnable = true;
			pipelineDesc.blend.srcColorFactor = rhi::EBlendFactor::eSrcAlpha;
			pipelineDesc.blend.dstColorFactor = rhi::EBlendFactor::eOneMinusSrcAlpha;
			pipelineDesc.blend.colorOp = rhi::EBlendOp::eAdd;
			pipelineDesc.blend.srcAlphaFactor = rhi::EBlendFactor::eOne;
			pipelineDesc.blend.dstAlphaFactor = rhi::EBlendFactor::eOneMinusSrcAlpha;
			pipelineDesc.blend.alphaOp = rhi::EBlendOp::eAdd;
		}
		else
		{
			// Masked materials are cut out in the shader against alphaCutoff, so as far as
			// the pipeline is concerned they behave exactly like opaque ones.
			pipelineDesc.depth.depthWriteEnable = true;
		}

		Entry entry{};
		entry.state = state;
		entry.pipeline = rhi_->createGraphicsPipeline(pipelineDesc);

		entries_.push_back(entry);

		return entry.pipeline;
	}
}
