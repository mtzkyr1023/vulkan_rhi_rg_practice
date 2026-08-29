
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
		case ERGFormat::B8G8R8A8_UNORM:
			return rhi::ETextureFormat::eB8G8R8A8_UNORM;
		case ERGFormat::R16G16_SFLOAT:
			return rhi::ETextureFormat::eR16G16_SFLOAT;
		case ERGFormat::R16G16B16A16_SFLOAT:
			return rhi::ETextureFormat::eR16G16B16A16_SFLOAT;
		case ERGFormat::R32G32_UINT:
			return rhi::ETextureFormat::eR32G32_UINT;
		case ERGFormat::D32_SFLOAT:
			return rhi::ETextureFormat::eD32_SFLOAT;
		default:
			return rhi::ETextureFormat::eR8G8B8A8_UNORM;
		}
	}

	rhi::ETextureUsage convertTextureCapabilities(ERGTextureCapability capabilities)
	{
		rhi::ETextureUsage usage = (rhi::ETextureUsage)0;

		if ((capabilities & ERGTextureCapability::ColorAttachment) == ERGTextureCapability::ColorAttachment)
			usage |= rhi::ETextureUsage::eColorAttachment;
		if ((capabilities & ERGTextureCapability::DepthAttachment) == ERGTextureCapability::DepthAttachment)
			usage |= rhi::ETextureUsage::eDepthStencilAttachment;
		if ((capabilities & ERGTextureCapability::ShaderRead) == ERGTextureCapability::ShaderRead)
			usage |= rhi::ETextureUsage::eSampled;
		if ((capabilities & ERGTextureCapability::Storage) == ERGTextureCapability::Storage)
			usage |= rhi::ETextureUsage::eStorage;
		if ((capabilities & ERGTextureCapability::CopySrc) == ERGTextureCapability::CopySrc)
			usage |= rhi::ETextureUsage::eTransferSrc;
		if ((capabilities & ERGTextureCapability::CopyDst) == ERGTextureCapability::CopyDst)
			usage |= rhi::ETextureUsage::eTransferDst;

		return usage;
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

				// Emitted even when the state does not change. Two consecutive passes
				// writing the same attachment still need a dependency between them, and
				// only the backend knows whether its API expresses that as a barrier: D3D12
				// orders same-state writes itself and rejects a no-op transition, while
				// Vulkan needs an explicit one.
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

		// Every texture goes back to the state it was declared to start in.
		//
		// The graph is rebuilt from scratch each frame and resets lastState to
		// initialState, but the resource behind it is not: an imported one lives across
		// frames and a transient comes back off the free list carrying whatever the last
		// frame left in it. Without this the declaration is only true for a pass sequence
		// that happens to end where it began, and adding a pass that reads the depth buffer
		// at the end of the frame silently breaks the next frame's clear.
		//
		// An initial state of Undefined means the caller is managing the transitions
		// itself, which is how the backbuffer is imported -- the RHI moves it to present
		// after this returns, and a restore here would fight that.
		for (auto& texture : textures_)
		{
			if (texture.physical == INVALID_HANDLE)
				continue;

			if (texture.desc.initialState == ERGTextureUsage::Undefined)
				continue;

			if (texture.lastState == texture.desc.initialState)
				continue;

			// Safe even though the transient was already handed back at its last use: the
			// free is deferred to the frame that reuses the slot, so the resource is still
			// alive and it is the state travelling with it that has to be right.
			rhi::TextureBarrier barrier
			{
				.texture = texture.physical,
				.before = convertTextureState(texture.lastState),
				.after = convertTextureState(texture.desc.initialState),
			};

			rhi_->textureBarrier(cmd, barrier);

			texture.lastState = texture.desc.initialState;
		}
	}

	void RenderGraph::allocate()
	{
		for (auto& texture : textures_)
		{
			if (texture.physical != INVALID_HANDLE) continue;

			// Capabilities cover the texture's whole life; the initial state is only where
			// it starts. Falling back to the state keeps callers that predate this working,
			// but anything rendered into and then sampled has to declare both.
			const rhi::ETextureUsage usage = ((u32)texture.desc.capabilities != 0)
				? convertTextureCapabilities(texture.desc.capabilities)
				: convertTextureUsage(texture.desc.initialState);

			texture.physical = rhi_->createTexture(rhi::TextureDesc
				{
					.width = texture.desc.width,
					.height = texture.desc.height,
					.depth = texture.desc.depth,
					.usage = usage,
					.mipLevels = texture.desc.mipLevels,
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
