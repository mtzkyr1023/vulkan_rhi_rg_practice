#ifndef _MV_VULKAN_RHI_H_
#define _MV_VULKAN_RHI_H_

#include "rhi/vk1_4/vulkan_device.h"
#include "rhi/vk1_4/vulkan_command.h"
#include "rhi/vk1_4/vulkan_resource.h"
#include "rhi/vk1_4/vulkan_descriptor.h"
#include "rhi/vk1_4/vulkan_swapchain.h"
#include "rhi/vk1_4/vulkan_memory.h"
#include "rhi/vk1_4/vulkan_pipeline.h"
#include "rhi/vk1_4/vulkan_frame_resource.h"

#include "rhi/rhi.h"

namespace mv
{
	namespace backend
	{
		namespace vk1_4
		{
			class VulkanRHI : public rhi::IRHI
			{
			public:
				void initialize(void* hwnd) override;
				void deinitialize() override;

				void waitIdle() override;

				rhi::FrameContext beginFrame() override;
				void endFrame() override;

				void beginRenderPass(rhi::CommandBufferHandle cmd, const rhi::RenderPassDesc& desc) override;
				void endRenderPass(rhi::CommandBufferHandle cmd) override;

				void bindGraphicsPipeline(rhi::CommandBufferHandle cmd, rhi::PipelineHandle pipeline) override;

				void setViewport(rhi::CommandBufferHandle cmd, f32 x, f32 y, f32 width, f32 height) override;
				void setScissor(rhi::CommandBufferHandle cmd, s32 x, s32 y, u32 width, u32 height) override;

				void draw(rhi::CommandBufferHandle cmd, u32 vertexCount, u32 instanceCount, u32 firstVertex, u32 firstInstance) override;

				rhi::ETextureFormat backbufferFormat() const override;

				rhi::CommandBufferHandle allocateCommandBuffer(rhi::EQueueType queueType) override;

				rhi::BufferHandle createBuffer(const rhi::BufferDesc& desc) override;
				rhi::TextureHandle createTexture(const rhi::TextureDesc& desc) override;

				rhi::ShaderHandle createShader(const rhi::ShaderDesc& desc) override;
				rhi::BindGroupLayoutHandle createBindGroupLayout(const rhi::BindGroupLayoutDesc& desc) override;
				rhi::BindGroupHandle createBindGroup(const rhi::BindGroupDesc& desc) override;
				rhi::PipelineLayoutHandle createPipelineLayout(const rhi::PipelineLayoutDesc& desc) override;
				rhi::PipelineHandle createGraphicsPipeline(const rhi::GraphicsPipelineDesc& desc) override;

				void* mapBuffer(rhi::BufferHandle handle) override;
				void unmapBuffer(rhi::BufferHandle handle) override;
				void writeBuffer(rhi::BufferHandle handle, const void* data, u64 size, u64 offset) override;
				void uploadBuffer(rhi::BufferHandle handle, const void* data, u64 size) override;
				void uploadTexture(rhi::TextureHandle handle, const rhi::TextureUpload* levels, u32 levelCount) override;

				void bindBindGroup(rhi::CommandBufferHandle cmd, rhi::PipelineLayoutHandle layout, u32 setIndex, rhi::BindGroupHandle group) override;
				void updateBindGroupTexture(rhi::BindGroupHandle group, u32 binding, u32 arrayIndex, rhi::TextureHandle texture, u32 baseMip, u32 mipCount) override;
				void copyBuffer(rhi::CommandBufferHandle cmd, rhi::BufferHandle dst, rhi::BufferHandle src, u64 size) override;
				void pushConstants(rhi::CommandBufferHandle cmd, rhi::PipelineLayoutHandle layout, const void* data, u32 size, u32 offset) override;
				bool supportsBindless() const override { return device_.supportsBindless(); }

				void bindVertexBuffer(rhi::CommandBufferHandle cmd, u32 slot, rhi::BufferHandle buffer, u32 stride, u64 offset) override;
				void bindIndexBuffer(rhi::CommandBufferHandle cmd, rhi::BufferHandle buffer, rhi::EIndexFormat format, u64 offset) override;

				void drawIndexed(rhi::CommandBufferHandle cmd, u32 indexCount, u32 instanceCount, u32 firstIndex, s32 vertexOffset, u32 firstInstance) override;

				void textureBarrier(rhi::CommandBufferHandle cmd, const rhi::TextureBarrier& barrier) override;
				void bufferBarrier(rhi::CommandBufferHandle cmd, const rhi::BufferBarrier& barrier) override;

				rhi::CommandBufferHandle getCurrentCommandBuffer() const override;

				void freeImage(rhi::TextureHandle handle) override;
				void freeBuffer(rhi::BufferHandle handle) override;

				void releaseImage(rhi::TextureHandle handle) override;
				void releaseBuffer(rhi::BufferHandle handle) override;

			private:
				void createBackbuffer() override;

				VkSampler getSampler(const rhi::SamplerDesc& desc);

				// A mip subrange needs its own VkImageView, so they are cached rather than
				// recreated every time a residency level changes.
				VkImageView getImageView(rhi::TextureHandle texture, u32 baseMip, u32 mipCount);

				// Records, submits and waits on a single throwaway command buffer. Used by the
				// upload paths and by one-time layout transitions.
				VkCommandBuffer beginOneShotCommands();
				void endOneShotCommands(VkCommandBuffer commandBuffer);

			private:
				struct VulkanBindGroup
				{
					VkDescriptorSet set = VK_NULL_HANDLE;
				};

			private:
				VulkanDevice device_;
				VulkanSwapchain swapchain_;

				VulkanDescriptorAllocator descriptorAllocator_;

				std::vector<VulkanBuffer> buffers_;
				std::vector<VulkanImage> images_;

				std::vector<rhi::TextureHandle> freeImageList_;
				std::vector<rhi::BufferHandle> freeBufferList_;

				std::vector<rhi::TextureHandle> backbuffers_;

				std::vector<VulkanFrameResource> frameResources_;

				std::vector<VulkanBindGroup> bindGroups_;

				// Samplers are immutable state, so identical descs share one object.
				std::vector<std::pair<rhi::SamplerDesc, VkSampler>> samplerCache_;

				struct ImageViewKey
				{
					rhi::TextureHandle texture;
					u32 baseMip;
					u32 mipCount;
				};

				std::vector<std::pair<ImageViewKey, VkImageView>> imageViewCache_;

				VulkanMemoryAllocator memoryAllocator_[(u32)rhi::EMemoryType::eNum];

				VulkanShaderManager shaderManager_;
				VulkanBindGroupLayoutManager layoutManager_;
				VulkanPipelineManager pipelineManager_;

				VulkanCommandPool commandPool_[(u32)rhi::EQueueType::eNum];

				types::u32 currentFrame_ = 0;
			};
		}
	}
}

#endif