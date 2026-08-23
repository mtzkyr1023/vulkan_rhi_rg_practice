
#include "rg/render_graph.h"

namespace mv::rg
{
	rhi::EMemoryType convertMemoryType(ERGMemoryType type)
	{
		switch (type)
		{
		case ERGMemoryType::DeviceLocalImage:
			return rhi::EMemoryType::eDeviceLocalImage;
		case ERGMemoryType::DeviceLocalBuffer:
			return rhi::EMemoryType::eDeviceLocalBuffer;
		case ERGMemoryType::HostVisibleImage:
			return rhi::EMemoryType::eHostVisibleImage;
		case ERGMemoryType::HostVisibleBuffer:
			return rhi::EMemoryType::eHostVisibleBuffer;
		default:
			return rhi::EMemoryType::eDeviceLocalImage;
		}
	}

	rhi::ETextureUsage convertTextureUsage(ERGTextureUsage usage)
	{
		switch (usage)
		{
		case ERGTextureUsage::ShaderRead:
			return rhi::ETextureUsage::eSampled;
		case ERGTextureUsage::ColorAttachment:
			return rhi::ETextureUsage::eColorAttachment;
		case ERGTextureUsage::DepthAttachment:
			return rhi::ETextureUsage::eDepthStencilAttachment;
		case ERGTextureUsage::CopySrc:
			return rhi::ETextureUsage::eTransferSrc;
		case ERGTextureUsage::CopyDst:
			return rhi::ETextureUsage::eTransferDst;
		default:
			return rhi::ETextureUsage::eSampled;
		}
	}


	rhi::EBufferUsage convertBufferUsage(ERGBufferUsage usage)
	{
		switch (usage)
		{
		case ERGBufferUsage::CopySrc:
			return rhi::EBufferUsage::eTransferSrc;
		default:
			return rhi::EBufferUsage::eUniform;
		}
	}

	rhi::ETextureFormat convertTextureFormat(ERGFormat format)
	{
		switch (format)
		{
		case ERGFormat::R8G8B8A8_UNORM:
			return rhi::ETextureFormat::eR8G8B8A8_UNORM;
		case ERGFormat::R8G8B8A8_SRGB:
			return rhi::ETextureFormat::eR8G8B8A8_SRGB;
		default:
			return rhi::ETextureFormat::eR8G8B8A8_UNORM;
		}
	}

	rhi::EResourceState convertTextureState(ERGTextureUsage usage)
	{
		switch (usage)
		{
		case ERGTextureUsage::ShaderRead:
			return rhi::EResourceState::eShaderRead;
		case ERGTextureUsage::ColorAttachment:
			return rhi::EResourceState::eRenderTarget;
		case ERGTextureUsage::DepthAttachment:
			return rhi::EResourceState::eDepthStencilWrite;
		case ERGTextureUsage::CopySrc:
			return rhi::EResourceState::eTransferSrc;
		case ERGTextureUsage::CopyDst:
			return rhi::EResourceState::eTransferDst;
		case ERGTextureUsage::StorageRead:
			return rhi::EResourceState::eShaderRead;
		case ERGTextureUsage::StorageWrite:
			return rhi::EResourceState::eShaderWrite;
		case ERGTextureUsage::Present:
			return rhi::EResourceState::ePresent;
		default:
			return rhi::EResourceState::eUndefined;
		}
	}

	rhi::EResourceState convertBufferState(ERGBufferUsage usage)
	{
		switch (usage)
		{
		case ERGBufferUsage::Uniform:
			return rhi::EResourceState::eConstantBuffer;
		case ERGBufferUsage::CopySrc:
			return rhi::EResourceState::eTransferSrc;
		case ERGBufferUsage::CopyDst:
			return rhi::EResourceState::eTransferDst;
		case ERGBufferUsage::StorageRead:
			return rhi::EResourceState::eShaderRead;
		case ERGBufferUsage::StorageWrite:
			return rhi::EResourceState::eShaderWrite;
		default:
			return rhi::EResourceState::eUndefined;
		}
	}

	Builder::Builder(RenderGraph& graph, RenderPass& pass)
		: graph_(graph)
		, pass_(pass)
	{

	}

	void Builder::accessTexture(RGTextureHandle handle, ERGTextureUsage usage)
	{
		RGTextureUsage textureUsage
		{
			.handle = handle,
			.usage = usage,
		};

		pass_.textureUsages.push_back(textureUsage);
	}

	void Builder::accessBuffer(RGBufferHandle handle, ERGBufferUsage usage)
	{
		RGBufferUsage bufferUsage
		{
			.handle = handle,
			.usage = usage,
		};

		pass_.bufferUsages.push_back(bufferUsage);
	}

	Context::Context(RenderGraph& graph, const std::shared_ptr<rhi::IRHI>& rhi, rhi::CommandBufferHandle cmd)
		: graph_(graph)
		, rhi_(rhi)
		, cmd_(cmd)
	{

	}

	rhi::TextureHandle Context::getTexture(RGTextureHandle handle)
	{
		if (graph_.textures().size() > static_cast<size_t>(handle))
		{
			return graph_.textures()[handle].physical;
		}

		return INVALID_HANDLE;
	}

	rhi::BufferHandle Context::getBuffer(RGBufferHandle handle)
	{
		if (graph_.buffers().size() > static_cast<size_t>(handle))
		{
			return graph_.buffers()[handle].physical;
		}

		return INVALID_HANDLE;
	}

	RenderGraph::RenderGraph(const std::shared_ptr<rhi::IRHI>& rhi)
		: rhi_(rhi)
	{}


