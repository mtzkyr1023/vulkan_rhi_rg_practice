#include "props/skinned_prop_renderer.h"

#include <algorithm>
#include <cstring>

namespace mv::props
{
	namespace
	{
		// Must match SkinnedConstants in skinned_prop.hlsl -- the same 80 bytes the
		// static prop pass pushes.
		struct SkinnedGpuConstants
		{
			f32 model[16]{};

			u32 materialIndex = 0;
			f32 pad[3]{};
		};

		static_assert(sizeof(SkinnedGpuConstants) == 80, "the skinned push constants must stay inside the guaranteed block");

		// The palette buffer stores rows, not matrices: a float4 element sidesteps
		// every question about structured float4x4 layout across the two compilers.
		constexpr u32 kRowsPerJoint = 4;
	}

	bool SkinnedPropRenderer::initialize(
		const std::shared_ptr<rhi::IRHI>& rhi,
		const Shaders& shaders,
		const std::vector<rhi::ETextureFormat>& colorFormats,
		rhi::ETextureFormat depthFormat,
		rhi::BindGroupLayoutHandle sceneLayout,
		rhi::BindGroupLayoutHandle bindlessLayout,
		u32 framesInFlight)
	{
		if (!rhi || shaders.vs == nullptr || shaders.ps == nullptr || framesInFlight == 0)
			return false;

		if (sceneLayout == INVALID_HANDLE || bindlessLayout == INVALID_HANDLE)
			return false;

		rhi_ = rhi;

		{
			rhi::BindGroupLayoutDesc layoutDesc{};
			layoutDesc.bindings.push_back({
				.binding = 0, .count = 1,
				.type = rhi::EDescriptorType::eStorageBuffer, .stages = rhi::EShaderStage::eVertex });

			paletteLayout_ = rhi_->createBindGroupLayout(layoutDesc);
		}

		{
			rhi::PipelineLayoutDesc layoutDesc{};
			layoutDesc.bindGroups.push_back(sceneLayout);
			layoutDesc.bindGroups.push_back(bindlessLayout);
			layoutDesc.bindGroups.push_back(paletteLayout_);
			layoutDesc.pushConstantSize = sizeof(SkinnedGpuConstants);

			pipelineLayout_ = rhi_->createPipelineLayout(layoutDesc);
		}

		paletteBuffers_.resize(framesInFlight, INVALID_HANDLE);
		paletteGroups_.resize(framesInFlight, INVALID_HANDLE);

		for (u32 i = 0; i < framesInFlight; i++)
		{
			rhi::BufferDesc desc{};
			desc.size = (u64)kMaxJoints * sizeof(math::Mat4);
			desc.usage = rhi::EBufferUsage::eStorage;
			desc.memoryType = rhi::EMemoryType::eHostVisibleBuffer;

			paletteBuffers_[i] = rhi_->createBuffer(desc);

			rhi::BindGroupDesc groupDesc{};
			groupDesc.layout = paletteLayout_;
			groupDesc.storageBuffers.push_back({
				.binding = 0, .buffer = paletteBuffers_[i], .offset = 0,
				.stride = sizeof(f32) * 4, .count = kMaxJoints * kRowsPerJoint });

			paletteGroups_[i] = rhi_->createBindGroup(groupDesc);
		}

		rhi::ShaderDesc vsDesc{ rhi::EShaderType::eVertex, shaders.vs, shaders.vsSize, "VSMain" };
		rhi::ShaderDesc psDesc{ rhi::EShaderType::eFragment, shaders.ps, shaders.psSize, "PSMain" };

		rhi::GraphicsPipelineDesc pipelineDesc{};
		pipelineDesc.vs = rhi_->createShader(vsDesc);
		pipelineDesc.ps = rhi_->createShader(psDesc);
		pipelineDesc.layoutHandle = pipelineLayout_;

		pipelineDesc.vertexLayout.bindings.push_back({
			.binding = 0, .stride = sizeof(asset::SkinnedVertex), .perInstance = false });
		pipelineDesc.vertexLayout.attributes.push_back({
			.location = 0, .semanticName = "POSITION", .semanticIndex = 0,
			.binding = 0, .format = rhi::EVertexFormat::eFloat3, .offset = (u32)offsetof(asset::SkinnedVertex, position) });
		pipelineDesc.vertexLayout.attributes.push_back({
			.location = 1, .semanticName = "NORMAL", .semanticIndex = 0,
			.binding = 0, .format = rhi::EVertexFormat::eFloat3, .offset = (u32)offsetof(asset::SkinnedVertex, normal) });
		pipelineDesc.vertexLayout.attributes.push_back({
			.location = 2, .semanticName = "TEXCOORD", .semanticIndex = 0,
			.binding = 0, .format = rhi::EVertexFormat::eFloat2, .offset = (u32)offsetof(asset::SkinnedVertex, uv) });
		pipelineDesc.vertexLayout.attributes.push_back({
			.location = 3, .semanticName = "JOINTS", .semanticIndex = 0,
			.binding = 0, .format = rhi::EVertexFormat::eFloat4, .offset = (u32)offsetof(asset::SkinnedVertex, joints) });
		pipelineDesc.vertexLayout.attributes.push_back({
			.location = 4, .semanticName = "WEIGHTS", .semanticIndex = 0,
			.binding = 0, .format = rhi::EVertexFormat::eFloat4, .offset = (u32)offsetof(asset::SkinnedVertex, weights) });

		pipelineDesc.depth.depthTestEnable = true;
		pipelineDesc.depth.depthWriteEnable = true;
		pipelineDesc.depth.depthCompareOp = rhi::ECompareOp::eLessEqual;

		pipelineDesc.rasterizer.cullMode = rhi::ECullMode::eBack;

		pipelineDesc.colorFormats = colorFormats;
		pipelineDesc.depthFormat = depthFormat;

		pipeline_ = rhi_->createGraphicsPipeline(pipelineDesc);

		ready_ = pipeline_ != INVALID_HANDLE;

		return ready_;
	}

