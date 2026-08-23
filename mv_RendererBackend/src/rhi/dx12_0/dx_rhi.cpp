
#include "rhi/dx12_0/dx_rhi.h"

namespace mv::backend::dx12_0
{
	static D3D12_RESOURCE_STATES toDxResourceState(rhi::EResourceState state)
	{
		switch (state)
		{
		case rhi::EResourceState::eColorAttachment:      return D3D12_RESOURCE_STATE_RENDER_TARGET;
		case rhi::EResourceState::eCopySrc:              return D3D12_RESOURCE_STATE_COPY_SOURCE;
		case rhi::EResourceState::eCopyDst:              return D3D12_RESOURCE_STATE_COPY_DEST;
		case rhi::EResourceState::eVertexBuffer:         return D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
		case rhi::EResourceState::eIndexBuffer:          return D3D12_RESOURCE_STATE_INDEX_BUFFER;
		case rhi::EResourceState::eConstantBuffer:       return D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
		case rhi::EResourceState::eShaderRead:           return D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
		case rhi::EResourceState::eShaderWrite:          return D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
		case rhi::EResourceState::eRenderTarget:         return D3D12_RESOURCE_STATE_RENDER_TARGET;
		case rhi::EResourceState::eDepthStencilWrite:    return D3D12_RESOURCE_STATE_DEPTH_WRITE;
		case rhi::EResourceState::eDepthStencilRead:     return D3D12_RESOURCE_STATE_DEPTH_READ;
		case rhi::EResourceState::eTransferSrc:          return D3D12_RESOURCE_STATE_COPY_SOURCE;
		case rhi::EResourceState::eTransferDst:          return D3D12_RESOURCE_STATE_COPY_DEST;
		case rhi::EResourceState::ePresent:              return D3D12_RESOURCE_STATE_PRESENT;
		case rhi::EResourceState::eUndefined:
		default:                                         return D3D12_RESOURCE_STATE_COMMON;
		}
	}

	DXGI_FORMAT toDxgiFormat(rhi::ETextureFormat format)
	{
		switch (format)
		{
		case rhi::ETextureFormat::eR8G8B8A8_UNORM:     return DXGI_FORMAT_R8G8B8A8_UNORM;
		case rhi::ETextureFormat::eR8G8B8A8_SRGB:      return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
		case rhi::ETextureFormat::eB8G8R8A8_UNORM:     return DXGI_FORMAT_B8G8R8A8_UNORM;
		case rhi::ETextureFormat::eD32_SFLOAT:         return DXGI_FORMAT_D32_FLOAT;
		case rhi::ETextureFormat::eD24_UNORM_S8_UINT:  return DXGI_FORMAT_D24_UNORM_S8_UINT;
		case rhi::ETextureFormat::eR32G32_UINT:        return DXGI_FORMAT_R32G32_UINT;
		case rhi::ETextureFormat::eUndefined:
		default:                                       return DXGI_FORMAT_UNKNOWN;
		}
	}

	static rhi::ETextureFormat fromDxgiFormat(DXGI_FORMAT format)
	{
		switch (format)
		{
		case DXGI_FORMAT_R8G8B8A8_UNORM:       return rhi::ETextureFormat::eR8G8B8A8_UNORM;
		case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:  return rhi::ETextureFormat::eR8G8B8A8_SRGB;
		case DXGI_FORMAT_B8G8R8A8_UNORM:       return rhi::ETextureFormat::eB8G8R8A8_UNORM;
		case DXGI_FORMAT_D32_FLOAT:            return rhi::ETextureFormat::eD32_SFLOAT;
		case DXGI_FORMAT_D24_UNORM_S8_UINT:    return rhi::ETextureFormat::eD24_UNORM_S8_UINT;
		case DXGI_FORMAT_R32G32_UINT:          return rhi::ETextureFormat::eR32G32_UINT;
		default:                               return rhi::ETextureFormat::eUndefined;
		}
	}