	RGTextureHandle RenderGraph::createTexture(const RGTextureDesc& desc)
	{
		RGTextureHandle handle = (RGTextureHandle)textures_.size();

		RGTexture texture
		{
			.name = desc.name,
			.handle = handle,
			.desc = desc,
			.physical = INVALID_HANDLE,
			.initialState = desc.initialState,
			.lastState = desc.initialState,
		};

		textures_.push_back(texture);

		return handle;
	}

	RGBufferHandle RenderGraph::createBuffer(const RGBufferDesc& desc)
	{
		RGBufferHandle handle = (RGBufferHandle)buffers_.size();

		RGBuffer buffer
		{
			.name = desc.name,
			.handle = handle,
			.desc = desc,
			.physical = INVALID_HANDLE,
			.initialState = desc.initialState,
			.lastState = desc.initialState,
		};

		buffers_.push_back(buffer);

		return handle;
	}

	RGTextureHandle RenderGraph::importTexture(std::string name, rhi::TextureHandle rhiHandle, ERGTextureUsage initialState)
	{
		RGTextureHandle handle = (RGTextureHandle)textures_.size();
		
		RGTexture texture
		{
			.name = name,
			.handle = handle,
			.desc = RGTextureDesc{},
			.physical = rhiHandle,
			.imported = true,
			.initialState = initialState,
			.lastState = initialState,
		};

		textures_.push_back(texture);

		return handle;
	}

	void RenderGraph::addPass(const RenderPass& pass)
	{
		passes_.push_back(pass);
	}

	void RenderGraph::compile()
	{
		compiledPasses_.clear();


		allocate();

		RGPassHandle handle = 0;
		for (auto& pass : passes_)
		{
			Builder builder(*this, pass);
			
			CompiledPass compiledPass;
			compiledPass.pass = &pass;
			compiledPass.handle = handle;

			pass.setup(builder);
			// コンパクトにしたい
			for (auto& textureUsage : pass.textureUsages)
			{
				auto& texture = textures_[textureUsage.handle];
				if (texture.firstUseHandle == INVALID_HANDLE) texture.firstUseHandle = handle;
				texture.lastUseHandle = handle;
				rhi::TextureBarrier barrier
				{
					.texture = textures_[textureUsage.handle].physical,
					.before = convertTextureState(texture.lastState),
					.after = convertTextureState(textureUsage.usage),
				};
				compiledPass.textureBarriers.push_back(barrier);
				texture.lastState = textureUsage.usage;
			}
			for (auto& bufferUsage : pass.bufferUsages)
			{
				auto& buffer = buffers_[bufferUsage.handle];
				if (buffer.firstUseHandle == INVALID_HANDLE) buffer.firstUseHandle = handle;
				buffer.lastUseHandle = handle;
				rhi::BufferBarrier barrier
				{
					.buffer = buffers_[bufferUsage.handle].physical,
					.before = convertBufferState(buffer.lastState),
					.after = convertBufferState(bufferUsage.usage),
				};
				compiledPass.bufferBarriers.push_back(barrier);
				buffer.lastState = bufferUsage.usage;
			}

			compiledPasses_.push_back(compiledPass);
			handle++;
		}
	}

	void RenderGraph::execute()
	{
		rhi::CommandBufferHandle cmd = rhi_->getCurrentCommandBuffer();

		Context context(*this, rhi_, cmd);
		for (auto& pass : compiledPasses_)
		{
			emitBarriers(context, pass.textureBarriers);
			emitBarriers(context, pass.bufferBarriers);


			pass.pass->execute(context);

			// Only resources the graph allocated are released here. An imported texture is
			// owned by the caller and lives across frames; freeing it destroys a resource
			// this very command list still references.
			for (auto& texture : textures_)
			{
				if (!texture.imported && texture.lastUseHandle == pass.handle)
				{
					rhi_->freeImage(texture.physical);
				}
			}

			for (auto& buffer : buffers_)
			{
				if (buffer.lastUseHandle == pass.handle)
				{
					rhi_->freeBuffer(buffer.physical);
				}
			}
		}
	}

	void RenderGraph::allocate()
	{
		for (auto& texture : textures_)
		{
			if (texture.physical != INVALID_HANDLE) continue;
			texture.physical = rhi_->createTexture(rhi::TextureDesc
				{
					.width = texture.desc.width,
					.height = texture.desc.height,
					.depth = texture.desc.depth,
					.usage = convertTextureUsage(texture.desc.initialState),
					.format = convertTextureFormat(texture.desc.format),
					.memoryType = convertMemoryType(texture.desc.memoryType),
				});
		}

		for (auto& buffer : buffers_)
		{
			if (buffer.physical != INVALID_HANDLE) continue;
			buffer.physical = rhi_->createBuffer(rhi::BufferDesc
				{
					.size = buffer.desc.size,
					.usage = convertBufferUsage(buffer.desc.initialState),
					.memoryType = convertMemoryType(buffer.desc.memoryType),
				});
		}
	}

	void RenderGraph::emitBarriers(Context& context, const std::vector<rhi::TextureBarrier>& barriers)
	{
		for (auto& barrier : barriers)
		{
			context.rhi()->textureBarrier(context.cmd(), barrier);
		}
	}

	void RenderGraph::emitBarriers(Context& context, const std::vector<rhi::BufferBarrier>& barriers)
	{
		for (auto& barrier : barriers)
		{
			context.rhi()->bufferBarrier(context.cmd(), barrier);
		}
	}
}
