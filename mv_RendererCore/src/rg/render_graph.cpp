
#include "rg/render_graph.h"

namespace mv::rg
{
	Builder::Builder(RenderGraph& graph, RenderPass& pass)
		: graph_(graph)
		, pass_(pass)
	{

	}

	void Builder::access(RGTextureHandle handle, ERGResourceUsase usage)
	{
		RGTextureUsage textureUsage
		{
			.handle = handle,
			.usage = usage,
		};

		pass_.textureUsages.push_back(textureUsage);
	}

	void Builder::access(RGBufferHandle handle, ERGResourceUsase usage)
	{
		RGBufferUsage bufferUsage
		{
			.handle = handle,
			.usage = usage,
		};

		pass_.bufferUsages.push_back(bufferUsage);
	}

	Context::Context(RenderGraph& graph, rhi::IRHI* rhi)
		: graph_(graph)
		, rhi_(rhi)
	{

	}

	rhi::TextureHandle Context::getTexture(RGTextureHandle handle)
	{

	}

	rhi::BufferHandle Context::getBuffer(RGBufferHandle handle)
	{

	}


	RGTextureHandle RenderGraph::createTexture(const RGTextureDesc& desc)
	{
		RGTextureHandle handle = textures_.size();

		RGTexture texture
		{
			.handle = handle,
			.desc = desc,
			.physical = -1,
		};

		textures_.push_back(texture);

		return handle;
	}

	RGBufferHandle RenderGraph::createBuffer(const RGBufferDesc& desc)
	{
		RGBufferHandle handle = buffers_.size();

		RGBuffer buffer
		{
			.handle = handle,
			.desc = desc,
			.physical = -1,
		};

		buffers_.push_back(buffer);

		return handle;
	}

	void RenderGraph::addPass(const RenderPass& pass)
	{
		passes_.push_back(pass);
	}

	void RenderGraph::compile()
	{
		compiledPasses_.clear();
	}

	void RenderGraph::execute()
	{
		for (auto& pass : compiledPasses_)
		{

		}
	}
}
