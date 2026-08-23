#include "rhi/vk1_4/vulkan_rhi.h"

namespace mv::backend::vk1_4
{


	void VulkanRHI::initialize(void* hwnd)
	{
		device_.initialize();
		swapchain_.initialize(&device_, hwnd);

		for (u32 i = 0; i < (u32)rhi::EMemoryType::eNum; i++)
		{
			memoryAllocator_[i].initialize(&device_, 64ull * 1024 * 1024, (rhi::EMemoryType)i);
		}

		shaderManager_.initialize(&device_);
		layoutManager_.initialize(&device_);
		pipelineManager_.initialize(&device_, &shaderManager_, &layoutManager_);

		// One frame slot that is never reset: bind groups created here stay valid until
		// shutdown. Per-frame transient sets would use a slot per frame in flight.
		descriptorAllocator_.initialize(&device_, 1);


		commandPool_[(u32)rhi::EQueueType::eGraphics].initialize(&device_, device_.graphicsQueueFamilyIndex());
		commandPool_[(u32)rhi::EQueueType::eCompute].initialize(&device_, device_.graphicsQueueFamilyIndex());
		commandPool_[(u32)rhi::EQueueType::eTransfer].initialize(&device_, device_.graphicsQueueFamilyIndex());

		frameResources_.resize((size_t)framesInFlight_);
		for (u32 i = 0; i < framesInFlight_; i++)
		{
			frameResources_[i].initialize(&device_, &commandPool_[(u32)rhi::EQueueType::eGraphics]);
		}

		createBackbuffer();
	}

	void VulkanRHI::deinitialize()
	{
		for (u32 i = 0; i < framesInFlight_; i++)
		{
			vkWaitForFences(device_.device(), 1, &frameResources_[i].inFlightFence, VK_TRUE, UINT64_MAX);
		}
		device_.waitIdle();
	
		for (u32 i = 0; i < framesInFlight_; i++)
		{
			frameResources_[i].deinitialize(&device_, &commandPool_[(u32)rhi::EQueueType::eGraphics]);
		}

		commandPool_[(u32)rhi::EQueueType::eGraphics].deinitialize();
		commandPool_[(u32)rhi::EQueueType::eCompute].deinitialize();
		commandPool_[(u32)rhi::EQueueType::eTransfer].deinitialize();

		for (auto& entry : samplerCache_)
		{
			vkDestroySampler(device_.device(), entry.second, nullptr);
		}
		samplerCache_.clear();

		for (auto& entry : imageViewCache_)
		{
			vkDestroyImageView(device_.device(), entry.second, nullptr);
		}
		imageViewCache_.clear();

		descriptorAllocator_.deinitialize();

		shaderManager_.deinitialize();
		layoutManager_.deinitialize();
		pipelineManager_.deinitialize();

		for (u32 i = 0; i < (u32)rhi::EMemoryType::eNum; i++)
		{
			memoryAllocator_[i].deinitialize();
		}
		swapchain_.deinitialize();

		device_.deinitialize();
	}

	void VulkanRHI::waitIdle()
	{
		device_.waitIdle();
	}

	rhi::FrameContext VulkanRHI::beginFrame()
	{
		rhi::FrameContext context;
		currentFrame_ = (currentFrame_+ 1) % framesInFlight_;
		
		context.currentFrameIndex = currentFrame_;

		VulkanFrameResource& frameResource = frameResources_[currentFrame_];

		vkWaitForFences(device_.device(), 1, &frameResource.inFlightFence, VK_TRUE, UINT64_MAX);

		vkResetFences(device_.device(), 1, &frameResource.inFlightFence);

		VkCommandBufferBeginInfo beginInfo{};
		beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		vkBeginCommandBuffer(commandPool_[(u32)rhi::EQueueType::eGraphics].getCommandBuffer(frameResource.commandBuffer).commandBuffer(), &beginInfo);

		swapchain_.acquireNextImage(frameResource.imageAvailableSemaphore);

		context.backbuffer = backbuffers_[swapchain_.imageIndex()];
		context.cmd = frameResource.commandBuffer;

		frameResource.backbuffer = backbuffers_[swapchain_.imageIndex()];

		return context;
	}

