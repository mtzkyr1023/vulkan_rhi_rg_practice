#include "props/prop_renderer.h"

#include <cstring>

namespace mv::props
{
	namespace
	{
		// Must match PropConstants in prop.hlsl.
		struct PropGpuConstants
		{
			f32 model[16]{};

			u32 materialIndex = 0;
			f32 pad[3]{};
		};

		static_assert(sizeof(PropGpuConstants) == 80, "the prop push constants must stay inside the guaranteed block");
	}

	bool PropRenderer::initialize(
		const std::shared_ptr<rhi::IRHI>& rhi,
		const Shaders& shaders,
		const std::vector<rhi::ETextureFormat>& colorFormats,
		rhi::ETextureFormat depthFormat,
		rhi::BindGroupLayoutHandle sceneLayout,
		rhi::BindGroupLayoutHandle bindlessLayout)
	{
		if (!rhi || shaders.vs == nullptr || shaders.ps == nullptr)
			return false;

		if (sceneLayout == INVALID_HANDLE || bindlessLayout == INVALID_HANDLE)
			return false;

		rhi_ = rhi;

		{
			rhi::PipelineLayoutDesc layoutDesc{};
			layoutDesc.bindGroups.push_back(sceneLayout);
			layoutDesc.bindGroups.push_back(bindlessLayout);
			layoutDesc.pushConstantSize = sizeof(PropGpuConstants);

			pipelineLayout_ = rhi_->createPipelineLayout(layoutDesc);
		}

		rhi::ShaderDesc vsDesc{ rhi::EShaderType::eVertex, shaders.vs, shaders.vsSize, "VSMain" };
		rhi::ShaderDesc psDesc{ rhi::EShaderType::eFragment, shaders.ps, shaders.psSize, "PSMain" };

		rhi::GraphicsPipelineDesc pipelineDesc{};
		pipelineDesc.vs = rhi_->createShader(vsDesc);
		pipelineDesc.ps = rhi_->createShader(psDesc);
		pipelineDesc.layoutHandle = pipelineLayout_;

		// The same vertex the whole engine feeds: position, normal, uv.
		pipelineDesc.vertexLayout.bindings.push_back({
			.binding = 0, .stride = sizeof(asset::ModelVertex), .perInstance = false });
		pipelineDesc.vertexLayout.attributes.push_back({
			.location = 0, .semanticName = "POSITION", .semanticIndex = 0,
			.binding = 0, .format = rhi::EVertexFormat::eFloat3, .offset = (u32)offsetof(asset::ModelVertex, position) });
		pipelineDesc.vertexLayout.attributes.push_back({
			.location = 1, .semanticName = "NORMAL", .semanticIndex = 0,
			.binding = 0, .format = rhi::EVertexFormat::eFloat3, .offset = (u32)offsetof(asset::ModelVertex, normal) });
		pipelineDesc.vertexLayout.attributes.push_back({
			.location = 2, .semanticName = "TEXCOORD", .semanticIndex = 0,
			.binding = 0, .format = rhi::EVertexFormat::eFloat2, .offset = (u32)offsetof(asset::ModelVertex, uv) });

		// Geometry like any other: depth-tested, depth-writing, opaque.
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

	void PropRenderer::deinitialize()
	{
		ready_ = false;
		rhi_.reset();
	}

	void PropRenderer::record(
		rhi::CommandBufferHandle cmd,
		const asset::Model& model,
		const math::Mat4& transform,
		rhi::BindGroupHandle sceneGroup,
		rhi::BindGroupHandle bindlessGroup)
	{
		if (!ready_ || model.vertexBuffer == INVALID_HANDLE ||
			sceneGroup == INVALID_HANDLE || bindlessGroup == INVALID_HANDLE)
			return;

		rhi_->bindGraphicsPipeline(cmd, pipeline_);
		rhi_->bindBindGroup(cmd, pipelineLayout_, 0, sceneGroup);
		rhi_->bindBindGroup(cmd, pipelineLayout_, 1, bindlessGroup);

		rhi_->bindVertexBuffer(cmd, 0, model.vertexBuffer, sizeof(asset::ModelVertex), 0);
		rhi_->bindIndexBuffer(cmd, model.indexBuffer, rhi::EIndexFormat::eUint32, 0);

		PropGpuConstants constants{};
		std::memcpy(constants.model, transform.m, sizeof(constants.model));

		for (const auto& primitive : model.primitives)
		{
			constants.materialIndex = primitive.material;

			rhi_->pushConstants(cmd, pipelineLayout_, &constants, sizeof(constants), 0);
			rhi_->drawIndexed(cmd, primitive.indexCount, 1, primitive.firstIndex, 0, 0);
		}
	}
}
