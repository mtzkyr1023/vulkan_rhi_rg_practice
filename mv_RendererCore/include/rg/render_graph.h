#ifndef _MV_RENDER_GRAPH_H_
#define _MV_RENDER_GRAPH_H_

#include <string>
#include <vector>
#include <functional>

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

		using RGPassHandle = u32;

		class RenderGraph;
		struct RenderPass;
		class Builder;
		class Context;

		enum class ERGTextureUsage
		{
			Undefined = 0,
			ShaderRead,
			ColorAttachment,
			DepthAttachment,
			CopySrc,
			CopyDst,
			StorageRead,
			StorageWrite,
			Present,
		};

		enum class ERGBufferUsage
		{
			Undefined = 0,
			Uniform,
			CopySrc,
			CopyDst,
			StorageRead,
			StorageWrite,
			Vertex,
			Index,
		};

		enum class ERGMemoryType
		{
			DeviceLocalImage = 0,
			DeviceLocalBuffer,
			HostVisibleImage,
			HostVisibleBuffer,
		};

		enum class ERGFormat
		{
			R8G8B8A8_UNORM = 0,
			R8G8B8A8_SRGB,
		};


		rhi::EMemoryType convertMemoryType(ERGMemoryType type);
		rhi::ETextureUsage convertTextureUsage(ERGTextureUsage usage);
		rhi::EBufferUsage convertBufferUsage(ERGBufferUsage usage);
		rhi::ETextureFormat convertTextureFormat(ERGFormat format);
		rhi::EResourceState convertTextureState(ERGTextureUsage usage);
		rhi::EResourceState convertBufferState(ERGBufferUsage usage);

		struct RGTextureDesc
		{
			std::string name;
			u32 width = 1;
			u32 height = 1;
			u32 depth = 1;
			ERGTextureUsage initialState = ERGTextureUsage::Undefined;
			ERGMemoryType memoryType = ERGMemoryType::DeviceLocalImage;
			ERGFormat format = ERGFormat::R8G8B8A8_UNORM;
		};

		struct RGBufferDesc
		{
			std::string name;
			size_t size = 0;
			ERGBufferUsage initialState = ERGBufferUsage::Undefined;
			ERGMemoryType memoryType = ERGMemoryType::DeviceLocalBuffer;
		};

		struct RGTexture
		{
			std::string name;
			RGTextureHandle handle;
			RGTextureDesc desc;

			rhi::TextureHandle physical;

			ERGTextureUsage initialState = ERGTextureUsage::Undefined;
			ERGTextureUsage lastState = ERGTextureUsage::Undefined;

			RGPassHandle firstUseHandle = INVALID_HANDLE;
			RGPassHandle lastUseHandle = INVALID_HANDLE;
		};

		struct RGBuffer
		{
			std::string name;
			RGBufferHandle handle;
			RGBufferDesc desc;

			rhi::BufferHandle physical;

			ERGBufferUsage initialState = ERGBufferUsage::Undefined;
			ERGBufferUsage lastState = ERGBufferUsage::Undefined;

			RGPassHandle firstUseHandle = INVALID_HANDLE;
			RGPassHandle lastUseHandle = INVALID_HANDLE;
		};

		struct RGTextureUsage
		{
			RGTextureHandle handle;

			ERGTextureUsage usage;
		};


		struct RGBufferUsage
		{
			RGBufferHandle handle;

			ERGBufferUsage usage;
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
			std::vector<rhi::TextureBarrier> textureBarriers;
			std::vector<rhi::BufferBarrier> bufferBarriers;

			RGPassHandle handle;

			RenderPass* pass = nullptr;
		};


		class Builder
		{
		public:
			Builder(RenderGraph& graph_, RenderPass& pass);

			void accessTexture(RGTextureHandle handle, ERGTextureUsage usage);
			void accessBuffer(RGBufferHandle handle, ERGBufferUsage usage);

		private:
			RenderGraph& graph_;

			RenderPass& pass_;
		};


		class Context
		{
		public:
			Context(RenderGraph& graph, const std::shared_ptr<rhi::IRHI>& rhi, rhi::CommandBufferHandle cmd);

			rhi::TextureHandle getTexture(RGTextureHandle handle);
			rhi::BufferHandle getBuffer(RGBufferHandle handle);

			rhi::IRHI* rhi() const { return rhi_.lock().get(); }

			rhi::CommandBufferHandle cmd() const { return cmd_; }

		private:
			RenderGraph& graph_;

			std::weak_ptr<rhi::IRHI> rhi_;

			rhi::CommandBufferHandle cmd_ = INVALID_HANDLE;
		};


		class RenderGraph
		{
		public:
			RenderGraph(const std::shared_ptr<rhi::IRHI>& rhi);

			RGTextureHandle createTexture(const RGTextureDesc& desc);
			RGBufferHandle createBuffer(const RGBufferDesc& desc);

			RGTextureHandle importTexture(std::string name, rhi::TextureHandle rhiHandle, ERGTextureUsage initialState);

			void addPass(const RenderPass& pass);

			void compile();

			void execute();

			const std::vector<RGTexture>& textures() const { return textures_; }
			const std::vector<RGBuffer>& buffers() const { return buffers_; }

		private:
			void allocate();

			void emitBarriers(Context& context, const std::vector<rhi::TextureBarrier>& barriers);
			void emitBarriers(Context& context, const std::vector<rhi::BufferBarrier>& barriers);

		private:
			std::shared_ptr<rhi::IRHI> rhi_;

			std::vector<RGTexture> textures_;
			std::vector<RGBuffer> buffers_;

			std::vector<RenderPass> passes_;

			std::vector<CompiledPass> compiledPasses_;
		};
	}
}

#endif
