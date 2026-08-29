
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
				void resize(u32 width, u32 height) override;

				void waitIdle() override;

				rhi::FrameContext beginFrame() override;
				void endFrame() override;

				void beginRenderPass(rhi::CommandBufferHandle cmd, const rhi::RenderPassDesc& desc) override;
				void endRenderPass(rhi::CommandBufferHandle cmd) override;

				void bindGraphicsPipeline(rhi::CommandBufferHandle cmd, rhi::PipelineHandle pipeline) override;

				void setViewport(rhi::CommandBufferHandle cmd, f32 x, f32 y, f32 width, f32 height) override;
				void setScissor(rhi::CommandBufferHandle cmd, s32 x, s32 y, u32 width, u32 height) override;

				void draw(rhi::CommandBufferHandle cmd, u32 vertexCount, u32 instanceCount, u32 firstVertex, u32 firstInstance) override;
				void drawIndirect(rhi::CommandBufferHandle cmd, rhi::BufferHandle args, u64 offset) override;

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

				void updateBindGroupBuffer(rhi::BindGroupHandle group, u32 binding, rhi::BufferHandle buffer, u64 offset, u32 stride, u32 count) override;
				void updateBindGroupStorageTexture(rhi::BindGroupHandle group, u32 binding, u32 arrayIndex, rhi::TextureHandle texture, u32 mipLevel) override;

				rhi::CommandBufferHandle beginImmediateCommands() override;
				void endImmediateCommands(rhi::CommandBufferHandle cmd) override;

				rhi::PipelineHandle createComputePipeline(const rhi::ComputePipelineDesc& desc) override;
				void bindComputePipeline(rhi::CommandBufferHandle cmd, rhi::PipelineHandle pipeline) override;
				void dispatch(rhi::CommandBufferHandle cmd, u32 groupsX, u32 groupsY, u32 groupsZ) override;

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

				// Destroys everything retired framesInFlight frames ago and recycles its handles.
				void drainPendingFrees(u32 frameSlot);

				// The actual destruction, once nothing can still be referencing it.
				void destroyImage(rhi::TextureHandle handle);
				void destroyBuffer(rhi::BufferHandle handle);

				void releaseImage(rhi::TextureHandle handle) override;
				void releaseBuffer(rhi::BufferHandle handle) override;

			private:
				void createBackbuffer() override;

				// Writes an SRV for a texture, honouring a mip subrange when one is asked for.
				void createTextureSrv(rhi::TextureHandle texture, u32 baseMip, u32 mipCount, D3D12_CPU_DESCRIPTOR_HANDLE dst);

				// One writable level, in the UAV-capable twin of the texture's format.
				void createTextureUav(rhi::TextureHandle texture, u32 mipLevel, D3D12_CPU_DESCRIPTOR_HANDLE dst);

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

				// Handles whose resources have actually been destroyed and can be handed out
				std::vector<rhi::TextureHandle> freeImageList_;
				std::vector<rhi::BufferHandle> freeBufferList_;

				// Retired this frame but not yet destroyed. A resource the render graph frees at
				// its last use is still referenced by the command list being recorded, and by the
				// frames already in flight. Destroying it there is what the debug layer calls
				// deleting an object that is still in use, and what hangs the device. Each slot
				// is drained at the top of the frame that reuses it, which is the point at which
				// its fence has proved the GPU is done.
				std::vector<std::vector<rhi::TextureHandle>> pendingImageFree_;
				std::vector<std::vector<rhi::BufferHandle>> pendingBufferFree_;

				std::vector<rhi::TextureHandle> backbuffers_;

				std::vector<DxFrameResource> frameResources_;

				DxMemoryAllocator memoryAllocator_[(u32)rhi::EMemoryType::eNum];

				DxShaderManager shaderManager_;
				DxBindGroupLayoutManager layoutManager_;
				DxPipelineManager pipelineManager_;

				DxCommandPool commandPool_[(u32)rhi::EQueueType::eNum];

				// Backs beginImmediateCommands. One is enough: an immediate submit is waited
				// on before it returns, so two can never be outstanding at once.
				wrl::ComPtr<ID3D12CommandAllocator> immediateAllocator_;

				// Lazily built on the first indirect draw; interprets plain draw arguments.
				wrl::ComPtr<ID3D12CommandSignature> drawSignature_;

				types::u32 currentFrame_ = 0;
			};
		}
	}
}

#endif