	void SkinnedPropRenderer::deinitialize()
	{
		if (rhi_)
		{
			for (rhi::BufferHandle buffer : paletteBuffers_)
			{
				if (buffer != INVALID_HANDLE)
					rhi_->releaseBuffer(buffer);
			}
		}

		paletteBuffers_.clear();
		paletteGroups_.clear();

		ready_ = false;
		rhi_.reset();
	}

	void SkinnedPropRenderer::setPalette(u32 frameIndex, const math::Mat4* matrices, u32 count)
	{
		if (!ready_ || frameIndex >= paletteBuffers_.size() || matrices == nullptr)
			return;

		const u32 clamped = (std::min)(count, kMaxJoints);

		if (clamped > 0)
			rhi_->writeBuffer(paletteBuffers_[frameIndex], matrices, (u64)clamped * sizeof(math::Mat4), 0);
	}

	void SkinnedPropRenderer::record(
		rhi::CommandBufferHandle cmd,
		const asset::SkinnedModel& model,
		const math::Mat4& transform,
		u32 frameIndex,
		rhi::BindGroupHandle sceneGroup,
		rhi::BindGroupHandle bindlessGroup)
	{
		if (!ready_ || model.vertexBuffer == INVALID_HANDLE ||
			frameIndex >= paletteGroups_.size() ||
			sceneGroup == INVALID_HANDLE || bindlessGroup == INVALID_HANDLE)
			return;

		rhi_->bindGraphicsPipeline(cmd, pipeline_);
		rhi_->bindBindGroup(cmd, pipelineLayout_, 0, sceneGroup);
		rhi_->bindBindGroup(cmd, pipelineLayout_, 1, bindlessGroup);
		rhi_->bindBindGroup(cmd, pipelineLayout_, 2, paletteGroups_[frameIndex]);

		rhi_->bindVertexBuffer(cmd, 0, model.vertexBuffer, sizeof(asset::SkinnedVertex), 0);
		rhi_->bindIndexBuffer(cmd, model.indexBuffer, rhi::EIndexFormat::eUint32, 0);

		SkinnedGpuConstants constants{};
		std::memcpy(constants.model, transform.m, sizeof(constants.model));

		for (const auto& primitive : model.primitives)
		{
			constants.materialIndex = primitive.material;

			rhi_->pushConstants(cmd, pipelineLayout_, &constants, sizeof(constants), 0);
			rhi_->drawIndexed(cmd, primitive.indexCount, 1, primitive.firstIndex, 0, 0);
		}
	}
}
