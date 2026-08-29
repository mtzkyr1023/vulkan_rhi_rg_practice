#include "compute/mip_generator.h"

#include <algorithm>

namespace mv::compute
{
	namespace
	{
		// Must match the [numthreads] in mipgen.hlsl.
		constexpr u32 kGroupSize = 8;

		// Must match MipGenConstants in mipgen.hlsl.
		struct MipGenConstants
		{
			u32 outputWidth;
			u32 outputHeight;
			u32 inputWidth;
			u32 inputHeight;

			u32 srgb;
			u32 pad[3]{};
		};

		u32 groupCount(u32 extent)
		{
			return (extent + kGroupSize - 1) / kGroupSize;
		}
	}

	bool MipGenerator::initialize(const std::shared_ptr<rhi::IRHI>& rhi, const u32* shaderBytecode, u32 shaderSize)
	{
		if (!rhi || shaderBytecode == nullptr || shaderSize == 0)
			return false;

		rhi_ = rhi;

		rhi::BindGroupLayoutDesc layoutDesc{};
		// Both ends are storage images. The source could be a sampled one, but then the
		// level being read would have to be in a different layout from the level being
		// written, and the RHI transitions a texture as a whole.
		layoutDesc.bindings.push_back({
			.binding = 0, .count = 1,
			.type = rhi::EDescriptorType::eStorageImage,
			.stages = rhi::EShaderStage::eCompute });
		layoutDesc.bindings.push_back({
			.binding = 1, .count = 1,
			.type = rhi::EDescriptorType::eStorageImage,
			.stages = rhi::EShaderStage::eCompute });

		layout_ = rhi_->createBindGroupLayout(layoutDesc);

		rhi::PipelineLayoutDesc pipelineLayoutDesc{};
		pipelineLayoutDesc.bindGroups.push_back(layout_);
		pipelineLayoutDesc.pushConstantSize = sizeof(MipGenConstants);

		pipelineLayout_ = rhi_->createPipelineLayout(pipelineLayoutDesc);

		rhi::ShaderDesc shaderDesc{ rhi::EShaderType::eCompute, shaderBytecode, shaderSize, "CSMain" };
		const rhi::ShaderHandle shader = rhi_->createShader(shaderDesc);

		rhi::ComputePipelineDesc pipelineDesc{};
		pipelineDesc.cs = shader;
		pipelineDesc.layoutHandle = pipelineLayout_;

		pipeline_ = rhi_->createComputePipeline(pipelineDesc);

		return pipeline_ != INVALID_HANDLE;
	}

	void MipGenerator::deinitialize()
	{
		rhi_.reset();

		levelGroups_.clear();

		layout_ = INVALID_HANDLE;
		pipelineLayout_ = INVALID_HANDLE;
		pipeline_ = INVALID_HANDLE;
	}

	rhi::BindGroupHandle MipGenerator::groupForLevel(u32 level)
	{
		while (levelGroups_.size() <= level)
		{
			// The bindings are placeholders: every use re-points them at the texture being
			// processed. Creating the group needs *something* valid in each slot, and there
			// is nothing to point at yet, so the group is created empty and filled in by
			// the update calls in record().
			rhi::BindGroupDesc groupDesc{};
			groupDesc.layout = layout_;

			levelGroups_.push_back(rhi_->createBindGroup(groupDesc));
		}

		return levelGroups_[level];
	}

	void MipGenerator::record(rhi::CommandBufferHandle cmd, rhi::TextureHandle texture, u32 width, u32 height, u32 mipLevels, bool srgb)
	{
		if (!isReady() || mipLevels <= 1)
			return;

		rhi_->bindComputePipeline(cmd, pipeline_);

		u32 sourceWidth = width;
		u32 sourceHeight = height;

		for (u32 level = 1; level < mipLevels; level++)
		{
			const u32 targetWidth = std::max(1u, sourceWidth / 2);
			const u32 targetHeight = std::max(1u, sourceHeight / 2);

			const rhi::BindGroupHandle group = groupForLevel(level);

			rhi_->updateBindGroupStorageTexture(group, 0, 0, texture, level - 1);
			rhi_->updateBindGroupStorageTexture(group, 1, 0, texture, level);

			rhi_->bindBindGroup(cmd, pipelineLayout_, 0, group);

			MipGenConstants constants{};
			constants.outputWidth = targetWidth;
			constants.outputHeight = targetHeight;
			constants.inputWidth = sourceWidth;
			constants.inputHeight = sourceHeight;
			constants.srgb = srgb ? 1u : 0u;

			rhi_->pushConstants(cmd, pipelineLayout_, &constants, sizeof(constants), 0);

			rhi_->dispatch(cmd, groupCount(targetWidth), groupCount(targetHeight), 1);

			// The level just written is the next iteration's source. Without this the read
			// can start before the write has landed, and the chain fills with noise from
			// whatever the memory held.
			if (level + 1 < mipLevels)
			{
				rhi::TextureBarrier barrier{};
				barrier.texture = texture;
				barrier.before = rhi::EResourceState::eShaderWrite;
				barrier.after = rhi::EResourceState::eShaderWrite;

				rhi_->textureBarrier(cmd, barrier);
			}

			sourceWidth = targetWidth;
			sourceHeight = targetHeight;
		}
	}

	void MipGenerator::generate(rhi::TextureHandle texture, u32 width, u32 height, u32 mipLevels, bool srgb)
	{
		if (!isReady() || mipLevels <= 1)
			return;

		const rhi::CommandBufferHandle cmd = rhi_->beginImmediateCommands();

		// Into the layout a storage image has to be written in, from whatever the upload
		// left the texture in.
		rhi::TextureBarrier toWrite{};
		toWrite.texture = texture;
		toWrite.before = rhi::EResourceState::eShaderRead;
		toWrite.after = rhi::EResourceState::eShaderWrite;
		rhi_->textureBarrier(cmd, toWrite);

		record(cmd, texture, width, height, mipLevels, srgb);

		rhi::TextureBarrier toRead{};
		toRead.texture = texture;
		toRead.before = rhi::EResourceState::eShaderWrite;
		toRead.after = rhi::EResourceState::eShaderRead;
		rhi_->textureBarrier(cmd, toRead);

		rhi_->endImmediateCommands(cmd);
	}
}