	void VulkanRHI::endFrame()
	{
		VulkanFrameResource& frameResource = frameResources_[currentFrame_];

		VulkanCommandBuffer& commandBuffer = commandPool_[(u32)rhi::EQueueType::eGraphics].getCommandBuffer(frameResource.commandBuffer);
		VkCommandBuffer cmd = commandBuffer.commandBuffer();

		rhi::TextureBarrier barrier
		{
			.texture = frameResource.backbuffer,
			.before = rhi::EResourceState::eColorAttachment,
			.after = rhi::EResourceState::ePresent,
		};

		textureBarrier(frameResource.commandBuffer, barrier);

		vkEndCommandBuffer(cmd);
		
		VkSemaphore waitSemaphores[] = { frameResource.imageAvailableSemaphore };
		VkSemaphore signalSemaphores[] = { swapchain_.renderFinishedSemaphore() };
		VkCommandBuffer commandBuffers[] = { cmd };
		VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
		VkSubmitInfo submitInfo{};
		submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submitInfo.waitSemaphoreCount = 1;
		submitInfo.pWaitSemaphores = waitSemaphores;
		submitInfo.pWaitDstStageMask = waitStages;
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = commandBuffers;
		submitInfo.signalSemaphoreCount = 1;
		submitInfo.pSignalSemaphores = signalSemaphores;
		if (vkQueueSubmit(device_.graphicsQueue(), 1, &submitInfo, frameResource.inFlightFence) != VK_SUCCESS)
		{
			throw std::exception("Failed to submit draw command buffer");
		}

		swapchain_.present(device_.graphicsQueue());
	}

	void VulkanRHI::beginRenderPass(rhi::CommandBufferHandle cmd, const rhi::RenderPassDesc& desc)
	{
		VkCommandBuffer commandBuffer = commandPool_[(u32)rhi::EQueueType::eGraphics].getCommandBuffer(cmd).commandBuffer();

		std::vector<VkRenderingAttachmentInfo> attachments;
		for (const auto& target : desc.colorTargets)
		{
			VkClearValue clearValue{};
			clearValue.color.float32[0] = target.clearColor[0];
			clearValue.color.float32[1] = target.clearColor[1];
			clearValue.color.float32[2] = target.clearColor[2];
			clearValue.color.float32[3] = target.clearColor[3];

			VkRenderingAttachmentInfo attachment{};
			attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
			attachment.imageView = images_[target.texture].view;
			attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			attachment.loadOp = target.clear ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
			attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
			attachment.clearValue = clearValue;

			attachments.push_back(attachment);
		}

		u32 width = 0;
		u32 height = 0;
		if (!desc.colorTargets.empty())
		{
			const rhi::TextureDesc& textureDesc = images_[desc.colorTargets[0].texture].desc;
			width = textureDesc.width;
			height = textureDesc.height;
		}

		VkRenderingAttachmentInfo depthAttachment{};
		const bool hasDepth = (desc.depthTarget.texture != INVALID_HANDLE);
		if (hasDepth)
		{
			depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
			depthAttachment.imageView = images_[desc.depthTarget.texture].view;
			depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
			depthAttachment.loadOp = desc.depthTarget.clear ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
			depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
			depthAttachment.clearValue.depthStencil.depth = desc.depthTarget.clearDepth;

			if (width == 0 || height == 0)
			{
				const rhi::TextureDesc& depthDesc = images_[desc.depthTarget.texture].desc;
				width = depthDesc.width;
				height = depthDesc.height;
			}
		}

		VkRenderingInfo info{};
		info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
		info.colorAttachmentCount = (u32)attachments.size();
		info.pColorAttachments = attachments.data();
		info.pDepthAttachment = hasDepth ? &depthAttachment : nullptr;
		info.layerCount = 1;
		info.renderArea = { { 0, 0 }, { width, height } };
		info.viewMask = 0;

		vkCmdBeginRendering(commandBuffer, &info);
	}

	void VulkanRHI::endRenderPass(rhi::CommandBufferHandle cmd)
	{
		vkCmdEndRendering(commandPool_[(u32)rhi::EQueueType::eGraphics].getCommandBuffer(cmd).commandBuffer());
	}

