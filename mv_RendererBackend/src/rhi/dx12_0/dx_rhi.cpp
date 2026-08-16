
#include "rhi/dx12_0/dx_rhi.h"

namespace mv::backend::dx12_0
{
	void DxRHI::initialize(void* hwnd)
	{
		device_.initialize();
		swapchain_.initialize(&device_, hwnd);

		commandPool_[(u32)rhi::EQueueType::eGraphics].initialize(&device_, D3D12_COMMAND_LIST_TYPE_DIRECT);
		commandPool_[(u32)rhi::EQueueType::eCompute].initialize(&device_, D3D12_COMMAND_LIST_TYPE_COMPUTE);
		commandPool_[(u32)rhi::EQueueType::eTransfer].initialize(&device_, D3D12_COMMAND_LIST_TYPE_COPY);

		for (u32 i = 0; i < (u32)rhi::EMemoryType::eNum; i++)
		{
			memoryAllocator_[i].initialize(&device_, 128 * 1024, (rhi::EMemoryType)i);
		}

		frameResources_.resize((size_t)framesInFlight_);
		for (u32 i = 0; i < framesInFlight_; i++)
		{
			frameResources_[i].initialize(&device_, &commandPool_[(u32)rhi::EQueueType::eGraphics]);
		}

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

		//for (u32 i = 0; i < (u32)rhi::EMemoryType::eNum; i++)
		//{
		//	memoryAllocator_[i].deinitialize();
		//}
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

		if (frameResource.inFlightFence->GetCompletedValue() != frameResource.fenceValue)
		{
			HANDLE fenceEvent = CreateEvent(NULL, FALSE, FALSE, NULL);

			frameResource.inFlightFence->SetEventOnCompletion(frameResource.fenceValue, fenceEvent);

			WaitForSingleObject(fenceEvent, INFINITE);
		}

		ID3D12CommandAllocator* allocator = commandPool_[(u32)rhi::EQueueType::eGraphics].allocator();
		allocator->Reset();

		ID3D12GraphicsCommandList* cmd = commandPool_[(u32)rhi::EQueueType::eGraphics].getCommandList(frameResource.commandBuffer).commandList();
		cmd->Reset(allocator, nullptr);

		swapchain_.acquireNextImage();

		context.backbuffer = backbuffers_[swapchain_.imageIndex()];
		context.cmd = frameResource.commandBuffer;

		frameResource.backbuffer = backbuffers_[swapchain_.imageIndex()];

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

	void DxRHI::clearRenderTarget(float clearColor[])
	{
		DxFrameResource& frameResource = frameResources_[currentFrame_];
		ID3D12GraphicsCommandList* commandBuffer = commandPool_[(u32)rhi::EQueueType::eGraphics].getCommandList(frameResource.commandBuffer).commandList();

		float clearValue[] =
		{
			clearColor[0], clearColor[1], clearColor[2], clearColor[3],
		};

		DxImage& backbuffer = images_[frameResource.backbuffer];

		commandBuffer->ClearRenderTargetView(backbuffer.cpu, clearValue, 0, nullptr);
	}

	rhi::CommandBufferHandle DxRHI::allocateCommandBuffer(rhi::EQueueType queueType)
	{
		return commandPool_[(u32)queueType].allocate();
	}

	void DxRHI::createBackbuffer()
	{
		std::vector<rhi::TextureHandle> backbuffers;
		for (u32 i = 0; i < swapchain_.imageCount(); i++)
		{
			rhi::TextureHandle handle = (rhi::TextureHandle)images_.size();
			wrl::ComPtr<ID3D12Resource> backbuffer;

			swapchain_.swapchain()->GetBuffer(i, IID_PPV_ARGS(&backbuffer));

			images_.emplace_back(rhi::TextureDesc{}, backbuffer, Allocation(), true);

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
		wrl::ComPtr<ID3D12Resource> resource;
		Allocation alloc{};

		D3D12_RESOURCE_DESC resdesc{};

		//if ((desc.usage & rhi::EBufferUsage::eVertex) == rhi::EBufferUsage::eVertex) bufferInfo.usage |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
		//if ((desc.usage & rhi::EBufferUsage::eIndex) == rhi::EBufferUsage::eIndex) bufferInfo.usage |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
		//if ((desc.usage & rhi::EBufferUsage::eUniform) == rhi::EBufferUsage::eUniform) bufferInfo.usage |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
		//if ((desc.usage & rhi::EBufferUsage::eStorage) == rhi::EBufferUsage::eStorage) bufferInfo.usage |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
		//if ((desc.usage & rhi::EBufferUsage::eIndirectArgs) == rhi::EBufferUsage::eIndirectArgs) bufferInfo.usage |= VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
		//if ((desc.usage & rhi::EBufferUsage::eTransferSrc) == rhi::EBufferUsage::eTransferSrc) bufferInfo.usage |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
		//if ((desc.usage & rhi::EBufferUsage::eTransferDst) == rhi::EBufferUsage::eTransferDst) bufferInfo.usage |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;

		//if (vkCreateBuffer(device_.device(), &bufferInfo, nullptr, &buffer) != VK_SUCCESS)
		//{
		//	throw std::exception("Failed to create buffer");
		//}

		alloc = memoryAllocator_[(u32)desc.memoryType].allocate(resource);

		vkBindBufferMemory(device_.device(), buffer, alloc.memory, alloc.offset);

		buffers_.emplace_back(desc, buffer, alloc, false);
		return handle;
	}
	rhi::TextureHandle VulkanRHI::createTexture(const rhi::TextureDesc& desc)
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
		VkImage image = VK_NULL_HANDLE;
		VkImageView view = VK_NULL_HANDLE;
		Allocation alloc{};

		VkImageCreateInfo imageInfo{};
		imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		imageInfo.imageType = VK_IMAGE_TYPE_2D;
		imageInfo.extent.width = desc.width;
		imageInfo.extent.height = desc.height;
		imageInfo.extent.depth = desc.depth;
		imageInfo.mipLevels = 1;
		imageInfo.arrayLayers = 1;
		imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
		imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
		imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		imageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
		imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
		imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

		if (vkCreateImage(device_.device(), &imageInfo, nullptr, &image) != VK_SUCCESS)
		{
			throw std::exception("Failed to create image");
		}

		alloc = memoryAllocator_[(u32)desc.memoryType].allocate(image);

		vkBindImageMemory(device_.device(), image, alloc.memory, alloc.offset);

		VkImageViewCreateInfo viewInfo{};
		viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		viewInfo.image = image;
		// ToDo: Support different view types
		viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
		// ToDo: Support different formats
		viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
		viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		// ToDo: Support different mip levels and array layers
		viewInfo.subresourceRange.baseMipLevel = 0;
		viewInfo.subresourceRange.levelCount = 1;
		viewInfo.subresourceRange.baseArrayLayer = 0;
		viewInfo.subresourceRange.layerCount = 1;

		if (vkCreateImageView(device_.device(), &viewInfo, nullptr, &view) != VK_SUCCESS)
		{
			throw std::exception("Failed to create image view");
		}
		images_.emplace_back(desc, image, view, alloc, false);
		return handle;
	}


	void VulkanRHI::textureBarrier(rhi::CommandBufferHandle cmd, const rhi::TextureBarrier& barrier)
	{
		VulkanCommandBuffer& commandBuffer = commandPool_[(u32)rhi::EQueueType::eGraphics].getCommandBuffer(cmd);

		commandBuffer.pipelineBarrier(
			VulkanStateInfo{ getPipelineStageFlags(barrier.before), getAccessFlags(barrier.before), getImageLayout(barrier.before) },
			VulkanStateInfo{ getPipelineStageFlags(barrier.after), getAccessFlags(barrier.after), getImageLayout(barrier.after) },
			images_[barrier.texture]);
	}

	void VulkanRHI::bufferBarrier(rhi::CommandBufferHandle cmd, const rhi::BufferBarrier& barrier)
	{
		const VulkanCommandBuffer& commandBuffer = commandPool_[(u32)rhi::EQueueType::eGraphics].getCommandBuffer(cmd);
	}

	rhi::CommandBufferHandle VulkanRHI::getCurrentCommandBuffer() const
	{
		return frameResources_[currentFrame_].commandBuffer;
	}

	void VulkanRHI::freeImage(rhi::TextureHandle handle)
	{
		VulkanImage& image = images_[handle];

		if (image.imported) return;

		memoryAllocator_[(u32)image.desc.memoryType].free(image.alloc);
	}

	void VulkanRHI::freeBuffer(rhi::BufferHandle handle)
	{
		VulkanBuffer& buffer = buffers_[handle];

		if (buffer.imported) return;

		memoryAllocator_[(u32)buffer.desc.memoryType].free(buffer.alloc);
	}

	void VulkanRHI::releaseImage(rhi::TextureHandle handle)
	{
		VulkanImage& image = images_[handle];

		if (image.imported) return;

		freeImageList_.push_back(handle);
	}

	void VulkanRHI::releaseBuffer(rhi::BufferHandle handle)
	{
		VulkanBuffer& buffer = buffers_[handle];

		if (buffer.imported) return;

		freeBufferList_.push_back(handle);
	}

}