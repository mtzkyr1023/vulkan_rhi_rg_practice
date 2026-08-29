
#ifndef _MV_RHI_H_
#define _MV_RHI_H_

#include <memory>

#include "util/types.h"

#include "rhi/resource.h"
#include "rhi/commandbuffer.h"
#include "rhi/pipeline.h"

namespace mv
{
	namespace rhi
	{
		struct FrameContext
		{
			TextureHandle backbuffer;
			CommandBufferHandle cmd;
			u32 currentFrameIndex;
		};

		class ICommandBuffer;
		enum class EQueueType;

		class IRHI
		{
		public:
			virtual ~IRHI() {}

			virtual void initialize(void* hwnd) = 0;
			virtual void deinitialize() = 0;

			virtual void waitIdle() = 0;

			virtual FrameContext beginFrame() = 0;
			virtual void endFrame() = 0;

			virtual void beginRenderPass(CommandBufferHandle cmd, const RenderPassDesc& desc) = 0;
			virtual void endRenderPass(CommandBufferHandle cmd) = 0;

			virtual void bindGraphicsPipeline(CommandBufferHandle cmd, PipelineHandle pipeline) = 0;

			virtual void setViewport(CommandBufferHandle cmd, f32 x, f32 y, f32 width, f32 height) = 0;
			virtual void setScissor(CommandBufferHandle cmd, s32 x, s32 y, u32 width, u32 height) = 0;

			virtual void draw(CommandBufferHandle cmd, u32 vertexCount, u32 instanceCount, u32 firstVertex, u32 firstInstance) = 0;

			// Draw whose arguments live in a buffer a shader filled: sixteen bytes laid out
			// as vertexCount, instanceCount, firstVertex, firstInstance, which is the one
			// layout both APIs happen to agree on. The buffer needs eIndirectArgs usage and
			// the eIndirectArgument state when the draw reads it.
			virtual void drawIndirect(CommandBufferHandle cmd, BufferHandle args, u64 offset) = 0;

			// The two backends do not necessarily agree on a swap chain format, and a pipeline's
			// render target formats must match the attachment it renders to, so callers building
			// pipelines for the backbuffer have to ask rather than assume.
			virtual ETextureFormat backbufferFormat() const = 0;

			// Rebuilds the swap chain at a new size, keeping the backbuffer handles stable so
			// anything holding one stays valid. Waits for the GPU first: the buffers being
			// replaced may still be on screen or in flight.
			//
			// Everything else that was sized to the window is the caller's problem. The RHI
			// has no idea which of its textures were meant to track it.
			virtual void resize(u32 width, u32 height) = 0;

			// Callers that keep per-frame copies of a resource need to know how many.
			u32 framesInFlight() const { return framesInFlight_; }

			virtual CommandBufferHandle allocateCommandBuffer(EQueueType type) = 0;


			virtual BufferHandle createBuffer(const BufferDesc& desc) = 0;
			virtual TextureHandle createTexture(const TextureDesc& desc) = 0;

			virtual ShaderHandle createShader(const ShaderDesc& desc) = 0;
			virtual BindGroupLayoutHandle createBindGroupLayout(const BindGroupLayoutDesc& desc) = 0;
			virtual BindGroupHandle createBindGroup(const BindGroupDesc& desc) = 0;
			virtual PipelineLayoutHandle createPipelineLayout(const PipelineLayoutDesc& desc) = 0;
			virtual PipelineHandle createGraphicsPipeline(const GraphicsPipelineDesc& desc) = 0;

			// Host-visible buffers only; returns null for device-local ones. Vulkan keeps its
			// host-visible pools permanently mapped, so unmapBuffer is a no-op there and only
			// D3D12 actually releases the pointer.
			virtual void* mapBuffer(BufferHandle handle) = 0;
			virtual void unmapBuffer(BufferHandle handle) = 0;

			virtual void writeBuffer(BufferHandle handle, const void* data, u64 size, u64 offset) = 0;

			// Device-local destination: stages through a temporary host-visible buffer and
			// submits the copy immediately, blocking until the GPU is done with it.
			virtual void uploadBuffer(BufferHandle handle, const void* data, u64 size) = 0;

			// One entry per mip level, largest first, each tightly packed and matching that
			// level's extent. Uploaded in a single submit, and left ready to be sampled.
			virtual void uploadTexture(TextureHandle handle, const TextureUpload* levels, u32 levelCount) = 0;

