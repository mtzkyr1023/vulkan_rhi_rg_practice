
#include "rg/render_graph.h"

namespace mv::rg
{
	RGTextureHandle RenderGraph::createTexture(const RGTextureDesc& desc)
	{

	}

	RGBufferHandle RenderGraph::createBuffer(const RGBufferDesc& desc)
	{

	}

	void RenderGraph::addPass(const RenderPass& pass)
	{

	}

	void RenderGraph::compile()
	{
		compiledPasses_.clear();
	}

	void RenderGraph::execute()
	{
		for (auto& pass : compiledPasses_)
		{
			

			pass.pass->execute();
		}
	}
}
