
#ifndef _MV_DX_RHI_H_
#define _MV_DX_RHI_H_

#include "rhi/dx12_0/dx_device.h"
#include "rhi/dx12_0/dx_swapchain.h"
#include "rhi/dx12_0/dx_command.h"
#include "rhi/dx12_0/dx_frame_resource.h"
#include "rhi/dx12_0/dx_descriptor.h"
#include "rhi/dx12_0/dx_resource.h"
#include "rhi/dx12_0/dx_memory.h"
#include "rhi/dx12_0/dx_pipeline.h"

#include "rhi/rhi.h"

namespace mv
{
	namespace backend
	{
		namespace dx12_0
		{
			using namespace types;

			class DxRHI : public rhi::IRHI
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

				// Writes an SRV for a texture, honouring a mip subrange when one is asked for.
				void createTextureSrv(rhi::TextureHandle texture, u32 baseMip, u32 mipCount, D3D12_CPU_DESCRIPTOR_HANDLE dst);

			private:
				struct DxBindGroup
				{
					static constexpr u32 kNone = 0xFFFFFFFF;

					u32 viewIndex = kNone;
					u32 samplerIndex = kNone;

					// Needed to translate a binding number into its offset within the table
					// when a single slot is rewritten later.
					rhi::BindGroupLayoutHandle layout = INVALID_HANDLE;
				};

			private:
				DxDevice device_;
				DxSwapchain swapchain_;

				DxDescriptorAllocator rtvDescriptorAllocator_;
				DxDescriptorAllocator dsvDescriptorAllocator_;
				DxDescriptorAllocator srvDescriptorAllocator_;
				DxDescriptorAllocator globalDescriptorAllocator_;
				DxDescriptorAllocator samplerDescriptorAllocator_;

				std::vector<DxBindGroup> bindGroups_;

				std::vector<DxBuffer> buffers_;
				std::vector<DxImage> images_;

				std::vector<rhi::TextureHandle> freeImageList_;
				std::vector<rhi::BufferHandle> freeBufferList_;

				std::vector<rhi::TextureHandle> backbuffers_;

				std::vector<DxFrameResource> frameResources_;

				DxMemoryAllocator memoryAllocator_[(u32)rhi::EMemoryType::eNum];

				DxShaderManager shaderManager_;
				DxBindGroupLayoutManager layoutManager_;
				DxPipelineManager pipelineManager_;

				DxCommandPool commandPool_[(u32)rhi::EQueueType::eNum];

				types::u32 currentFrame_ = 0;
			};
		}
	}
}

#endif