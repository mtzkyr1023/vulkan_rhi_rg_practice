#include "grass/grass_field.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace mv::grass
{
	namespace
	{
		// Must match the canonical blade in grass.hlsl: five triangles, listed out.
		constexpr u32 kVertsPerBlade = 15;

		// Must match the [numthreads] in grass_cull.hlsl.
		constexpr u32 kCullTile = 64;

		// float4s per instance record, matching writer and reader.
		constexpr u32 kFloat4sPerInstance = 3;

		// Must match GrassCullConstants in grass_cull.hlsl.
		struct CullGpuConstants
		{
			f32 planes[4][4]{};

			f32 cameraPosition[3]{};
			f32 radius = 0.0f;

			f32 worldSize = 0.0f;
			f32 heightScale = 0.0f;
			f32 rockHeight = 0.0f;
			f32 rockSlope = 0.0f;

			f32 waterLevel = 0.0f;
			f32 density = 0.0f;
			f32 bladeHeight = 0.0f;
			f32 pad = 0.0f;

			u32 fieldResolution = 0;
			u32 bladesPerSide = 0;
			u32 pad2[2]{};
		};

		static_assert(sizeof(CullGpuConstants) == 128, "the cull push constants must fit the guaranteed block");

		// Must match GrassConstants in grass.hlsl.
		struct GrassGpuConstants
		{
			f32 rootColor[3]{};
			f32 bladeHeight = 0.0f;

			f32 tipColor[3]{};
			f32 bladeWidth = 0.0f;

			f32 time = 0.0f;
			f32 windStrength = 0.0f;
			f32 pad[2]{};
		};
	}

	bool GrassField::initialize(
		const std::shared_ptr<rhi::IRHI>& rhi,
		const Shaders& shaders,
		const std::vector<rhi::ETextureFormat>& colorFormats,
		rhi::ETextureFormat depthFormat,
		rhi::BindGroupLayoutHandle sceneLayout,
		rhi::BindGroupLayoutHandle bindlessLayout)
	{
		if (!rhi || shaders.vs == nullptr || shaders.ps == nullptr ||
			shaders.cull == nullptr || shaders.reset == nullptr)
			return false;

		if (sceneLayout == INVALID_HANDLE || bindlessLayout == INVALID_HANDLE)
			return false;

		rhi_ = rhi;

		// --- the buffers the cull fills and the draw consumes ----------------------

		{
			rhi::BufferDesc desc{};
			desc.size = (u64)kMaxBladesPerSide * kMaxBladesPerSide * kFloat4sPerInstance * 16;
			desc.usage = rhi::EBufferUsage::eStorage | rhi::EBufferUsage::eStorageReadWrite;
			desc.memoryType = rhi::EMemoryType::eDeviceLocalBuffer;

			instances_ = rhi_->createBuffer(desc);
		}

		{
			rhi::BufferDesc desc{};
			desc.size = 4 * sizeof(u32);
			desc.usage = rhi::EBufferUsage::eStorage | rhi::EBufferUsage::eStorageReadWrite |
				rhi::EBufferUsage::eIndirectArgs | rhi::EBufferUsage::eTransferDst;
			desc.memoryType = rhi::EMemoryType::eDeviceLocalBuffer;

			drawArgs_ = rhi_->createBuffer(desc);
		}

		// --- cull ------------------------------------------------------------------

		{
			rhi::BindGroupLayoutDesc desc{};
			desc.bindings.push_back({
				.binding = 0, .count = 1,
				.type = rhi::EDescriptorType::eStorageBuffer,
				.stages = rhi::EShaderStage::eCompute });
			desc.bindings.push_back({
				.binding = 1, .count = 1,
				.type = rhi::EDescriptorType::eStorageBufferReadWrite,
				.stages = rhi::EShaderStage::eCompute });
			desc.bindings.push_back({
				.binding = 2, .count = 1,
				.type = rhi::EDescriptorType::eStorageBufferReadWrite,
				.stages = rhi::EShaderStage::eCompute });

			cullLayout_ = rhi_->createBindGroupLayout(desc);

			rhi::PipelineLayoutDesc layoutDesc{};
			layoutDesc.bindGroups.push_back(cullLayout_);
			layoutDesc.pushConstantSize = sizeof(CullGpuConstants);

			cullPipelineLayout_ = rhi_->createPipelineLayout(layoutDesc);

			rhi::ShaderDesc csDesc{ rhi::EShaderType::eCompute, shaders.cull, shaders.cullSize, "CSMain" };

			rhi::ComputePipelineDesc pipelineDesc{};
			pipelineDesc.cs = rhi_->createShader(csDesc);
			pipelineDesc.layoutHandle = cullPipelineLayout_;

			cullPipeline_ = rhi_->createComputePipeline(pipelineDesc);

			// The reset shares the cull's layout: it touches one of the same bindings, and
			// one layout means one bind group serves both dispatches.
			rhi::ShaderDesc resetDesc{ rhi::EShaderType::eCompute, shaders.reset, shaders.resetSize, "CSMain" };

			rhi::ComputePipelineDesc resetPipelineDesc{};
			resetPipelineDesc.cs = rhi_->createShader(resetDesc);
			resetPipelineDesc.layoutHandle = cullPipelineLayout_;

			resetPipeline_ = rhi_->createComputePipeline(resetPipelineDesc);
		}

		// --- draw ------------------------------------------------------------------

		{
			// Set 2: the instance records, read by the vertex shader. Everything else the
			// blades sample -- shadows, SH, samplers -- arrives through the borrowed sets.
			rhi::BindGroupLayoutDesc desc{};
			desc.bindings.push_back({
				.binding = 0, .count = 1,
				.type = rhi::EDescriptorType::eStorageBuffer,
				.stages = rhi::EShaderStage::eVertex });

			layout_ = rhi_->createBindGroupLayout(desc);

			rhi::PipelineLayoutDesc layoutDesc{};
			layoutDesc.bindGroups.push_back(sceneLayout);
			layoutDesc.bindGroups.push_back(bindlessLayout);
			layoutDesc.bindGroups.push_back(layout_);
			layoutDesc.pushConstantSize = sizeof(GrassGpuConstants);

			pipelineLayout_ = rhi_->createPipelineLayout(layoutDesc);

			rhi::BindGroupDesc groupDesc{};
			groupDesc.layout = layout_;
			groupDesc.storageBuffers.push_back({
				.binding = 0, .buffer = instances_, .offset = 0,
				.stride = 16, .count = kMaxBladesPerSide * kMaxBladesPerSide * kFloat4sPerInstance });

			group_ = rhi_->createBindGroup(groupDesc);
		}

		rhi::ShaderDesc vsDesc{ rhi::EShaderType::eVertex, shaders.vs, shaders.vsSize, "VSMain" };
		rhi::ShaderDesc psDesc{ rhi::EShaderType::eFragment, shaders.ps, shaders.psSize, "PSMain" };

		rhi::GraphicsPipelineDesc pipelineDesc{};
		pipelineDesc.vs = rhi_->createShader(vsDesc);
		pipelineDesc.ps = rhi_->createShader(psDesc);
		pipelineDesc.layoutHandle = pipelineLayout_;

		// Opaque, depth-tested, depth-writing: the blades are geometry like any other,
		// which is what lets the clouds, the water and the fog treat them as scenery
		// without ever hearing about them.
		pipelineDesc.depth.depthTestEnable = true;
		pipelineDesc.depth.depthWriteEnable = true;
		pipelineDesc.depth.depthCompareOp = rhi::ECompareOp::eLessEqual;

		// Both faces: a blade is a ribbon, seen from either side.
		pipelineDesc.rasterizer.cullMode = rhi::ECullMode::eNone;

		pipelineDesc.colorFormats = colorFormats;
		pipelineDesc.depthFormat = depthFormat;

		pipeline_ = rhi_->createGraphicsPipeline(pipelineDesc);

		ready_ = pipeline_ != INVALID_HANDLE &&
			cullPipeline_ != INVALID_HANDLE && resetPipeline_ != INVALID_HANDLE;

		return ready_;
	}

	void GrassField::deinitialize()
	{
		if (rhi_)
		{
			if (instances_ != INVALID_HANDLE) rhi_->releaseBuffer(instances_);
			if (drawArgs_ != INVALID_HANDLE) rhi_->releaseBuffer(drawArgs_);
		}

		instances_ = INVALID_HANDLE;
		drawArgs_ = INVALID_HANDLE;

		ready_ = false;

		heightField_ = INVALID_HANDLE;
		group_ = INVALID_HANDLE;
		cullGroup_ = INVALID_HANDLE;

		rhi_.reset();
	}

	void GrassField::setHeightField(rhi::BufferHandle buffer, u32 resolution, f32 worldSize, f32 heightScale)
	{
		if (!ready_ || buffer == INVALID_HANDLE || resolution == 0)
			return;

		fieldResolution_ = resolution;
		worldSize_ = worldSize;
		heightScale_ = heightScale;

		const u32 count = resolution * resolution;

		if (cullGroup_ == INVALID_HANDLE)
		{
			rhi::BindGroupDesc desc{};
			desc.layout = cullLayout_;
			desc.storageBuffers.push_back({
				.binding = 0, .buffer = buffer, .offset = 0,
				.stride = sizeof(f32), .count = count });
			desc.storageBuffers.push_back({
				.binding = 1, .buffer = instances_, .offset = 0,
				.stride = 16, .count = kMaxBladesPerSide * kMaxBladesPerSide * kFloat4sPerInstance });
			desc.storageBuffers.push_back({
				.binding = 2, .buffer = drawArgs_, .offset = 0,
				.stride = sizeof(u32), .count = 4 });

			cullGroup_ = rhi_->createBindGroup(desc);
		}
		else
		{
			rhi_->updateBindGroupBuffer(cullGroup_, 0, buffer, 0, sizeof(f32), count);
		}

		heightField_ = buffer;
	}

	void GrassField::recordCull(
		rhi::CommandBufferHandle cmd,
		const GrassParams& params,
		const math::Vec3& cameraPosition,
		f32 rockHeight,
		f32 rockSlope,
		f32 waterLevel,
		const f32 planes[4][4])
	{
		if (!ready_ || cullGroup_ == INVALID_HANDLE)
			return;

		auto barrier = [&](rhi::BufferHandle buffer, rhi::EResourceState before, rhi::EResourceState after)
		{
			rhi::BufferBarrier b{};
			b.buffer = buffer;
			b.before = before;
			b.after = after;

			rhi_->bufferBarrier(cmd, b);
		};

		// Only the frame's first barrier per buffer names a previous state, and on the
		// first frame there is none to name, which eUndefined covers. Everything after
		// the reset is within this frame and always knows its states.
		const rhi::EResourceState argsPrevious =
			firstFrame_ ? rhi::EResourceState::eUndefined : rhi::EResourceState::eIndirectArgument;
		const rhi::EResourceState instancesPrevious =
			firstFrame_ ? rhi::EResourceState::eUndefined : rhi::EResourceState::eShaderRead;

		barrier(drawArgs_, argsPrevious, rhi::EResourceState::eShaderWrite);
		barrier(instances_, instancesPrevious, rhi::EResourceState::eShaderWrite);

		// Reset the arguments -- instanceCount back to zero -- then fence the write
		// against the cull's atomics. Same state on both sides, which the backends turn
		// into the UAV barrier it really is.
		rhi_->bindComputePipeline(cmd, resetPipeline_);
		rhi_->bindBindGroup(cmd, cullPipelineLayout_, 0, cullGroup_);
		rhi_->dispatch(cmd, 1, 1, 1);

		barrier(drawArgs_, rhi::EResourceState::eShaderWrite, rhi::EResourceState::eShaderWrite);

		CullGpuConstants constants{};

		std::memcpy(constants.planes, planes, sizeof(constants.planes));

		constants.cameraPosition[0] = cameraPosition.x;
		constants.cameraPosition[1] = cameraPosition.y;
		constants.cameraPosition[2] = cameraPosition.z;
		constants.radius = (std::max)(params.radius, 1.0f);

		constants.worldSize = worldSize_;
		constants.heightScale = heightScale_;
		constants.rockHeight = rockHeight;
		constants.rockSlope = rockSlope;

		constants.waterLevel = waterLevel;
		constants.density = params.density;
		constants.bladeHeight = params.bladeHeight;

		constants.fieldResolution = fieldResolution_;
		constants.bladesPerSide = (std::min)((std::max)(params.bladesPerSide, 2u), kMaxBladesPerSide);

		const u32 cells = constants.bladesPerSide * constants.bladesPerSide;

		rhi_->bindComputePipeline(cmd, cullPipeline_);
		rhi_->bindBindGroup(cmd, cullPipelineLayout_, 0, cullGroup_);
		rhi_->pushConstants(cmd, cullPipelineLayout_, &constants, sizeof(constants), 0);

		rhi_->dispatch(cmd, (cells + kCullTile - 1) / kCullTile, 1, 1);

		// Hand the results to the draw: the arguments to the front end, the records to
		// the vertex shader.
		barrier(drawArgs_, rhi::EResourceState::eShaderWrite, rhi::EResourceState::eIndirectArgument);
		barrier(instances_, rhi::EResourceState::eShaderWrite, rhi::EResourceState::eShaderRead);

		firstFrame_ = false;
	}

	void GrassField::recordDraw(
		rhi::CommandBufferHandle cmd,
		const GrassParams& params,
		rhi::BindGroupHandle sceneGroup,
		rhi::BindGroupHandle bindlessGroup)
	{
		if (!ready_ || firstFrame_ ||
			sceneGroup == INVALID_HANDLE || bindlessGroup == INVALID_HANDLE)
			return;

		GrassGpuConstants constants{};

		constants.rootColor[0] = params.rootColor.x;
		constants.rootColor[1] = params.rootColor.y;
		constants.rootColor[2] = params.rootColor.z;
		constants.bladeHeight = params.bladeHeight;

		constants.tipColor[0] = params.tipColor.x;
		constants.tipColor[1] = params.tipColor.y;
		constants.tipColor[2] = params.tipColor.z;
		constants.bladeWidth = params.bladeWidth;

		constants.time = time_;
		constants.windStrength = params.windStrength;

		rhi_->bindGraphicsPipeline(cmd, pipeline_);
		rhi_->bindBindGroup(cmd, pipelineLayout_, 0, sceneGroup);
		rhi_->bindBindGroup(cmd, pipelineLayout_, 1, bindlessGroup);
		rhi_->bindBindGroup(cmd, pipelineLayout_, 2, group_);
		rhi_->pushConstants(cmd, pipelineLayout_, &constants, sizeof(constants), 0);

		rhi_->drawIndirect(cmd, drawArgs_, 0);
	}
}