	void DxRHI::initialize(void* hwnd)
	{
		device_.initialize();
		swapchain_.initialize(&device_, hwnd);

		commandPool_[(u32)rhi::EQueueType::eGraphics].initialize(&device_, D3D12_COMMAND_LIST_TYPE_DIRECT);
		commandPool_[(u32)rhi::EQueueType::eCompute].initialize(&device_, D3D12_COMMAND_LIST_TYPE_COMPUTE);
		commandPool_[(u32)rhi::EQueueType::eTransfer].initialize(&device_, D3D12_COMMAND_LIST_TYPE_COPY);

		for (u32 i = 0; i < (u32)rhi::EMemoryType::eNum; i++)
		{
			// D3D12 places every buffer on a 64KB boundary, so even tiny buffers cost 64KB
			// of pool; 128KB held only two of them.
			memoryAllocator_[i].initialize(&device_, 64ull * 1024 * 1024, (rhi::EMemoryType)i);
		}

		shaderManager_.initialize(&device_);
		layoutManager_.initialize(&device_);
		pipelineManager_.initialize(&device_, &shaderManager_, &layoutManager_);

		rtvDescriptorAllocator_.initialize(&device_, 1, 8, D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
		dsvDescriptorAllocator_.initialize(&device_, 1, 8, D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
		srvDescriptorAllocator_.initialize(&device_, 1, 256, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, false);
		// A bindless table reserves its whole declared range out of this heap, so it has to
		// be sized for that rather than for the number of individual bindings.
		globalDescriptorAllocator_.initialize(&device_, 1, 16384, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, true);
		samplerDescriptorAllocator_.initialize(&device_, 1, 256, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, true);

		frameResources_.resize((size_t)framesInFlight_);
		for (u32 i = 0; i < framesInFlight_; i++)
		{
			frameResources_[i].initialize(&device_, &commandPool_[(u32)rhi::EQueueType::eGraphics]);
		}

		createBackbuffer();
	}

	void DxRHI::deinitialize()
	{
		device_.waitIdle();

		for (u32 i = 0; i < framesInFlight_; i++)
		{
			frameResources_[i].deinitialize(&device_, &commandPool_[(u32)rhi::EQueueType::eGraphics]);
		}

		commandPool_[(u32)rhi::EQueueType::eGraphics].deinitialize();
		commandPool_[(u32)rhi::EQueueType::eCompute].deinitialize();
		commandPool_[(u32)rhi::EQueueType::eTransfer].deinitialize();

		shaderManager_.deinitialize();
		layoutManager_.deinitialize();
		pipelineManager_.deinitialize();

		for (u32 i = 0; i < (u32)rhi::EMemoryType::eNum; i++)
		{
			memoryAllocator_[i].deinitialize();
		}

		rtvDescriptorAllocator_.deinitialize();
		dsvDescriptorAllocator_.deinitialize();
		srvDescriptorAllocator_.deinitialize();
		globalDescriptorAllocator_.deinitialize();
		samplerDescriptorAllocator_.deinitialize();

		swapchain_.deinitialize();

		device_.deinitialize();
	}

	void DxRHI::waitIdle()
	{
		device_.waitIdle();
	}

	rhi::FrameContext DxRHI::beginFrame()
	{
		rhi::FrameContext context;
		currentFrame_ = (currentFrame_ + 1) % framesInFlight_;

		context.currentFrameIndex = currentFrame_;

		DxFrameResource& frameResource = frameResources_[currentFrame_];

		if (frameResource.inFlightFence->GetCompletedValue() < frameResource.fenceValue)
		{
			HANDLE fenceEvent = CreateEvent(NULL, FALSE, FALSE, NULL);

			frameResource.inFlightFence->SetEventOnCompletion(frameResource.fenceValue, fenceEvent);

			WaitForSingleObject(fenceEvent, INFINITE);
			CloseHandle(fenceEvent);
		}

		// Safe to reset now: the fence wait above guarantees the GPU is done with
		// every command list this frame's own allocator has ever produced.
		frameResource.allocator->Reset();

		DxCommandList& commandBuffer = commandPool_[(u32)rhi::EQueueType::eGraphics].getCommandList(frameResource.commandBuffer);
		ID3D12GraphicsCommandList* cmd = commandBuffer.commandList();
		cmd->Reset(frameResource.allocator.Get(), nullptr);

		// Reset() drops every piece of command list state, the root signature included.
		commandBuffer.setBoundRootSignature(nullptr);

		swapchain_.acquireNextImage();

		context.backbuffer = backbuffers_[swapchain_.imageIndex()];
		context.cmd = frameResource.commandBuffer;

		frameResource.backbuffer = backbuffers_[swapchain_.imageIndex()];

		// No transition into eColorAttachment here: the render graph emits it as the
		// imported backbuffer's first access, and endFrame emits the matching one back
		// to ePresent. Adding one here would double-transition and desync state tracking.
		return context;
	}

	void DxRHI::endFrame()
	{
		DxFrameResource& frameResource = frameResources_[currentFrame_];

		DxCommandList& commandBuffer = commandPool_[(u32)rhi::EQueueType::eGraphics].getCommandList(frameResource.commandBuffer);
		ID3D12GraphicsCommandList* cmd = commandBuffer.commandList();

		rhi::TextureBarrier barrier
		{
			.texture = frameResource.backbuffer,
			.before = rhi::EResourceState::eColorAttachment,
			.after = rhi::EResourceState::ePresent,
		};

		textureBarrier(frameResource.commandBuffer, barrier);

		cmd->Close();

		ID3D12CommandList* commandList[] =
		{
			cmd
		};
		device_.graphicsQueue()->ExecuteCommandLists(1, commandList);

		frameResource.fenceValue++;
		device_.graphicsQueue()->Signal(frameResource.inFlightFence.Get(), frameResource.fenceValue);

		swapchain_.present(device_.graphicsQueue());
	}

	void DxRHI::beginRenderPass(rhi::CommandBufferHandle cmd, const rhi::RenderPassDesc& desc)
	{
		ID3D12GraphicsCommandList* commandList = commandPool_[(u32)rhi::EQueueType::eGraphics].getCommandList(cmd).commandList();

		std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> rtvs;
		for (const auto& target : desc.colorTargets)
		{
			rtvs.push_back(images_[target.texture].rtv);
		}

		const bool hasDepth = (desc.depthTarget.texture != INVALID_HANDLE);
		const D3D12_CPU_DESCRIPTOR_HANDLE dsv = hasDepth ? images_[desc.depthTarget.texture].dsv : D3D12_CPU_DESCRIPTOR_HANDLE{};

		commandList->OMSetRenderTargets((UINT)rtvs.size(), rtvs.data(), FALSE, hasDepth ? &dsv : nullptr);

		for (u32 i = 0; i < (u32)desc.colorTargets.size(); i++)
		{
			const auto& target = desc.colorTargets[i];
			if (!target.clear) continue;

			commandList->ClearRenderTargetView(rtvs[i], target.clearColor, 0, nullptr);
		}

		if (hasDepth && desc.depthTarget.clear)
		{
			commandList->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, desc.depthTarget.clearDepth, 0, 0, nullptr);
		}
	}

	void DxRHI::endRenderPass(rhi::CommandBufferHandle cmd)
	{
		// D3D12 has no scope to close when render targets are bound the classic way.
	}

	void DxRHI::bindGraphicsPipeline(rhi::CommandBufferHandle cmd, rhi::PipelineHandle pipeline)
	{
		ID3D12GraphicsCommandList* commandList = commandPool_[(u32)rhi::EQueueType::eGraphics].getCommandList(cmd).commandList();

		DxCommandList& commandBuffer = commandPool_[(u32)rhi::EQueueType::eGraphics].getCommandList(cmd);
		const DxPipeline& dxPipeline = pipelineManager_.pipeline(pipeline);

		// A VkPipeline carries its layout, so binding one in D3D12 has to set the root
		// signature and primitive topology too to end up in the same state.
		if (commandBuffer.boundRootSignature() != dxPipeline.rootSignature())
		{
			commandList->SetGraphicsRootSignature(dxPipeline.rootSignature());
			commandBuffer.setBoundRootSignature(dxPipeline.rootSignature());
		}

		commandList->SetPipelineState(dxPipeline.pipelineState());
		commandList->IASetPrimitiveTopology(dxPipeline.topology());
	}

	void DxRHI::setViewport(rhi::CommandBufferHandle cmd, f32 x, f32 y, f32 width, f32 height)
	{
		ID3D12GraphicsCommandList* commandList = commandPool_[(u32)rhi::EQueueType::eGraphics].getCommandList(cmd).commandList();

		D3D12_VIEWPORT viewport{};
		viewport.TopLeftX = x;
		viewport.TopLeftY = y;
		viewport.Width = width;
		viewport.Height = height;
		viewport.MinDepth = 0.0f;
		viewport.MaxDepth = 1.0f;

		commandList->RSSetViewports(1, &viewport);
	}

	void DxRHI::setScissor(rhi::CommandBufferHandle cmd, s32 x, s32 y, u32 width, u32 height)
	{
		ID3D12GraphicsCommandList* commandList = commandPool_[(u32)rhi::EQueueType::eGraphics].getCommandList(cmd).commandList();

		D3D12_RECT scissor{};
		scissor.left = x;
		scissor.top = y;
		scissor.right = x + (LONG)width;
		scissor.bottom = y + (LONG)height;

		commandList->RSSetScissorRects(1, &scissor);
	}

	void DxRHI::draw(rhi::CommandBufferHandle cmd, u32 vertexCount, u32 instanceCount, u32 firstVertex, u32 firstInstance)
	{
		ID3D12GraphicsCommandList* commandList = commandPool_[(u32)rhi::EQueueType::eGraphics].getCommandList(cmd).commandList();

		commandList->DrawInstanced(vertexCount, instanceCount, firstVertex, firstInstance);
	}

	rhi::ETextureFormat DxRHI::backbufferFormat() const
	{
		return fromDxgiFormat(swapchain_.format());
	}

	void DxRHI::bindVertexBuffer(rhi::CommandBufferHandle cmd, u32 slot, rhi::BufferHandle buffer, u32 stride, u64 offset)
	{
		ID3D12GraphicsCommandList* commandList = commandPool_[(u32)rhi::EQueueType::eGraphics].getCommandList(cmd).commandList();

		const DxBuffer& dxBuffer = buffers_[buffer];

		// Unlike Vulkan, D3D12 carries the stride on the view rather than the pipeline.
		D3D12_VERTEX_BUFFER_VIEW view{};
		view.BufferLocation = dxBuffer.resource->GetGPUVirtualAddress() + offset;
		view.SizeInBytes = (UINT)(dxBuffer.desc.size - offset);
		view.StrideInBytes = stride;

		commandList->IASetVertexBuffers(slot, 1, &view);
	}

	void DxRHI::bindIndexBuffer(rhi::CommandBufferHandle cmd, rhi::BufferHandle buffer, rhi::EIndexFormat format, u64 offset)
	{
		ID3D12GraphicsCommandList* commandList = commandPool_[(u32)rhi::EQueueType::eGraphics].getCommandList(cmd).commandList();

		const DxBuffer& dxBuffer = buffers_[buffer];

		D3D12_INDEX_BUFFER_VIEW view{};
		view.BufferLocation = dxBuffer.resource->GetGPUVirtualAddress() + offset;
		view.SizeInBytes = (UINT)(dxBuffer.desc.size - offset);
		view.Format = (format == rhi::EIndexFormat::eUint16) ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT;

		commandList->IASetIndexBuffer(&view);
	}

	void DxRHI::drawIndexed(rhi::CommandBufferHandle cmd, u32 indexCount, u32 instanceCount, u32 firstIndex, s32 vertexOffset, u32 firstInstance)
	{
		ID3D12GraphicsCommandList* commandList = commandPool_[(u32)rhi::EQueueType::eGraphics].getCommandList(cmd).commandList();

		commandList->DrawIndexedInstanced(indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
	}

	void* DxRHI::mapBuffer(rhi::BufferHandle handle)
	{
		DxBuffer& buffer = buffers_[handle];

		const bool isReadback = (buffer.desc.memoryType == rhi::EMemoryType::eReadback);
		if (buffer.desc.memoryType != rhi::EMemoryType::eHostVisibleBuffer && !isReadback)
			return nullptr;

		// D3D12 maps the resource, not the heap it was placed in. Readback buffers are read
		// in full; for the write-only ones an empty read range says so.
		void* mapped = nullptr;
		D3D12_RANGE readRange{ 0, isReadback ? (SIZE_T)buffer.desc.size : 0 };
		if (FAILED(buffer.resource->Map(0, &readRange, &mapped)))
			return nullptr;

		return mapped;
	}

	void DxRHI::unmapBuffer(rhi::BufferHandle handle)
	{
		buffers_[handle].resource->Unmap(0, nullptr);
	}

	void DxRHI::writeBuffer(rhi::BufferHandle handle, const void* data, u64 size, u64 offset)
	{
		u8* dst = static_cast<u8*>(mapBuffer(handle));
		if (!dst)
		{
			throw std::exception("writeBuffer on a buffer that is not host visible");
		}

		memcpy(dst + offset, data, (size_t)size);

		unmapBuffer(handle);
	}

	void DxRHI::uploadBuffer(rhi::BufferHandle handle, const void* data, u64 size)
	{
		rhi::BufferDesc stagingDesc{};
		stagingDesc.size = size;
		stagingDesc.usage = rhi::EBufferUsage::eTransferSrc;
		stagingDesc.memoryType = rhi::EMemoryType::eHostVisibleBuffer;

		const rhi::BufferHandle staging = createBuffer(stagingDesc);
		writeBuffer(staging, data, size, 0);

		// A one-shot allocator and list: recorded, submitted and waited on right here, so
		// the staging buffer is provably free by the time this returns.
		wrl::ComPtr<ID3D12CommandAllocator> allocator;
		device_.device()->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator));

		wrl::ComPtr<ID3D12GraphicsCommandList> commandList;
		device_.device()->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr, IID_PPV_ARGS(&commandList));

