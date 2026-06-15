#ifndef _MV_RENDER_GRAPH_H_
#define _MV_RENDER_GRAPH_H_

#include "string"
#include "vector"
#include "functional"

#include "util/types.h"

#include "rhi/resource.h"
#include "rhi/commandbuffer.h"
#include "rhi/rhi.h"

namespace mv
{
	namespace rg
	{
		using namespace types;

		using RGTextureHandle = u32;
		using RGBufferHandle = u32;

		class RenderGraph;
		struct RenderPass;
		class Builder;
		class Context;

		enum class ERGResourceUsase
		{
			ShaderRead = 0,
			ColorAttachment,
			DepthAttachment,
			CopySrc,
			CopyDst,
			StorageRead,
			StorageWrite,
		};

		struct RGTextureDesc
		{
			u32 width = 1;
			u32 height = 1;
			u32 depth = 1;
		};

		struct RGBufferDesc
		{

		};

		struct RGTexture
		{
			RGTextureHandle handle;
			RGTextureDesc desc;

			rhi::TextureHandle physical;
		};

		struct RGBuffer
		{
			RGBufferHandle handle;
			RGBufferDesc desc;

			rhi::BufferHandle physical;
		};

		struct RGTextureUsage
		{
			RGTextureHandle handle;

			ERGResourceUsase usage;
		};


		struct RGBufferUsage
		{
			RGBufferHandle handle;

			ERGResourceUsase usage;
		};

		struct RenderPass
		{
			std::string name;

			std::vector<RGTextureUsage> textureUsages;
			std::vector<RGBufferUsage> bufferUsages;

			std::function<void(Builder&)> setup;

			std::function<void(Context&)> execute;
		};

		struct CompiledPass
		{
			std::vector<rhi::TextureBarrier> barriers;

			RenderPass* pass;
		};


		class Builder
		{
		public:
			Builder(RenderGraph& graph_, RenderPass& pass);

			void access(RGTextureHandle handle, ERGResourceUsase usage);
			void access(RGBufferHandle handle, ERGResourceUsase usage);

		private:
			RenderGraph& graph_;

			RenderPass& pass_;
		};


		class Context
		{
		public:
			Context(RenderGraph& graph, rhi::IRHI* rhi);

			rhi::TextureHandle getTexture(RGTextureHandle handle);
			rhi::BufferHandle getBuffer(RGBufferHandle handle);

			rhi::IRHI* rhi() { return rhi_; }

		private:
			RenderGraph& graph_;

			rhi::IRHI* rhi_;
		};


		class RenderGraph
		{
		public:

			RGTextureHandle createTexture(const RGTextureDesc& desc);
			RGBufferHandle createBuffer(const RGBufferDesc& desc);

			void addPass(const RenderPass& pass);

			void compile();

			void execute();

		private:

			std::vector<RGTexture> textures_;
			std::vector<RGBuffer> buffers_;

			std::vector<RenderPass> passes_;

			std::vector<CompiledPass> compiledPasses_;
		};
	}
}

#endif
