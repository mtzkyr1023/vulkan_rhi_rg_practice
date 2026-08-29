#include "compute/buffer_fill.h"

namespace mv::compute
{
	namespace
	{
		// Must match the [numthreads] in bufferfill.hlsl.
		constexpr u32 kGroupSize = 64;

		// Must match BufferFillConstants in bufferfill.hlsl.
		struct BufferFillConstants
		{
			u32 count;
			u32 value;
			u32 pad[2]{};
		};
	}

	bool BufferFill::initialize(const std::shared_ptr<rhi::IRHI>& rhi, const u32* shaderBytecode, u32 shaderSize)
	{
		if (!rhi || shaderBytecode == nullptr || shaderSize == 0)
			return false;

		rhi_ = rhi;

		rhi::BindGroupLayoutDesc layoutDesc{};
		layoutDesc.bindings.push_back({
			.binding = 0, .count = 1,
			.type = rhi::EDescriptorType::eStorageBufferReadWrite,
			.stages = rhi::EShaderStage::eCompute });

		layout_ = rhi_->createBindGroupLayout(layoutDesc);

		rhi::PipelineLayoutDesc pipelineLayoutDesc{};
		pipelineLayoutDesc.bindGroups.push_back(layout_);
		pipelineLayoutDesc.pushConstantSize = sizeof(BufferFillConstants);

		pipelineLayout_ = rhi_->createPipelineLayout(pipelineLayoutDesc);

		rhi::ShaderDesc shaderDesc{ rhi::EShaderType::eCompute, shaderBytecode, shaderSize, "CSMain" };
		const rhi::ShaderHandle shader = rhi_->createShader(shaderDesc);

		rhi::ComputePipelineDesc pipelineDesc{};
		pipelineDesc.cs = shader;
		pipelineDesc.layoutHandle = pipelineLayout_;

		pipeline_ = rhi_->createComputePipeline(pipelineDesc);

		return pipeline_ != INVALID_HANDLE;
	}

	void BufferFill::deinitialize()
	{
		rhi_.reset();

		layout_ = INVALID_HANDLE;
		pipelineLayout_ = INVALID_HANDLE;
		pipeline_ = INVALID_HANDLE;
		group_ = INVALID_HANDLE;
		target_ = INVALID_HANDLE;
	}

	void BufferFill::setTarget(rhi::BufferHandle buffer, u32 elementCount)
	{
		if (!isReady() || buffer == INVALID_HANDLE || elementCount == 0)
			return;

		target_ = buffer;
		elementCount_ = elementCount;

		rhi::BindGroupDesc groupDesc{};
		groupDesc.layout = layout_;
		groupDesc.storageBuffers.push_back({
			.binding = 0, .buffer = buffer, .offset = 0, .stride = sizeof(u32), .count = elementCount });

		group_ = rhi_->createBindGroup(groupDesc);
	}

	void BufferFill::record(rhi::CommandBufferHandle cmd, u32 value)
	{
		if (!isReady() || group_ == INVALID_HANDLE)
			return;

		rhi_->bindComputePipeline(cmd, pipeline_);
		rhi_->bindBindGroup(cmd, pipelineLayout_, 0, group_);

		BufferFillConstants constants{};
		constants.count = elementCount_;
		constants.value = value;

		rhi_->pushConstants(cmd, pipelineLayout_, &constants, sizeof(constants), 0);

		rhi_->dispatch(cmd, (elementCount_ + kGroupSize - 1) / kGroupSize, 1, 1);

		// Nothing orders this dispatch's writes against the next pass's reads of the same
		// buffer, and on the graphics queue the next pass is a fragment shader atomically
		// updating exactly these values.
		rhi::BufferBarrier barrier{};
		barrier.buffer = target_;
		barrier.before = rhi::EResourceState::eShaderWrite;
		barrier.after = rhi::EResourceState::eShaderWrite;

		rhi_->bufferBarrier(cmd, barrier);
	}
}