		commandList->CopyBufferRegion(buffers_[handle].resource.Get(), 0, buffers_[staging].resource.Get(), 0, size);
		commandList->Close();

		ID3D12CommandList* lists[] = { commandList.Get() };
		device_.graphicsQueue()->ExecuteCommandLists(1, lists);

		device_.waitIdle();

		releaseBuffer(staging);
	}

	void DxRHI::uploadTexture(rhi::TextureHandle handle, const rhi::TextureUpload* levels, u32 levelCount)
	{
		if (levelCount == 0) return;

		ID3D12Resource* texture = images_[handle].resource.Get();
		const D3D12_RESOURCE_DESC textureDesc = texture->GetDesc();

		// D3D12 requires each row of a texture copy source to start on a 256-byte boundary,
		// so the staging copy is row-by-row into a padded layout rather than a flat memcpy.
		// Asking for every subresource at once also lays the mip chain out in one buffer.
		std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> footprints(levelCount);
		std::vector<UINT> rowCounts(levelCount);
		std::vector<UINT64> rowSizes(levelCount);
		UINT64 totalBytes = 0;
		device_.device()->GetCopyableFootprints(&textureDesc, 0, levelCount, 0, footprints.data(), rowCounts.data(), rowSizes.data(), &totalBytes);

		rhi::BufferDesc stagingDesc{};
		stagingDesc.size = totalBytes;
		stagingDesc.usage = rhi::EBufferUsage::eTransferSrc;
		stagingDesc.memoryType = rhi::EMemoryType::eHostVisibleBuffer;

		const rhi::BufferHandle staging = createBuffer(stagingDesc);

		u8* dst = static_cast<u8*>(mapBuffer(staging));
		if (!dst)
		{
			throw std::exception("Failed to map texture staging buffer");
		}

		for (u32 level = 0; level < levelCount; level++)
		{
			const u8* src = static_cast<const u8*>(levels[level].data);

			for (UINT row = 0; row < rowCounts[level]; row++)
			{
				memcpy(dst + footprints[level].Offset + (size_t)row * footprints[level].Footprint.RowPitch,
					src + (size_t)row * rowSizes[level],
					(size_t)rowSizes[level]);
			}
		}

		unmapBuffer(staging);

		wrl::ComPtr<ID3D12CommandAllocator> allocator;
		device_.device()->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator));

		wrl::ComPtr<ID3D12GraphicsCommandList> commandList;
		device_.device()->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr, IID_PPV_ARGS(&commandList));

		D3D12_RESOURCE_BARRIER barrier{};
		barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		barrier.Transition.pResource = texture;
		barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
		barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
		commandList->ResourceBarrier(1, &barrier);

		for (u32 level = 0; level < levelCount; level++)
		{
			D3D12_TEXTURE_COPY_LOCATION dstLocation{};
			dstLocation.pResource = texture;
			dstLocation.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
			dstLocation.SubresourceIndex = level;

			D3D12_TEXTURE_COPY_LOCATION srcLocation{};
			srcLocation.pResource = buffers_[staging].resource.Get();
			srcLocation.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
			srcLocation.PlacedFootprint = footprints[level];

			commandList->CopyTextureRegion(&dstLocation, 0, 0, 0, &srcLocation, nullptr);
		}

		barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
		barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
		commandList->ResourceBarrier(1, &barrier);

		commandList->Close();

		ID3D12CommandList* lists[] = { commandList.Get() };
		device_.graphicsQueue()->ExecuteCommandLists(1, lists);

		device_.waitIdle();

		releaseBuffer(staging);
	}

	rhi::BindGroupHandle DxRHI::createBindGroup(const rhi::BindGroupDesc& desc)
	{
		const DxBindGroupLayout& layout = layoutManager_.layout(desc.layout);

		DxBindGroup group{};

		group.layout = desc.layout;

		// A descriptor table is one contiguous run, and array or bindless bindings occupy
		// `count` slots each, so the whole table is reserved in one go and each binding is
		// written at its own offset within it.
		const u32 viewSlots = layout.viewSlotCount();
		if (viewSlots > 0)
		{
			group.viewIndex = globalDescriptorAllocator_.allocateRange(viewSlots);

			for (const auto& item : desc.uniformBuffers)
			{
				const DxBuffer& buffer = buffers_[item.buffer];

				D3D12_CONSTANT_BUFFER_VIEW_DESC cbv{};
				cbv.BufferLocation = buffer.resource->GetGPUVirtualAddress() + item.offset;
				// Constant buffer views must be a multiple of 256 bytes.
				cbv.SizeInBytes = (UINT)(((item.range ? item.range : buffer.desc.size) + 255) & ~255ull);

				device_.device()->CreateConstantBufferView(
					&cbv,
					globalDescriptorAllocator_.getCpuHandle(group.viewIndex + layout.viewSlotOffset(item.binding)));
			}

			for (const auto& item : desc.storageBuffers)
			{
				const DxBuffer& buffer = buffers_[item.buffer];
				const D3D12_CPU_DESCRIPTOR_HANDLE dst =
					globalDescriptorAllocator_.getCpuHandle(group.viewIndex + layout.viewSlotOffset(item.binding));

				// Whether this is an SRV or a UAV is a property of the layout, not of the
				// binding, so the declared descriptor type decides.
				bool readWrite = false;
				for (const auto& declared : layout.viewBindings())
				{
					if (declared.binding == item.binding)
					{
						readWrite = (declared.type == rhi::EDescriptorType::eStorageBufferReadWrite);
						break;
					}
				}

				if (readWrite)
				{
					D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};
					uav.Format = DXGI_FORMAT_UNKNOWN;
					uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
					uav.Buffer.FirstElement = item.offset / (item.stride ? item.stride : 1);
					uav.Buffer.NumElements = item.count;
					uav.Buffer.StructureByteStride = item.stride;

					device_.device()->CreateUnorderedAccessView(buffer.resource.Get(), nullptr, &uav, dst);
				}
				else
				{
					D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
					srv.Format = DXGI_FORMAT_UNKNOWN;
					srv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
					srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
					srv.Buffer.FirstElement = item.offset / (item.stride ? item.stride : 1);
					srv.Buffer.NumElements = item.count;
					srv.Buffer.StructureByteStride = item.stride;

					device_.device()->CreateShaderResourceView(buffer.resource.Get(), &srv, dst);
				}
			}

			for (const auto& item : desc.sampledTextures)
			{
				createTextureSrv(
					item.texture,
					item.baseMip,
					item.mipCount,
					globalDescriptorAllocator_.getCpuHandle(group.viewIndex + layout.viewSlotOffset(item.binding) + item.arrayIndex));
			}
		}

		const u32 samplerSlots = layout.samplerSlotCount();
		if (samplerSlots > 0)
		{
			group.samplerIndex = samplerDescriptorAllocator_.allocateRange(samplerSlots);

			for (const auto& item : desc.samplers)
			{
				const bool anisotropic = (item.sampler.maxAnisotropy > 1);

				// Anisotropy is a filter mode in D3D12 rather than a separate toggle as it
				// is in Vulkan, so it replaces the min/mag/mip selection entirely.
				D3D12_FILTER filter;
				if (anisotropic)
				{
					filter = D3D12_FILTER_ANISOTROPIC;
				}
				else
				{
					filter = (item.sampler.filter == rhi::EFilterMode::eNearest)
						? D3D12_FILTER_MIN_MAG_MIP_POINT
						: D3D12_FILTER_MIN_MAG_MIP_LINEAR;
				}

				const D3D12_TEXTURE_ADDRESS_MODE address = (item.sampler.address == rhi::EAddressMode::eClampToEdge)
					? D3D12_TEXTURE_ADDRESS_MODE_CLAMP
					: D3D12_TEXTURE_ADDRESS_MODE_WRAP;

				D3D12_SAMPLER_DESC samplerDesc{};
				samplerDesc.Filter = filter;
				samplerDesc.AddressU = address;
				samplerDesc.AddressV = address;
				samplerDesc.AddressW = address;
				samplerDesc.MaxAnisotropy = anisotropic ? item.sampler.maxAnisotropy : 1;
				samplerDesc.MaxLOD = D3D12_FLOAT32_MAX;

				device_.device()->CreateSampler(
					&samplerDesc,
					samplerDescriptorAllocator_.getCpuHandle(group.samplerIndex + layout.samplerSlotOffset(item.binding) + item.arrayIndex));
			}
		}

		rhi::BindGroupHandle handle = (rhi::BindGroupHandle)bindGroups_.size();
		bindGroups_.push_back(group);

		return handle;
	}

	void DxRHI::createTextureSrv(rhi::TextureHandle texture, u32 baseMip, u32 mipCount, D3D12_CPU_DESCRIPTOR_HANDLE dst)
	{
		const DxImage& image = images_[texture];

		// A streaming system asks for the coarsest resident level, which for a small
		// texture can be past the end of a short chain. Clamping here keeps every caller
		// from having to know how many mips each texture happens to have.
		baseMip = std::min(baseMip, image.desc.mipLevels - 1);
		mipCount = std::min(mipCount ? mipCount : image.desc.mipLevels, image.desc.mipLevels - baseMip);

		// A null desc means the whole resource, which is what the common case wants.
		if (baseMip == 0 && mipCount == image.desc.mipLevels)
		{
			device_.device()->CreateShaderResourceView(image.resource.Get(), nullptr, dst);
			return;
		}

		D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
		srv.Format = toDxgiFormat(image.desc.format);
		srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srv.Texture2D.MostDetailedMip = baseMip;
		srv.Texture2D.MipLevels = mipCount;

		device_.device()->CreateShaderResourceView(image.resource.Get(), &srv, dst);
	}

	void DxRHI::copyBuffer(rhi::CommandBufferHandle cmd, rhi::BufferHandle dst, rhi::BufferHandle src, u64 size)
	{
		ID3D12GraphicsCommandList* commandList = commandPool_[(u32)rhi::EQueueType::eGraphics].getCommandList(cmd).commandList();

		// The source is typically a buffer a shader has just written. Buffers need no state
		// transition in D3D12, but the copy still has to be ordered after those writes.
		D3D12_RESOURCE_BARRIER barrier{};
		barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
		barrier.UAV.pResource = buffers_[src].resource.Get();
		commandList->ResourceBarrier(1, &barrier);

		commandList->CopyBufferRegion(buffers_[dst].resource.Get(), 0, buffers_[src].resource.Get(), 0, size);
	}

	void DxRHI::updateBindGroupTexture(rhi::BindGroupHandle group, u32 binding, u32 arrayIndex, rhi::TextureHandle texture, u32 baseMip, u32 mipCount)
	{
		const DxBindGroup& dxGroup = bindGroups_[group];
		const DxBindGroupLayout& layout = layoutManager_.layout(dxGroup.layout);

		createTextureSrv(
			texture,
			baseMip,
			mipCount,
			globalDescriptorAllocator_.getCpuHandle(dxGroup.viewIndex + layout.viewSlotOffset(binding) + arrayIndex));
	}

	void DxRHI::pushConstants(rhi::CommandBufferHandle cmd, rhi::PipelineLayoutHandle layout, const void* data, u32 size, u32 offset)
	{
		ID3D12GraphicsCommandList* commandList = commandPool_[(u32)rhi::EQueueType::eGraphics].getCommandList(cmd).commandList();

		DxCommandList& commandBuffer = commandPool_[(u32)rhi::EQueueType::eGraphics].getCommandList(cmd);
		const DxPipelineLayout& dxLayout = pipelineManager_.layout(layout);

		if (dxLayout.pushConstantSlot() == DxPipelineLayout::kNoPushConstants)
			return;

		// Same reason as bindBindGroup: root constants need a root signature, which Vulkan
		// takes as an argument instead of as command list state.
		if (commandBuffer.boundRootSignature() != dxLayout.rootSignature())
		{
			commandList->SetGraphicsRootSignature(dxLayout.rootSignature());
			commandBuffer.setBoundRootSignature(dxLayout.rootSignature());
		}

		commandList->SetGraphicsRoot32BitConstants(dxLayout.pushConstantSlot(), size / 4, data, offset / 4);
	}

	void DxRHI::bindBindGroup(rhi::CommandBufferHandle cmd, rhi::PipelineLayoutHandle layout, u32 setIndex, rhi::BindGroupHandle group)
	{
		ID3D12GraphicsCommandList* commandList = commandPool_[(u32)rhi::EQueueType::eGraphics].getCommandList(cmd).commandList();

		DxCommandList& commandBuffer = commandPool_[(u32)rhi::EQueueType::eGraphics].getCommandList(cmd);

		const DxBindGroup& dxGroup = bindGroups_[group];
		const DxPipelineLayout& dxLayout = pipelineManager_.layout(layout);
		const auto& slots = dxLayout.groupSlots(setIndex);

		// vkCmdBindDescriptorSets takes the pipeline layout, so Vulkan allows binding a set
		// before any pipeline. D3D12 rejects a descriptor table with no root signature set,
		// so the layout that was passed in is applied here when it is not already current.
		if (commandBuffer.boundRootSignature() != dxLayout.rootSignature())
		{
			commandList->SetGraphicsRootSignature(dxLayout.rootSignature());
			commandBuffer.setBoundRootSignature(dxLayout.rootSignature());
		}

		// Both shader-visible heaps have to be set together: SetDescriptorHeaps replaces the
		// whole binding, so setting them one at a time would unbind the other.
		ID3D12DescriptorHeap* heaps[] =
		{
			globalDescriptorAllocator_.heap(),
			samplerDescriptorAllocator_.heap(),
		};
		commandList->SetDescriptorHeaps(_countof(heaps), heaps);

		if (slots.viewTable != DxPipelineLayout::GroupRootSlots::kNone && dxGroup.viewIndex != DxBindGroup::kNone)
		{
			commandList->SetGraphicsRootDescriptorTable(slots.viewTable, globalDescriptorAllocator_.getGpuHandle(dxGroup.viewIndex));
		}

		if (slots.samplerTable != DxPipelineLayout::GroupRootSlots::kNone && dxGroup.samplerIndex != DxBindGroup::kNone)
		{
			commandList->SetGraphicsRootDescriptorTable(slots.samplerTable, samplerDescriptorAllocator_.getGpuHandle(dxGroup.samplerIndex));
		}
	}

	rhi::CommandBufferHandle DxRHI::allocateCommandBuffer(rhi::EQueueType queueType)
	{
		return commandPool_[(u32)queueType].allocate();
	}

	void DxRHI::createBackbuffer()
	{
		// The desc is what beginRenderPass derives its render area from, so imported
		// backbuffers have to describe themselves like any other texture.
		rhi::TextureDesc backbufferDesc{};
		backbufferDesc.width = swapchain_.width();
		backbufferDesc.height = swapchain_.height();
		backbufferDesc.depth = 1;
		backbufferDesc.usage = rhi::ETextureUsage::eColorAttachment;
		backbufferDesc.format = fromDxgiFormat(swapchain_.format());

		std::vector<rhi::TextureHandle> backbuffers;
		for (u32 i = 0; i < swapchain_.imageCount(); i++)
		{
			rhi::TextureHandle handle = (rhi::TextureHandle)images_.size();
			wrl::ComPtr<ID3D12Resource> backbuffer;

			swapchain_.swapchain()->GetBuffer(i, IID_PPV_ARGS(&backbuffer));

			DxImage image{};
			image.desc = backbufferDesc;
			image.resource = backbuffer;
			image.imported = true;

			image.descriptorIndex = rtvDescriptorAllocator_.allocate();
			image.rtv = rtvDescriptorAllocator_.getCpuHandle(image.descriptorIndex);
			device_.device()->CreateRenderTargetView(backbuffer.Get(), nullptr, image.rtv);

			images_.push_back(image);
			backbuffers.push_back(handle);
		}

		backbuffers_ = backbuffers;
	}

	rhi::BufferHandle DxRHI::createBuffer(const rhi::BufferDesc& desc)
	{
		for (auto& id : freeBufferList_)
		{
			const auto& d = buffers_[id].desc;
			if (d.memoryType == desc.memoryType && d.size == desc.size)
			{
				rhi::BufferHandle handle = id;
				id = freeBufferList_.back();
				freeBufferList_.pop_back();
				return handle;
			}
		}

		rhi::BufferHandle handle = (rhi::BufferHandle)buffers_.size();

		D3D12_RESOURCE_DESC resourceDesc{};
		resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		resourceDesc.Width = desc.size;
		resourceDesc.Height = 1;
		resourceDesc.DepthOrArraySize = 1;
		resourceDesc.MipLevels = 1;
		resourceDesc.Format = DXGI_FORMAT_UNKNOWN;
		resourceDesc.SampleDesc.Count = 1;
		resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		resourceDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

		// eStorage means a read-only structured buffer, which is an SRV and needs no flag.
		// Only the read-write variant takes ALLOW_UNORDERED_ACCESS, which would also make
		// the buffer illegal on an upload heap.
		if ((desc.usage & rhi::EBufferUsage::eStorageReadWrite) == rhi::EBufferUsage::eStorageReadWrite)
		{
			resourceDesc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
		}

		Allocation alloc = memoryAllocator_[(u32)desc.memoryType].allocate(resourceDesc);

		// Each heap type mandates its own starting state.
		D3D12_RESOURCE_STATES initialState = D3D12_RESOURCE_STATE_COMMON;
		if (desc.memoryType == rhi::EMemoryType::eHostVisibleBuffer) initialState = D3D12_RESOURCE_STATE_GENERIC_READ;
		else if (desc.memoryType == rhi::EMemoryType::eReadback) initialState = D3D12_RESOURCE_STATE_COPY_DEST;

		wrl::ComPtr<ID3D12Resource> resource;
		if (FAILED(device_.device()->CreatePlacedResource(alloc.heap.Get(), alloc.offset, &resourceDesc, initialState, nullptr, IID_PPV_ARGS(&resource))))
		{
			throw std::exception("Failed to create buffer");
		}

		DxBuffer buffer{};
		buffer.desc = desc;
		buffer.resource = resource;
		buffer.alloc = alloc;
		buffer.imported = false;

		buffers_.push_back(buffer);
		return handle;
	}

	rhi::TextureHandle DxRHI::createTexture(const rhi::TextureDesc& desc)
	{
		for (auto& id : freeImageList_)
		{
			const auto& d = images_[id].desc;
			if (d.memoryType == desc.memoryType && d.format == desc.format && d.width == desc.width && d.height == desc.height && d.depth == desc.depth)
			{
				rhi::TextureHandle handle = id;
				id = freeImageList_.back();
				freeImageList_.pop_back();
				return handle;
			}
		}

		rhi::TextureHandle handle = (rhi::TextureHandle)images_.size();

		D3D12_RESOURCE_DESC resourceDesc{};
		resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		resourceDesc.Width = desc.width;
		resourceDesc.Height = desc.height;
		resourceDesc.DepthOrArraySize = (UINT16)desc.depth;
		resourceDesc.MipLevels = (UINT16)(desc.mipLevels ? desc.mipLevels : rhi::mipLevelsFor(desc.width, desc.height));
		resourceDesc.Format = toDxgiFormat(desc.format);
		resourceDesc.SampleDesc.Count = 1;
		resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
		resourceDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

		if ((desc.usage & rhi::ETextureUsage::eStorage) == rhi::ETextureUsage::eStorage)
		{
			resourceDesc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
		}

		const bool isDepth = (desc.usage & rhi::ETextureUsage::eDepthStencilAttachment) == rhi::ETextureUsage::eDepthStencilAttachment;
		if (isDepth)
		{
			resourceDesc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
		}
		if ((desc.usage & rhi::ETextureUsage::eColorAttachment) == rhi::ETextureUsage::eColorAttachment)
		{
			resourceDesc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
		}

		Allocation alloc = memoryAllocator_[(u32)desc.memoryType].allocate(resourceDesc);

		// Declaring the value a target will actually be cleared with keeps the driver's fast
		// clear path available; depth clears to 1 and colour targets to zero. Color and
		// DepthStencil share a union, so only the member matching the type may be written.
		D3D12_CLEAR_VALUE clearValue{};
		clearValue.Format = resourceDesc.Format;
		if (isDepth)
		{
			clearValue.DepthStencil.Depth = 1.0f;
		}

		// A render target that is also sampled rests in the shader-read state between
		// frames, so it is created there and the render graph's per-frame barriers line up
		// from the very first frame.
		const bool isSampledRenderTarget =
			((desc.usage & rhi::ETextureUsage::eColorAttachment) == rhi::ETextureUsage::eColorAttachment) &&
			((desc.usage & rhi::ETextureUsage::eSampled) == rhi::ETextureUsage::eSampled);

		D3D12_RESOURCE_STATES initialState = D3D12_RESOURCE_STATE_COMMON;
		if (isDepth) initialState = D3D12_RESOURCE_STATE_DEPTH_WRITE;
		// Taken from the same mapping the barriers use, so the resting state cannot drift
		// from what a transition to eShaderRead expects to find.
		else if (isSampledRenderTarget) initialState = toDxResourceState(rhi::EResourceState::eShaderRead);

		wrl::ComPtr<ID3D12Resource> resource;
		const bool isRenderTarget = (desc.usage & rhi::ETextureUsage::eColorAttachment) == rhi::ETextureUsage::eColorAttachment;
		const D3D12_CLEAR_VALUE* clearValuePtr = (isDepth || isRenderTarget) ? &clearValue : nullptr;

		if (FAILED(device_.device()->CreatePlacedResource(alloc.heap.Get(), alloc.offset, &resourceDesc, initialState, clearValuePtr, IID_PPV_ARGS(&resource))))
		{
			throw std::exception("Failed to create image");
		}

		DxImage image{};
		image.desc = desc;
		// The stored desc carries the resolved level count so the upload path does not have
		// to recompute it.
		image.desc.mipLevels = resourceDesc.MipLevels;
		image.resource = resource;
		image.alloc = alloc;
		image.imported = false;

		if (isDepth)
		{
			image.descriptorIndex = dsvDescriptorAllocator_.allocate();
			image.dsv = dsvDescriptorAllocator_.getCpuHandle(image.descriptorIndex);

			D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
			dsvDesc.Format = resourceDesc.Format;
			dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;

			device_.device()->CreateDepthStencilView(resource.Get(), &dsvDesc, image.dsv);
		}
		else
		{
			// A texture can be both, so these are not exclusive and land in separate slots.
			if ((desc.usage & rhi::ETextureUsage::eColorAttachment) == rhi::ETextureUsage::eColorAttachment)
			{
				image.rtv = rtvDescriptorAllocator_.getCpuHandle(rtvDescriptorAllocator_.allocate());
				device_.device()->CreateRenderTargetView(resource.Get(), nullptr, image.rtv);
			}

			if ((desc.usage & rhi::ETextureUsage::eSampled) == rhi::ETextureUsage::eSampled)
			{
				image.descriptorIndex = srvDescriptorAllocator_.allocate();
				image.cpu = srvDescriptorAllocator_.getCpuHandle(image.descriptorIndex);
				device_.device()->CreateShaderResourceView(resource.Get(), nullptr, image.cpu);
			}
		}

		images_.push_back(image);
		return handle;
	}

	rhi::ShaderHandle DxRHI::createShader(const rhi::ShaderDesc& desc)
	{
		return shaderManager_.createShader(desc);
	}

	rhi::BindGroupLayoutHandle DxRHI::createBindGroupLayout(const rhi::BindGroupLayoutDesc& desc)
	{
		return layoutManager_.createBindGroupLayout(desc);
	}

	rhi::PipelineLayoutHandle DxRHI::createPipelineLayout(const rhi::PipelineLayoutDesc& desc)
	{
		return pipelineManager_.createPipelineLayout(desc);
	}

	rhi::PipelineHandle DxRHI::createGraphicsPipeline(const rhi::GraphicsPipelineDesc& desc)
	{
		return pipelineManager_.createPipeline(desc);
	}

	void DxRHI::textureBarrier(rhi::CommandBufferHandle cmd, const rhi::TextureBarrier& barrier)
	{
		ID3D12GraphicsCommandList* commandList = commandPool_[(u32)rhi::EQueueType::eGraphics].getCommandList(cmd).commandList();

		D3D12_RESOURCE_BARRIER b{};
		b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		b.Transition.pResource = images_[barrier.texture].resource.Get();
		b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		b.Transition.StateBefore = toDxResourceState(barrier.before);
		b.Transition.StateAfter = toDxResourceState(barrier.after);

		commandList->ResourceBarrier(1, &b);
	}

	void DxRHI::bufferBarrier(rhi::CommandBufferHandle cmd, const rhi::BufferBarrier& barrier)
	{
		ID3D12GraphicsCommandList* commandList = commandPool_[(u32)rhi::EQueueType::eGraphics].getCommandList(cmd).commandList();

		D3D12_RESOURCE_BARRIER b{};
		b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		b.Transition.pResource = buffers_[barrier.buffer].resource.Get();
		b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		b.Transition.StateBefore = toDxResourceState(barrier.before);
		b.Transition.StateAfter = toDxResourceState(barrier.after);

		commandList->ResourceBarrier(1, &b);
	}

	rhi::CommandBufferHandle DxRHI::getCurrentCommandBuffer() const
	{
		return frameResources_[currentFrame_].commandBuffer;
	}

	void DxRHI::freeImage(rhi::TextureHandle handle)
	{
		DxImage& image = images_[handle];

		if (image.imported) return;

		// The resource has to go before the memory does: leaving it alive while the block is
		// handed to the next allocation would alias two resources onto the same heap range.
		image.resource.Reset();

		memoryAllocator_[(u32)image.desc.memoryType].free(image.alloc);
		image.alloc = {};
	}

	void DxRHI::freeBuffer(rhi::BufferHandle handle)
	{
		DxBuffer& buffer = buffers_[handle];

		if (buffer.imported) return;

		buffer.resource.Reset();

		memoryAllocator_[(u32)buffer.desc.memoryType].free(buffer.alloc);
		buffer.alloc = {};
	}

	void DxRHI::releaseImage(rhi::TextureHandle handle)
	{
		DxImage& image = images_[handle];

		if (image.imported) return;

		freeImageList_.push_back(handle);
	}

	void DxRHI::releaseBuffer(rhi::BufferHandle handle)
	{
		DxBuffer& buffer = buffers_[handle];

		if (buffer.imported) return;

		freeBufferList_.push_back(handle);
	}
}

namespace mv::rhi
{
	std::shared_ptr<IRHI> IRHI::createDx12RHI()
	{
		return std::make_shared<backend::dx12_0::DxRHI>();
	}
}