	void VulkanRHI::bindGraphicsPipeline(rhi::CommandBufferHandle cmd, rhi::PipelineHandle pipeline)
	{
		VkCommandBuffer commandBuffer = commandPool_[(u32)rhi::EQueueType::eGraphics].getCommandBuffer(cmd).commandBuffer();

		vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineManager_.pipeline(pipeline).pipeline());
	}

	void VulkanRHI::setViewport(rhi::CommandBufferHandle cmd, f32 x, f32 y, f32 width, f32 height)
	{
		VkCommandBuffer commandBuffer = commandPool_[(u32)rhi::EQueueType::eGraphics].getCommandBuffer(cmd).commandBuffer();

		// Vulkan's clip space has +Y pointing down, D3D's points up. Flipping the viewport
		// (origin at the bottom, negative height) puts both backends in the same coordinate
		// system, so identical shaders and winding order produce identical images.
		VkViewport viewport{};
		viewport.x = x;
		viewport.y = y + height;
		viewport.width = width;
		viewport.height = -height;
		viewport.minDepth = 0.0f;
		viewport.maxDepth = 1.0f;

		vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
	}

	void VulkanRHI::setScissor(rhi::CommandBufferHandle cmd, s32 x, s32 y, u32 width, u32 height)
	{
		VkCommandBuffer commandBuffer = commandPool_[(u32)rhi::EQueueType::eGraphics].getCommandBuffer(cmd).commandBuffer();

		VkRect2D scissor{};
		scissor.offset = { x, y };
		scissor.extent = { width, height };

		vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
	}

	void VulkanRHI::draw(rhi::CommandBufferHandle cmd, u32 vertexCount, u32 instanceCount, u32 firstVertex, u32 firstInstance)
	{
		VkCommandBuffer commandBuffer = commandPool_[(u32)rhi::EQueueType::eGraphics].getCommandBuffer(cmd).commandBuffer();

		vkCmdDraw(commandBuffer, vertexCount, instanceCount, firstVertex, firstInstance);
	}

	rhi::ETextureFormat VulkanRHI::backbufferFormat() const
	{
		return fromVkFormat(swapchain_.format());
	}

	void VulkanRHI::bindVertexBuffer(rhi::CommandBufferHandle cmd, u32 slot, rhi::BufferHandle buffer, u32 stride, u64 offset)
	{
		VkCommandBuffer commandBuffer = commandPool_[(u32)rhi::EQueueType::eGraphics].getCommandBuffer(cmd).commandBuffer();

		// Vulkan takes the stride from the pipeline's vertex input state, not from the bind.
		VkBuffer buffers[] = { buffers_[buffer].buffer };
		VkDeviceSize offsets[] = { offset };

		vkCmdBindVertexBuffers(commandBuffer, slot, 1, buffers, offsets);
	}

	void VulkanRHI::bindIndexBuffer(rhi::CommandBufferHandle cmd, rhi::BufferHandle buffer, rhi::EIndexFormat format, u64 offset)
	{
		VkCommandBuffer commandBuffer = commandPool_[(u32)rhi::EQueueType::eGraphics].getCommandBuffer(cmd).commandBuffer();

		const VkIndexType indexType = (format == rhi::EIndexFormat::eUint16) ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32;

		vkCmdBindIndexBuffer(commandBuffer, buffers_[buffer].buffer, offset, indexType);
	}

	void VulkanRHI::drawIndexed(rhi::CommandBufferHandle cmd, u32 indexCount, u32 instanceCount, u32 firstIndex, s32 vertexOffset, u32 firstInstance)
	{
		VkCommandBuffer commandBuffer = commandPool_[(u32)rhi::EQueueType::eGraphics].getCommandBuffer(cmd).commandBuffer();

		vkCmdDrawIndexed(commandBuffer, indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
	}

	void* VulkanRHI::mapBuffer(rhi::BufferHandle handle)
	{
		VulkanBuffer& buffer = buffers_[handle];

		return memoryAllocator_[(u32)buffer.desc.memoryType].map(buffer.alloc);
	}

	void VulkanRHI::unmapBuffer(rhi::BufferHandle handle)
	{
		// Host-visible pools stay mapped for their lifetime; nothing to release per buffer.
	}

	void VulkanRHI::writeBuffer(rhi::BufferHandle handle, const void* data, u64 size, u64 offset)
	{
		u8* dst = static_cast<u8*>(mapBuffer(handle));
		if (!dst)
		{
			throw std::exception("writeBuffer on a buffer that is not host visible");
		}

		memcpy(dst + offset, data, (size_t)size);
	}

	VkCommandBuffer VulkanRHI::beginOneShotCommands()
	{
		VkCommandBufferAllocateInfo allocInfo{};
		allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		allocInfo.commandPool = commandPool_[(u32)rhi::EQueueType::eGraphics].commandPool();
		allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		allocInfo.commandBufferCount = 1;

		VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
		vkAllocateCommandBuffers(device_.device(), &allocInfo, &commandBuffer);

		VkCommandBufferBeginInfo beginInfo{};
		beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		vkBeginCommandBuffer(commandBuffer, &beginInfo);

		return commandBuffer;
	}

	void VulkanRHI::endOneShotCommands(VkCommandBuffer commandBuffer)
	{
		vkEndCommandBuffer(commandBuffer);

		VkSubmitInfo submitInfo{};
		submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &commandBuffer;

		vkQueueSubmit(device_.graphicsQueue(), 1, &submitInfo, VK_NULL_HANDLE);
		vkQueueWaitIdle(device_.graphicsQueue());

		vkFreeCommandBuffers(device_.device(), commandPool_[(u32)rhi::EQueueType::eGraphics].commandPool(), 1, &commandBuffer);
	}

	void VulkanRHI::uploadBuffer(rhi::BufferHandle handle, const void* data, u64 size)
	{
		rhi::BufferDesc stagingDesc{};
		stagingDesc.size = size;
		stagingDesc.usage = rhi::EBufferUsage::eTransferSrc;
		stagingDesc.memoryType = rhi::EMemoryType::eHostVisibleBuffer;

		const rhi::BufferHandle staging = createBuffer(stagingDesc);
		writeBuffer(staging, data, size, 0);

		// The copy is submitted and waited on right here, so the staging buffer is provably
		// free by the time this returns.
		VkCommandBuffer commandBuffer = beginOneShotCommands();

		VkBufferCopy region{};
		region.size = size;
		vkCmdCopyBuffer(commandBuffer, buffers_[staging].buffer, buffers_[handle].buffer, 1, &region);

		// Waiting on the queue orders execution but says nothing about visibility, so the
		// transfer writes still need to be made available to later reads.
		VkMemoryBarrier barrier{};
		barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
		barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;

		vkCmdPipelineBarrier(
			commandBuffer,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
			0,
			1, &barrier,
			0, nullptr,
			0, nullptr);

		endOneShotCommands(commandBuffer);

		releaseBuffer(staging);
	}

	void VulkanRHI::uploadTexture(rhi::TextureHandle handle, const rhi::TextureUpload* levels, u32 levelCount)
	{
		const rhi::TextureDesc& textureDesc = images_[handle].desc;
		if (levelCount == 0) return;

		// Every level goes into one staging buffer so the whole texture costs a single
		// submit; a submit per level would mean thousands of queue waits for a scene.
		u64 totalSize = 0;
		for (u32 level = 0; level < levelCount; level++)
		{
			totalSize += levels[level].size;
		}

		rhi::BufferDesc stagingDesc{};
		stagingDesc.size = totalSize;
		stagingDesc.usage = rhi::EBufferUsage::eTransferSrc;
		stagingDesc.memoryType = rhi::EMemoryType::eHostVisibleBuffer;

		const rhi::BufferHandle staging = createBuffer(stagingDesc);

		std::vector<VkBufferImageCopy> regions;
		regions.reserve(levelCount);

		u64 offset = 0;
		for (u32 level = 0; level < levelCount; level++)
		{
			writeBuffer(staging, levels[level].data, levels[level].size, offset);

			VkBufferImageCopy region{};
			region.bufferOffset = offset;
			region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			region.imageSubresource.mipLevel = level;
			region.imageSubresource.layerCount = 1;
			region.imageExtent =
			{
				(textureDesc.width >> level) ? (textureDesc.width >> level) : 1,
				(textureDesc.height >> level) ? (textureDesc.height >> level) : 1,
				1,
			};

			regions.push_back(region);

			offset += levels[level].size;
		}

		VkCommandBuffer commandBuffer = beginOneShotCommands();

		VkImageMemoryBarrier barrier{};
		barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		barrier.image = images_[handle].image;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		barrier.subresourceRange.levelCount = textureDesc.mipLevels;
		barrier.subresourceRange.layerCount = 1;

		barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		barrier.srcAccessMask = 0;
		barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

		vkCmdPipelineBarrier(
			commandBuffer,
			VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
			0, 0, nullptr, 0, nullptr, 1, &barrier);

		vkCmdCopyBufferToImage(
			commandBuffer,
			buffers_[staging].buffer,
			images_[handle].image,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			(u32)regions.size(), regions.data());

		barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

		vkCmdPipelineBarrier(
			commandBuffer,
			VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			0, 0, nullptr, 0, nullptr, 1, &barrier);

		endOneShotCommands(commandBuffer);

		releaseBuffer(staging);
	}

	VkSampler VulkanRHI::getSampler(const rhi::SamplerDesc& desc)
	{
		for (const auto& entry : samplerCache_)
		{
			if (entry.first.filter == desc.filter &&
				entry.first.address == desc.address &&
				entry.first.maxAnisotropy == desc.maxAnisotropy)
			{
				return entry.second;
			}
		}

		const VkFilter filter = (desc.filter == rhi::EFilterMode::eNearest) ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
		const VkSamplerAddressMode address = (desc.address == rhi::EAddressMode::eClampToEdge)
			? VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE
			: VK_SAMPLER_ADDRESS_MODE_REPEAT;

		VkSamplerCreateInfo ci{};
		ci.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
		ci.magFilter = filter;
		ci.minFilter = filter;
		ci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
		ci.addressModeU = address;
		ci.addressModeV = address;
		ci.addressModeW = address;
		// Unclamped so sampling can reach the smallest level the texture actually has.
		ci.maxLod = VK_LOD_CLAMP_NONE;

		const f32 anisotropy = (f32)desc.maxAnisotropy;
		if (anisotropy > 1.0f && device_.maxSamplerAnisotropy() > 1.0f)
		{
			ci.anisotropyEnable = VK_TRUE;
			ci.maxAnisotropy = (anisotropy < device_.maxSamplerAnisotropy()) ? anisotropy : device_.maxSamplerAnisotropy();
		}

		VkSampler sampler = VK_NULL_HANDLE;
		vkCreateSampler(device_.device(), &ci, nullptr, &sampler);

		samplerCache_.emplace_back(desc, sampler);

		return sampler;
	}

	rhi::BindGroupHandle VulkanRHI::createBindGroup(const rhi::BindGroupDesc& desc)
	{
		VulkanBindGroup group{};
		group.set = descriptorAllocator_.allocate(&layoutManager_.layout(desc.layout));

		// The info structs must stay alive until vkUpdateDescriptorSets returns, so they are
		// reserved up front: reallocation would leave the writes pointing at freed memory.
		std::vector<VkDescriptorBufferInfo> bufferInfos;
		std::vector<VkDescriptorImageInfo> imageInfos;
		bufferInfos.reserve(desc.uniformBuffers.size());
		imageInfos.reserve(desc.sampledTextures.size() + desc.samplers.size());

		std::vector<VkWriteDescriptorSet> writes;

		for (const auto& binding : desc.uniformBuffers)
		{
			VkDescriptorBufferInfo info{};
			info.buffer = buffers_[binding.buffer].buffer;
			info.offset = binding.offset;
			info.range = binding.range ? binding.range : VK_WHOLE_SIZE;
			bufferInfos.push_back(info);

			VkWriteDescriptorSet write{};
			write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			write.dstSet = group.set;
			write.dstBinding = binding.binding;
			write.descriptorCount = 1;
			write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			write.pBufferInfo = &bufferInfos.back();
			writes.push_back(write);
		}

		std::vector<VkDescriptorBufferInfo> storageInfos;
		storageInfos.reserve(desc.storageBuffers.size());

		for (const auto& binding : desc.storageBuffers)
		{
			VkDescriptorBufferInfo info{};
			info.buffer = buffers_[binding.buffer].buffer;
			info.offset = binding.offset;
			info.range = binding.count ? (VkDeviceSize)binding.stride * binding.count : VK_WHOLE_SIZE;
			storageInfos.push_back(info);

			VkWriteDescriptorSet write{};
			write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			write.dstSet = group.set;
			write.dstBinding = binding.binding;
			write.descriptorCount = 1;
			write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
			write.pBufferInfo = &storageInfos.back();
			writes.push_back(write);
		}

		for (const auto& binding : desc.sampledTextures)
		{
			VkDescriptorImageInfo info{};
			info.imageView = getImageView(binding.texture, binding.baseMip, binding.mipCount);
			info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			imageInfos.push_back(info);

			VkWriteDescriptorSet write{};
			write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			write.dstSet = group.set;
			write.dstBinding = binding.binding;
			write.dstArrayElement = binding.arrayIndex;
			write.descriptorCount = 1;
			write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
			write.pImageInfo = &imageInfos.back();
			writes.push_back(write);
		}

		for (const auto& binding : desc.samplers)
		{
			VkDescriptorImageInfo info{};
			info.sampler = getSampler(binding.sampler);
			imageInfos.push_back(info);

			VkWriteDescriptorSet write{};
			write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			write.dstSet = group.set;
			write.dstBinding = binding.binding;
			write.dstArrayElement = binding.arrayIndex;
			write.descriptorCount = 1;
			write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
			write.pImageInfo = &imageInfos.back();
			writes.push_back(write);
		}

		vkUpdateDescriptorSets(device_.device(), (u32)writes.size(), writes.data(), 0, nullptr);

		rhi::BindGroupHandle handle = (rhi::BindGroupHandle)bindGroups_.size();
		bindGroups_.push_back(group);

		return handle;
	}

	VkImageView VulkanRHI::getImageView(rhi::TextureHandle texture, u32 baseMip, u32 mipCount)
	{
		const VulkanImage& image = images_[texture];

		// A streaming system asks for the coarsest resident level, which for a small
		// texture can be past the end of a short chain. Clamping here keeps every caller
		// from having to know how many mips each texture happens to have.
		baseMip = std::min(baseMip, image.desc.mipLevels - 1);
		mipCount = std::min(mipCount ? mipCount : image.desc.mipLevels, image.desc.mipLevels - baseMip);

		// The whole chain is the view that was made alongside the image.
		if (baseMip == 0 && mipCount == image.desc.mipLevels)
			return image.view;

		const u32 levels = mipCount;

		for (const auto& entry : imageViewCache_)
		{
			if (entry.first.texture == texture && entry.first.baseMip == baseMip && entry.first.mipCount == levels)
				return entry.second;
		}

		VkImageViewCreateInfo viewInfo{};
		viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		viewInfo.image = image.image;
		viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
		viewInfo.format = toVkFormat(image.desc.format);
		viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		viewInfo.subresourceRange.baseMipLevel = baseMip;
		viewInfo.subresourceRange.levelCount = levels;
		viewInfo.subresourceRange.baseArrayLayer = 0;
		viewInfo.subresourceRange.layerCount = 1;

		VkImageView view = VK_NULL_HANDLE;
		vkCreateImageView(device_.device(), &viewInfo, nullptr, &view);

		imageViewCache_.push_back({ { texture, baseMip, levels }, view });

		return view;
	}

	void VulkanRHI::copyBuffer(rhi::CommandBufferHandle cmd, rhi::BufferHandle dst, rhi::BufferHandle src, u64 size)
	{
		VkCommandBuffer commandBuffer = commandPool_[(u32)rhi::EQueueType::eGraphics].getCommandBuffer(cmd).commandBuffer();

		// The source is typically a buffer a shader has just written, so the copy has to be
		// ordered after those writes.
		VkMemoryBarrier barrier{};
		barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
		barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

		vkCmdPipelineBarrier(
			commandBuffer,
			VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
			0, 1, &barrier, 0, nullptr, 0, nullptr);

		VkBufferCopy region{};
		region.size = size;

		vkCmdCopyBuffer(commandBuffer, buffers_[src].buffer, buffers_[dst].buffer, 1, &region);
	}

	void VulkanRHI::updateBindGroupTexture(rhi::BindGroupHandle group, u32 binding, u32 arrayIndex, rhi::TextureHandle texture, u32 baseMip, u32 mipCount)
	{
		VkDescriptorImageInfo info{};
		info.imageView = getImageView(texture, baseMip, mipCount);
		info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		VkWriteDescriptorSet write{};
		write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		write.dstSet = bindGroups_[group].set;
		write.dstBinding = binding;
		write.dstArrayElement = arrayIndex;
		write.descriptorCount = 1;
		write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
		write.pImageInfo = &info;

		vkUpdateDescriptorSets(device_.device(), 1, &write, 0, nullptr);
	}

	void VulkanRHI::pushConstants(rhi::CommandBufferHandle cmd, rhi::PipelineLayoutHandle layout, const void* data, u32 size, u32 offset)
	{
		VkCommandBuffer commandBuffer = commandPool_[(u32)rhi::EQueueType::eGraphics].getCommandBuffer(cmd).commandBuffer();

		vkCmdPushConstants(
			commandBuffer,
			pipelineManager_.layout(layout).layout(),
			VK_SHADER_STAGE_ALL_GRAPHICS,
			offset,
			size,
			data);
	}

	void VulkanRHI::bindBindGroup(rhi::CommandBufferHandle cmd, rhi::PipelineLayoutHandle layout, u32 setIndex, rhi::BindGroupHandle group)
	{
		VkCommandBuffer commandBuffer = commandPool_[(u32)rhi::EQueueType::eGraphics].getCommandBuffer(cmd).commandBuffer();

		VkDescriptorSet sets[] = { bindGroups_[group].set };

		vkCmdBindDescriptorSets(
			commandBuffer,
			VK_PIPELINE_BIND_POINT_GRAPHICS,
			pipelineManager_.layout(layout).layout(),
			setIndex,
			1, sets,
			0, nullptr);
	}

	rhi::CommandBufferHandle VulkanRHI::allocateCommandBuffer(rhi::EQueueType queueType)
	{
		return commandPool_[(u32)queueType].allocate();
	}

	void VulkanRHI::createBackbuffer()
	{
		// The desc is what beginRenderPass derives its render area from, so imported
		// backbuffers have to describe themselves like any other texture.
		rhi::TextureDesc backbufferDesc{};
		backbufferDesc.width = swapchain_.extent().width;
		backbufferDesc.height = swapchain_.extent().height;
		backbufferDesc.depth = 1;
		backbufferDesc.usage = rhi::ETextureUsage::eColorAttachment;
		backbufferDesc.format = fromVkFormat(swapchain_.format());

		std::vector<rhi::TextureHandle> backbuffers;
		for (u32 i = 0; i < swapchain_.imageCount(); i++)
		{
			rhi::TextureHandle handle = (rhi::TextureHandle)images_.size();
			VkImage image = swapchain_.images()[i];
			VkImageView view = swapchain_.views()[i];

			images_.emplace_back(backbufferDesc, image, view, Allocation(), true);

			backbuffers.push_back(handle);
		}

		backbuffers_ = backbuffers;
	}

	rhi::BufferHandle VulkanRHI::createBuffer(const rhi::BufferDesc& desc)
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
		VkBuffer buffer = VK_NULL_HANDLE;
		Allocation alloc{};

		VkBufferCreateInfo bufferInfo{};
		bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		bufferInfo.size = desc.size;
		if ((desc.usage & rhi::EBufferUsage::eVertex) == rhi::EBufferUsage::eVertex) bufferInfo.usage |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
		if ((desc.usage & rhi::EBufferUsage::eIndex) == rhi::EBufferUsage::eIndex) bufferInfo.usage |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
		if ((desc.usage & rhi::EBufferUsage::eUniform) == rhi::EBufferUsage::eUniform) bufferInfo.usage |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
		if ((desc.usage & rhi::EBufferUsage::eStorage) == rhi::EBufferUsage::eStorage) bufferInfo.usage |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
		if ((desc.usage & rhi::EBufferUsage::eIndirectArgs) == rhi::EBufferUsage::eIndirectArgs) bufferInfo.usage |= VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
		if ((desc.usage & rhi::EBufferUsage::eTransferSrc) == rhi::EBufferUsage::eTransferSrc) bufferInfo.usage |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
		if ((desc.usage & rhi::EBufferUsage::eTransferDst) == rhi::EBufferUsage::eTransferDst) bufferInfo.usage |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
		// Vulkan makes no distinction between a read-only and a writable storage buffer.
		if ((desc.usage & rhi::EBufferUsage::eStorageReadWrite) == rhi::EBufferUsage::eStorageReadWrite) bufferInfo.usage |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;

		if (vkCreateBuffer(device_.device(), &bufferInfo, nullptr, &buffer) != VK_SUCCESS)
		{
			throw std::exception("Failed to create buffer");
		}

		alloc = memoryAllocator_[(u32)desc.memoryType].allocate(buffer);

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
		const u32 mipLevels = desc.mipLevels ? desc.mipLevels : rhi::mipLevelsFor(desc.width, desc.height);

		imageInfo.mipLevels = mipLevels;
		imageInfo.arrayLayers = 1;
		imageInfo.format = toVkFormat(desc.format);
		imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
		imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
		imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

		imageInfo.usage = 0;
		if ((desc.usage & rhi::ETextureUsage::eSampled) == rhi::ETextureUsage::eSampled) imageInfo.usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
		if ((desc.usage & rhi::ETextureUsage::eStorage) == rhi::ETextureUsage::eStorage) imageInfo.usage |= VK_IMAGE_USAGE_STORAGE_BIT;
		if ((desc.usage & rhi::ETextureUsage::eColorAttachment) == rhi::ETextureUsage::eColorAttachment) imageInfo.usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
		if ((desc.usage & rhi::ETextureUsage::eDepthStencilAttachment) == rhi::ETextureUsage::eDepthStencilAttachment) imageInfo.usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
		if ((desc.usage & rhi::ETextureUsage::eTransferSrc) == rhi::ETextureUsage::eTransferSrc) imageInfo.usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
		if ((desc.usage & rhi::ETextureUsage::eTransferDst) == rhi::ETextureUsage::eTransferDst) imageInfo.usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;

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
		viewInfo.format = toVkFormat(desc.format);
		// A depth image's view must be created with the depth aspect, not colour.
		const bool isDepth = (desc.format == rhi::ETextureFormat::eD32_SFLOAT || desc.format == rhi::ETextureFormat::eD24_UNORM_S8_UINT);
		viewInfo.subresourceRange.aspectMask = isDepth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
		// The view has to span every level or sampling never leaves mip 0.
		viewInfo.subresourceRange.baseMipLevel = 0;
		viewInfo.subresourceRange.levelCount = mipLevels;
		viewInfo.subresourceRange.baseArrayLayer = 0;
		viewInfo.subresourceRange.layerCount = 1;

		if (vkCreateImageView(device_.device(), &viewInfo, nullptr, &view) != VK_SUCCESS)
		{
			throw std::exception("Failed to create image view");
		}

		// The stored desc carries the resolved level count so the upload path does not have
		// to recompute it.
		rhi::TextureDesc resolvedDesc = desc;
		resolvedDesc.mipLevels = mipLevels;

		images_.emplace_back(resolvedDesc, image, view, alloc, false);

		// Depth targets are moved into their attachment layout once and stay there, which
		// mirrors D3D12 creating them straight into DEPTH_WRITE. A render target that is
		// also sampled gets the same treatment for its shader-read resting layout, so the
		// render graph's per-frame barriers line up from the very first frame.
		const bool isSampledRenderTarget =
			((desc.usage & rhi::ETextureUsage::eColorAttachment) == rhi::ETextureUsage::eColorAttachment) &&
			((desc.usage & rhi::ETextureUsage::eSampled) == rhi::ETextureUsage::eSampled);

		if (isDepth || isSampledRenderTarget)
		{
			VkCommandBuffer commandBuffer = beginOneShotCommands();

			VkImageMemoryBarrier barrier{};
			barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			barrier.image = image;
			barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.subresourceRange.aspectMask = isDepth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
			barrier.subresourceRange.levelCount = mipLevels;
			barrier.subresourceRange.layerCount = 1;
			barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			barrier.newLayout = isDepth ? VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			barrier.srcAccessMask = 0;
			barrier.dstAccessMask = isDepth ? VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT : VK_ACCESS_SHADER_READ_BIT;

			vkCmdPipelineBarrier(
				commandBuffer,
				VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
				isDepth ? VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT : VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
				0, 0, nullptr, 0, nullptr, 1, &barrier);

			endOneShotCommands(commandBuffer);
		}

		return handle;
	}


	rhi::ShaderHandle VulkanRHI::createShader(const rhi::ShaderDesc& desc)
	{
		return shaderManager_.createShader(desc);
	}

	rhi::BindGroupLayoutHandle VulkanRHI::createBindGroupLayout(const rhi::BindGroupLayoutDesc& desc)
	{
		return layoutManager_.createBindGroupLayout(desc);
	}

	rhi::PipelineLayoutHandle VulkanRHI::createPipelineLayout(const rhi::PipelineLayoutDesc& desc)
	{
		return pipelineManager_.createPipelineLayout(desc);
	}

	rhi::PipelineHandle VulkanRHI::createGraphicsPipeline(const rhi::GraphicsPipelineDesc& desc)
	{
		return pipelineManager_.createPipeline(desc);
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

		// The object has to go before the memory does: leaving it alive while the block is
		// handed to the next allocation would alias two images onto the same memory.
		if (image.view != VK_NULL_HANDLE)
		{
			vkDestroyImageView(device_.device(), image.view, nullptr);
			image.view = VK_NULL_HANDLE;
		}
		if (image.image != VK_NULL_HANDLE)
		{
			vkDestroyImage(device_.device(), image.image, nullptr);
			image.image = VK_NULL_HANDLE;
		}

		memoryAllocator_[(u32)image.desc.memoryType].free(image.alloc);
		image.alloc = {};
	}

	void VulkanRHI::freeBuffer(rhi::BufferHandle handle)
	{
		VulkanBuffer& buffer = buffers_[handle];

		if (buffer.imported) return;

		if (buffer.buffer != VK_NULL_HANDLE)
		{
			vkDestroyBuffer(device_.device(), buffer.buffer, nullptr);
			buffer.buffer = VK_NULL_HANDLE;
		}

		memoryAllocator_[(u32)buffer.desc.memoryType].free(buffer.alloc);
		buffer.alloc = {};
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