			virtual void bindBindGroup(CommandBufferHandle cmd, PipelineLayoutHandle layout, u32 setIndex, BindGroupHandle group) = 0;

			// Writes one slot of an array binding after the group already exists, which is
			// what makes a bindless table usable as textures stream in. The mip range lets
			// the slot expose only the levels that are currently resident.
			virtual void updateBindGroupTexture(
				BindGroupHandle group,
				u32 binding,
				u32 arrayIndex,
				TextureHandle texture,
				u32 baseMip = 0,
				u32 mipCount = 0) = 0;

			// Records outside a frame, for work that happens when an asset is built rather
			// than when one is drawn: generating a mip chain, baking a sky, turning a
			// heightmap into vertices. The upload paths already did this privately; a
			// compute pass that runs at load time needs the same thing, and needs it in
			// terms of the public command interface.
			//
			// endImmediateCommands submits and waits, so anything recorded is complete by
			// the time it returns. That is a full pipeline stall, which is exactly right at
			// load time and exactly wrong per frame.
			virtual CommandBufferHandle beginImmediateCommands() = 0;
			virtual void endImmediateCommands(CommandBufferHandle cmd) = 0;

			virtual PipelineHandle createComputePipeline(const ComputePipelineDesc& desc) = 0;

			// Compute is its own bind point. Vulkan spells that out in every call; D3D12
			// keeps a separate root signature and descriptor tables for it on the same
			// command list, so binding a compute pipeline is what switches the rest of the
			// binding calls over to it.
			virtual void bindComputePipeline(CommandBufferHandle cmd, PipelineHandle pipeline) = 0;

			// Thread *groups*, not threads. The group size lives in the shader.
			virtual void dispatch(CommandBufferHandle cmd, u32 groupsX, u32 groupsY, u32 groupsZ) = 0;

			// Re-points one writable-image binding. Generating a mip chain rebinds the same
			// slot to each level in turn rather than declaring one descriptor per level.
			virtual void updateBindGroupStorageTexture(
				BindGroupHandle group,
				u32 binding,
				u32 arrayIndex,
				TextureHandle texture,
				u32 mipLevel) = 0;

			// Re-points one buffer binding of an existing group. The counterpart to the
			// texture version, for geometry that is regenerated rather than streamed: the
			// visibility buffer resolves triangles out of whatever vertex and index buffers
			// this names, so swapping scenes means swapping these rather than rebuilding
			// the group and every descriptor in it.
			virtual void updateBindGroupBuffer(
				BindGroupHandle group,
				u32 binding,
				BufferHandle buffer,
				u64 offset,
				u32 stride,
				u32 count) = 0;

			// GPU-side copy, for pulling a shader-written buffer into readback memory.
			// Inserts the barrier that orders it after those writes.
			virtual void copyBuffer(CommandBufferHandle cmd, BufferHandle dst, BufferHandle src, u64 size) = 0;

			virtual void pushConstants(CommandBufferHandle cmd, PipelineLayoutHandle layout, const void* data, u32 size, u32 offset) = 0;

			// False means unbounded descriptor arrays are unavailable and callers have to
			// fall back to binding resources per draw.
			virtual bool supportsBindless() const = 0;

			virtual void bindVertexBuffer(CommandBufferHandle cmd, u32 slot, BufferHandle buffer, u32 stride, u64 offset) = 0;
			virtual void bindIndexBuffer(CommandBufferHandle cmd, BufferHandle buffer, EIndexFormat format, u64 offset) = 0;

			virtual void drawIndexed(CommandBufferHandle cmd, u32 indexCount, u32 instanceCount, u32 firstIndex, s32 vertexOffset, u32 firstInstance) = 0;

			virtual void textureBarrier(CommandBufferHandle cmd, const TextureBarrier& barrier) = 0;
			virtual void bufferBarrier(CommandBufferHandle cmd, const BufferBarrier& barrier) = 0;

			virtual CommandBufferHandle getCurrentCommandBuffer() const = 0;

			virtual void freeImage(TextureHandle handle) = 0;
			virtual void freeBuffer(BufferHandle handle) = 0;

			virtual void releaseImage(TextureHandle handle) = 0;
			virtual void releaseBuffer(BufferHandle handle) = 0;

			static std::shared_ptr<IRHI> createVulkanRHI();
			static std::shared_ptr<IRHI> createDx12RHI();

		protected:
			virtual void createBackbuffer() = 0;

		protected:
			static constexpr types::u32 framesInFlight_ = 2;
		};
	}
}

#endif