#ifndef _MV_RENDER_GRAPH_H_
#define _MV_RENDER_GRAPH_H_

#include "string"
#include "vector"
#include "functional"

#include "util/types.h"

#include "rhi/resource.h"
#include "rhi/commandbuffer.h"

namespace mv
{
	namespace rg
	{
		using namespace types;

		using RGTextureHandle = u32;
		using RGBufferHandle = u32;

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
			RGTextureDesc desc;
		};

		struct RGBuffer
		{
			RGBufferDesc desc;
		};

		struct RenderPass
		{
			std::string name;

			std::vector<RGTextureHandle> reads;
			std::vector<RGTextureHandle> writes;

			std::function<void(rhi::CommandBufferHandle) > execute;
		};

		struct CompiledPass
		{
			std::vector<rhi::TextureBarrier> barriers;

			RenderPass* pass;
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

			std::vector<RenderPass> passes_;

			std::vector<CompiledPass> compiledPasses_;
		};
	}
}

#endif
