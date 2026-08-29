#include "debug/debug_line_renderer.h"

#include <algorithm>

namespace mv::debugdraw
{
	bool DebugLineRenderer::initialize(
		const std::shared_ptr<rhi::IRHI>& rhi,
		const Shaders& shaders,
		rhi::ETextureFormat colorFormat,
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
			// The bindless group rides along unused: the shader includes the shared scene
			// header, and the layouts a pipeline binds have to match the sets it declares.
			rhi::PipelineLayoutDesc layoutDesc{};
			layoutDesc.bindGroups.push_back(sceneLayout);
			layoutDesc.bindGroups.push_back(bindlessLayout);

			pipelineLayout_ = rhi_->createPipelineLayout(layoutDesc);
		}

		rhi::ShaderDesc vsDesc{ rhi::EShaderType::eVertex, shaders.vs, shaders.vsSize, "VSMain" };
		rhi::ShaderDesc psDesc{ rhi::EShaderType::eFragment, shaders.ps, shaders.psSize, "PSMain" };

		rhi::GraphicsPipelineDesc pipelineDesc{};
		pipelineDesc.vs = rhi_->createShader(vsDesc);
		pipelineDesc.ps = rhi_->createShader(psDesc);
		pipelineDesc.layoutHandle = pipelineLayout_;

		pipelineDesc.vertexLayout.bindings.push_back({
			.binding = 0, .stride = sizeof(Vertex), .perInstance = false });
		pipelineDesc.vertexLayout.attributes.push_back({
			.location = 0, .semanticName = "POSITION", .semanticIndex = 0,
			.binding = 0, .format = rhi::EVertexFormat::eFloat3, .offset = (u32)offsetof(Vertex, position) });
		pipelineDesc.vertexLayout.attributes.push_back({
			.location = 1, .semanticName = "COLOR", .semanticIndex = 0,
			.binding = 0, .format = rhi::EVertexFormat::eFloat3, .offset = (u32)offsetof(Vertex, color) });

		pipelineDesc.topology = rhi::EPrimitiveTopology::eLineList;

		// Tested against the scene so the wireframes sit in the world, but never
		// written: debug ink must not occlude anything real.
		pipelineDesc.depth.depthTestEnable = true;
		pipelineDesc.depth.depthWriteEnable = false;
		pipelineDesc.depth.depthCompareOp = rhi::ECompareOp::eLessEqual;

		pipelineDesc.rasterizer.cullMode = rhi::ECullMode::eNone;

		pipelineDesc.colorFormats = { colorFormat };
		pipelineDesc.depthFormat = depthFormat;

		pipeline_ = rhi_->createGraphicsPipeline(pipelineDesc);

		if (pipeline_ == INVALID_HANDLE)
			return false;

		buffers_.resize(framesInFlight, INVALID_HANDLE);
		counts_.resize(framesInFlight, 0);

		for (u32 i = 0; i < framesInFlight; i++)
		{
			rhi::BufferDesc desc{};
			desc.size = (u64)kMaxVertices * sizeof(Vertex);
			desc.usage = rhi::EBufferUsage::eVertex;
			desc.memoryType = rhi::EMemoryType::eHostVisibleBuffer;

			buffers_[i] = rhi_->createBuffer(desc);

			if (buffers_[i] == INVALID_HANDLE)
				return false;
		}

		ready_ = true;

		return true;
	}

	void DebugLineRenderer::deinitialize()
	{
		if (rhi_)
		{
			for (rhi::BufferHandle buffer : buffers_)
			{
				if (buffer != INVALID_HANDLE)
					rhi_->releaseBuffer(buffer);
			}
		}

		buffers_.clear();
		counts_.clear();

		ready_ = false;
		rhi_.reset();
	}

	void DebugLineRenderer::upload(u32 frameIndex, const Vertex* vertices, u32 vertexCount)
	{
		if (!ready_ || frameIndex >= buffers_.size())
			return;

		const u32 count = (std::min)(vertexCount, kMaxVertices);

		counts_[frameIndex] = count;

		if (count > 0 && vertices != nullptr)
			rhi_->writeBuffer(buffers_[frameIndex], vertices, (u64)count * sizeof(Vertex), 0);
	}

	void DebugLineRenderer::record(
		rhi::CommandBufferHandle cmd,
		u32 frameIndex,
		rhi::BindGroupHandle sceneGroup,
		rhi::BindGroupHandle bindlessGroup)
	{
		if (!ready_ || frameIndex >= buffers_.size() || counts_[frameIndex] == 0)
			return;

		if (sceneGroup == INVALID_HANDLE || bindlessGroup == INVALID_HANDLE)
			return;

		rhi_->bindGraphicsPipeline(cmd, pipeline_);
		rhi_->bindBindGroup(cmd, pipelineLayout_, 0, sceneGroup);
		rhi_->bindBindGroup(cmd, pipelineLayout_, 1, bindlessGroup);

		rhi_->bindVertexBuffer(cmd, 0, buffers_[frameIndex], sizeof(Vertex), 0);

		rhi_->draw(cmd, counts_[frameIndex], 1, 0, 0);
	}
}
