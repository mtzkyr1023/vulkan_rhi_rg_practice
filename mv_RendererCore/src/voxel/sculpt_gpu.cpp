#include "voxel/sculpt_gpu.h"

#include <vector>

namespace mv::voxel
{
	namespace
	{
		// Must match SculptMcConstants in sculpt_mc.hlsl.
		struct SculptMcGpuConstants
		{
			f32 origin[3]{};
			f32 cellSize = 1.0f;

			u32 cells = 0;
			u32 maxVertices = 0;
			f32 pad[2]{};
		};

		// Must match SculptDrawConstants in sculpt_draw.hlsl.
		struct SculptDrawGpuConstants
		{
			u32 materialIndex = 0;
			f32 pad[3]{};
		};

		// Must match SculptBrushConstants in sculpt_brush.hlsl.
		struct SculptBrushGpuConstants
		{
			f32 center[3]{};
			f32 radius = 0.0f;

			f32 strength = 0.0f;
			u32 corners = 0;
			f32 pad[2]{};
		};

		// Must match SculptDeformConstants in sculpt_deform.hlsl.
		struct SculptDeformGpuConstants
		{
			f32 origin[3]{};
			f32 cellSize = 1.0f;

			u32 corners = 0;
			f32 time = 0.0f;
			f32 amplitude = 0.0f;
			f32 wavelength = 1.0f;

			f32 speed = 0.0f;
			f32 pad[3]{};
		};
	}

	bool SculptGpu::initialize(
		const std::shared_ptr<rhi::IRHI>& rhi,
		const Shaders& shaders,
		const std::vector<rhi::ETextureFormat>& colorFormats,
		rhi::ETextureFormat depthFormat,
		rhi::BindGroupLayoutHandle sceneLayout,
		rhi::BindGroupLayoutHandle bindlessLayout,
		const u16* edgeTable,
		const s8* triTable,
		u32 maxCorners)
	{
		if (!rhi || shaders.mc == nullptr || shaders.reset == nullptr ||
			shaders.deform == nullptr || shaders.brush == nullptr ||
			shaders.vs == nullptr || shaders.ps == nullptr ||
			edgeTable == nullptr || triTable == nullptr || maxCorners == 0)
			return false;

		if (sceneLayout == INVALID_HANDLE || bindlessLayout == INVALID_HANDLE)
			return false;

		rhi_ = rhi;
		maxCorners_ = maxCorners;

		// --- the shared tables -----------------------------------------------------

		{
			std::vector<s32> tables(256 + 256 * 16);

			for (u32 i = 0; i < 256; i++)
				tables[i] = (s32)edgeTable[i];

			for (u32 i = 0; i < 256 * 16; i++)
				tables[256 + i] = (s32)triTable[i];

			rhi::BufferDesc desc{};
			desc.size = tables.size() * sizeof(s32);
			desc.usage = rhi::EBufferUsage::eStorage | rhi::EBufferUsage::eTransferDst;
			desc.memoryType = rhi::EMemoryType::eDeviceLocalBuffer;

			tableBuffer_ = rhi_->createBuffer(desc);
			rhi_->uploadBuffer(tableBuffer_, tables.data(), desc.size);
		}

		// --- march + reset ---------------------------------------------------------

		{
			rhi::BindGroupLayoutDesc desc{};
			desc.bindings.push_back({
				.binding = 0, .count = 1,
				.type = rhi::EDescriptorType::eStorageBuffer,
				.stages = rhi::EShaderStage::eCompute });
			desc.bindings.push_back({
				.binding = 1, .count = 1,
				.type = rhi::EDescriptorType::eStorageBuffer,
				.stages = rhi::EShaderStage::eCompute });
			desc.bindings.push_back({
				.binding = 2, .count = 1,
				.type = rhi::EDescriptorType::eStorageBufferReadWrite,
				.stages = rhi::EShaderStage::eCompute });
			desc.bindings.push_back({
				.binding = 3, .count = 1,
				.type = rhi::EDescriptorType::eStorageBufferReadWrite,
				.stages = rhi::EShaderStage::eCompute });

			computeLayout_ = rhi_->createBindGroupLayout(desc);

			rhi::PipelineLayoutDesc layoutDesc{};
			layoutDesc.bindGroups.push_back(computeLayout_);
			layoutDesc.pushConstantSize = sizeof(SculptMcGpuConstants);

			computePipelineLayout_ = rhi_->createPipelineLayout(layoutDesc);

			rhi::ShaderDesc mcDesc{ rhi::EShaderType::eCompute, shaders.mc, shaders.mcSize, "CSMain" };

			rhi::ComputePipelineDesc mcPipelineDesc{};
			mcPipelineDesc.cs = rhi_->createShader(mcDesc);
			mcPipelineDesc.layoutHandle = computePipelineLayout_;

			mcPipeline_ = rhi_->createComputePipeline(mcPipelineDesc);

			rhi::ShaderDesc resetDesc{ rhi::EShaderType::eCompute, shaders.reset, shaders.resetSize, "CSMain" };

			rhi::ComputePipelineDesc resetPipelineDesc{};
			resetPipelineDesc.cs = rhi_->createShader(resetDesc);
			resetPipelineDesc.layoutHandle = computePipelineLayout_;

			resetPipeline_ = rhi_->createComputePipeline(resetPipelineDesc);
		}

		// --- deform ----------------------------------------------------------------

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

			deformLayout_ = rhi_->createBindGroupLayout(desc);

			rhi::PipelineLayoutDesc layoutDesc{};
			layoutDesc.bindGroups.push_back(deformLayout_);
			layoutDesc.pushConstantSize = sizeof(SculptDeformGpuConstants);

			deformPipelineLayout_ = rhi_->createPipelineLayout(layoutDesc);

			rhi::ShaderDesc csDesc{ rhi::EShaderType::eCompute, shaders.deform, shaders.deformSize, "CSMain" };

			rhi::ComputePipelineDesc pipelineDesc{};
			pipelineDesc.cs = rhi_->createShader(csDesc);
			pipelineDesc.layoutHandle = deformPipelineLayout_;

			deformPipeline_ = rhi_->createComputePipeline(pipelineDesc);
		}

		// --- brush -----------------------------------------------------------------

		{
			rhi::BindGroupLayoutDesc desc{};
			desc.bindings.push_back({
				.binding = 0, .count = 1,
				.type = rhi::EDescriptorType::eStorageBufferReadWrite,
				.stages = rhi::EShaderStage::eCompute });

			brushLayout_ = rhi_->createBindGroupLayout(desc);

			rhi::PipelineLayoutDesc layoutDesc{};
			layoutDesc.bindGroups.push_back(brushLayout_);
			layoutDesc.pushConstantSize = sizeof(SculptBrushGpuConstants);

			brushPipelineLayout_ = rhi_->createPipelineLayout(layoutDesc);

			rhi::ShaderDesc csDesc{ rhi::EShaderType::eCompute, shaders.brush, shaders.brushSize, "CSMain" };

			rhi::ComputePipelineDesc pipelineDesc{};
			pipelineDesc.cs = rhi_->createShader(csDesc);
			pipelineDesc.layoutHandle = brushPipelineLayout_;

			brushPipeline_ = rhi_->createComputePipeline(pipelineDesc);
		}

		// --- draw ------------------------------------------------------------------

		{
			rhi::BindGroupLayoutDesc desc{};
			desc.bindings.push_back({
				.binding = 0, .count = 1,
				.type = rhi::EDescriptorType::eStorageBuffer,
				.stages = rhi::EShaderStage::eVertex });

			drawLayout_ = rhi_->createBindGroupLayout(desc);

			rhi::PipelineLayoutDesc layoutDesc{};
			layoutDesc.bindGroups.push_back(sceneLayout);
			layoutDesc.bindGroups.push_back(bindlessLayout);
			layoutDesc.bindGroups.push_back(drawLayout_);
			layoutDesc.pushConstantSize = sizeof(SculptDrawGpuConstants);

			drawPipelineLayout_ = rhi_->createPipelineLayout(layoutDesc);

			rhi::ShaderDesc vsDesc{ rhi::EShaderType::eVertex, shaders.vs, shaders.vsSize, "VSMain" };
			rhi::ShaderDesc psDesc{ rhi::EShaderType::eFragment, shaders.ps, shaders.psSize, "PSMain" };

			rhi::GraphicsPipelineDesc pipelineDesc{};
			pipelineDesc.vs = rhi_->createShader(vsDesc);
			pipelineDesc.ps = rhi_->createShader(psDesc);
			pipelineDesc.layoutHandle = drawPipelineLayout_;

			pipelineDesc.depth.depthTestEnable = true;
			pipelineDesc.depth.depthWriteEnable = true;
			pipelineDesc.depth.depthCompareOp = rhi::ECompareOp::eLessEqual;

			pipelineDesc.rasterizer.cullMode = rhi::ECullMode::eNone;

			pipelineDesc.colorFormats = colorFormats;
			pipelineDesc.depthFormat = depthFormat;

			drawPipeline_ = rhi_->createGraphicsPipeline(pipelineDesc);
		}

		ready_ = mcPipeline_ != INVALID_HANDLE && resetPipeline_ != INVALID_HANDLE &&
			deformPipeline_ != INVALID_HANDLE && brushPipeline_ != INVALID_HANDLE &&
			drawPipeline_ != INVALID_HANDLE;

		return ready_;
	}

	u32 SculptGpu::addChunk()
	{
		Chunk chunk{};

		{
			rhi::BufferDesc desc{};
			desc.size = (u64)maxCorners_ * sizeof(f32);
			desc.usage = rhi::EBufferUsage::eStorage | rhi::EBufferUsage::eStorageReadWrite |
				rhi::EBufferUsage::eTransferDst;
			desc.memoryType = rhi::EMemoryType::eDeviceLocalBuffer;

			chunk.density = rhi_->createBuffer(desc);
			chunk.animated = rhi_->createBuffer(desc);
		}

		{
			rhi::BufferDesc desc{};
			desc.size = (u64)kChunkMaxVertices * 2 * 16;
			desc.usage = rhi::EBufferUsage::eStorage | rhi::EBufferUsage::eStorageReadWrite;
			desc.memoryType = rhi::EMemoryType::eDeviceLocalBuffer;

			chunk.vertices = rhi_->createBuffer(desc);
		}

		{
			rhi::BufferDesc desc{};
			desc.size = 4 * sizeof(u32);
			desc.usage = rhi::EBufferUsage::eStorage | rhi::EBufferUsage::eStorageReadWrite |
				rhi::EBufferUsage::eIndirectArgs | rhi::EBufferUsage::eTransferDst;
			desc.memoryType = rhi::EMemoryType::eDeviceLocalBuffer;

			chunk.drawArgs = rhi_->createBuffer(desc);
		}

		{
			rhi::BindGroupDesc desc{};
			desc.layout = computeLayout_;
			desc.storageBuffers.push_back({
				.binding = 0, .buffer = chunk.density, .offset = 0,
				.stride = sizeof(f32), .count = maxCorners_ });
			desc.storageBuffers.push_back({
				.binding = 1, .buffer = tableBuffer_, .offset = 0,
				.stride = sizeof(s32), .count = 256 + 256 * 16 });
			desc.storageBuffers.push_back({
				.binding = 2, .buffer = chunk.vertices, .offset = 0,
				.stride = 16, .count = kChunkMaxVertices * 2 });
			desc.storageBuffers.push_back({
				.binding = 3, .buffer = chunk.drawArgs, .offset = 0,
				.stride = sizeof(u32), .count = 4 });

			chunk.computeGroup = rhi_->createBindGroup(desc);

			rhi::BindGroupDesc animatedDesc = desc;
			animatedDesc.storageBuffers[0].buffer = chunk.animated;

			chunk.computeGroupAnimated = rhi_->createBindGroup(animatedDesc);
		}

		{
			rhi::BindGroupDesc desc{};
			desc.layout = deformLayout_;
			desc.storageBuffers.push_back({
				.binding = 0, .buffer = chunk.density, .offset = 0,
				.stride = sizeof(f32), .count = maxCorners_ });
			desc.storageBuffers.push_back({
				.binding = 1, .buffer = chunk.animated, .offset = 0,
				.stride = sizeof(f32), .count = maxCorners_ });

			chunk.deformGroup = rhi_->createBindGroup(desc);
		}

		{
			rhi::BindGroupDesc desc{};
			desc.layout = brushLayout_;
			desc.storageBuffers.push_back({
				.binding = 0, .buffer = chunk.density, .offset = 0,
				.stride = sizeof(f32), .count = maxCorners_ });

			chunk.brushGroup = rhi_->createBindGroup(desc);
		}

		{
			rhi::BindGroupDesc desc{};
			desc.layout = drawLayout_;
			desc.storageBuffers.push_back({
				.binding = 0, .buffer = chunk.vertices, .offset = 0,
				.stride = 16, .count = kChunkMaxVertices * 2 });

			chunk.drawGroup = rhi_->createBindGroup(desc);
		}

		chunks_.push_back(chunk);

		return (u32)chunks_.size() - 1;
	}

	void SculptGpu::deinitialize()
	{
		if (rhi_)
		{
			for (Chunk& chunk : chunks_)
			{
				if (chunk.density != INVALID_HANDLE) rhi_->releaseBuffer(chunk.density);
				if (chunk.animated != INVALID_HANDLE) rhi_->releaseBuffer(chunk.animated);
				if (chunk.vertices != INVALID_HANDLE) rhi_->releaseBuffer(chunk.vertices);
				if (chunk.drawArgs != INVALID_HANDLE) rhi_->releaseBuffer(chunk.drawArgs);
			}

			if (tableBuffer_ != INVALID_HANDLE) rhi_->releaseBuffer(tableBuffer_);
		}

		chunks_.clear();
		tableBuffer_ = INVALID_HANDLE;

		ready_ = false;
		rhi_.reset();
	}

	void SculptGpu::updateDensity(
		u32 chunkIndex,
		const f32* density, u32 cornerCount,
		const math::Vec3& origin, f32 cellSize, u32 cells)
	{
		if (!ready_ || chunkIndex >= chunks_.size() ||
			density == nullptr || cornerCount == 0 || cornerCount > maxCorners_)
			return;

		Chunk& chunk = chunks_[chunkIndex];

		rhi_->uploadBuffer(chunk.density, density, (u64)cornerCount * sizeof(f32));

		chunk.origin = origin;
		chunk.cellSize = cellSize;
		chunk.cells = cells;
		chunk.remeshPending = true;
	}

	void SculptGpu::queueBrush(
		u32 chunkIndex, const math::Vec3& worldPosition, f32 radius, f32 strength)
	{
		if (!ready_ || chunkIndex >= chunks_.size())
			return;

		Chunk& chunk = chunks_[chunkIndex];

		if (chunk.cells == 0)
			return;

		BrushOp op{};
		op.gridCenter = {
			(worldPosition.x - chunk.origin.x) / chunk.cellSize,
			(worldPosition.y - chunk.origin.y) / chunk.cellSize,
			(worldPosition.z - chunk.origin.z) / chunk.cellSize };
		op.gridRadius = radius / chunk.cellSize;
		op.strength = strength;

		chunk.brushOps.push_back(op);
		chunk.remeshPending = true;
	}

	void SculptGpu::queueRemesh(u32 chunkIndex)
	{
		if (chunkIndex < chunks_.size())
			chunks_[chunkIndex].remeshPending = true;
	}

	void SculptGpu::queueRemeshAll()
	{
		for (Chunk& chunk : chunks_)
		{
			if (chunk.cells != 0)
				chunk.remeshPending = true;
		}
	}

	bool SculptGpu::anyPending() const
	{
		for (const Chunk& chunk : chunks_)
		{
			if (chunk.remeshPending)
				return true;
		}

		return false;
	}

	void SculptGpu::recordBrushes(rhi::CommandBufferHandle cmd, Chunk& chunk)
	{
		if (chunk.brushOps.empty())
			return;

		auto barrier = [&](rhi::BufferHandle buffer, rhi::EResourceState before, rhi::EResourceState after)
		{
			rhi::BufferBarrier b{};
			b.buffer = buffer;
			b.before = before;
			b.after = after;

			rhi_->bufferBarrier(cmd, b);
		};

		barrier(chunk.density, rhi::EResourceState::eShaderRead, rhi::EResourceState::eShaderWrite);

		rhi_->bindComputePipeline(cmd, brushPipeline_);
		rhi_->bindBindGroup(cmd, brushPipelineLayout_, 0, chunk.brushGroup);

		const u32 corners = chunk.cells + 1;
		const u32 groups = (corners + 3) / 4;

		for (size_t i = 0; i < chunk.brushOps.size(); i++)
		{
			const BrushOp& op = chunk.brushOps[i];

			SculptBrushGpuConstants constants{};
			constants.center[0] = op.gridCenter.x;
			constants.center[1] = op.gridCenter.y;
			constants.center[2] = op.gridCenter.z;
			constants.radius = op.gridRadius;
			constants.strength = op.strength;
			constants.corners = corners;

			rhi_->pushConstants(cmd, brushPipelineLayout_, &constants, sizeof(constants), 0);
			rhi_->dispatch(cmd, groups, groups, groups);

			if (i + 1 < chunk.brushOps.size())
				barrier(chunk.density, rhi::EResourceState::eShaderWrite, rhi::EResourceState::eShaderWrite);
		}

		barrier(chunk.density, rhi::EResourceState::eShaderWrite, rhi::EResourceState::eShaderRead);

		chunk.brushOps.clear();
	}

	void SculptGpu::recordMarch(rhi::CommandBufferHandle cmd, Chunk& chunk, rhi::BindGroupHandle group)
	{
		auto barrier = [&](rhi::BufferHandle buffer, rhi::EResourceState before, rhi::EResourceState after)
		{
			rhi::BufferBarrier b{};
			b.buffer = buffer;
			b.before = before;
			b.after = after;

			rhi_->bufferBarrier(cmd, b);
		};

		const rhi::EResourceState argsPrevious =
			chunk.firstFrame ? rhi::EResourceState::eUndefined : rhi::EResourceState::eIndirectArgument;
		const rhi::EResourceState verticesPrevious =
			chunk.firstFrame ? rhi::EResourceState::eUndefined : rhi::EResourceState::eShaderRead;

		barrier(chunk.drawArgs, argsPrevious, rhi::EResourceState::eShaderWrite);
		barrier(chunk.vertices, verticesPrevious, rhi::EResourceState::eShaderWrite);

		rhi_->bindComputePipeline(cmd, resetPipeline_);
		rhi_->bindBindGroup(cmd, computePipelineLayout_, 0, group);
		rhi_->dispatch(cmd, 1, 1, 1);

		barrier(chunk.drawArgs, rhi::EResourceState::eShaderWrite, rhi::EResourceState::eShaderWrite);

		SculptMcGpuConstants constants{};
		constants.origin[0] = chunk.origin.x;
		constants.origin[1] = chunk.origin.y;
		constants.origin[2] = chunk.origin.z;
		constants.cellSize = chunk.cellSize;
		constants.cells = chunk.cells;
		constants.maxVertices = kChunkMaxVertices;

		rhi_->bindComputePipeline(cmd, mcPipeline_);
		rhi_->bindBindGroup(cmd, computePipelineLayout_, 0, group);
		rhi_->pushConstants(cmd, computePipelineLayout_, &constants, sizeof(constants), 0);

		const u32 groups = (chunk.cells + 3) / 4;
		rhi_->dispatch(cmd, groups, groups, groups);

		barrier(chunk.drawArgs, rhi::EResourceState::eShaderWrite, rhi::EResourceState::eIndirectArgument);
		barrier(chunk.vertices, rhi::EResourceState::eShaderWrite, rhi::EResourceState::eShaderRead);

		chunk.firstFrame = false;
		chunk.meshed = true;
	}

	void SculptGpu::recordDeform(
		rhi::CommandBufferHandle cmd, Chunk& chunk,
		f32 time, f32 amplitude, f32 wavelength, f32 speed)
	{
		auto barrier = [&](rhi::BufferHandle buffer, rhi::EResourceState before, rhi::EResourceState after)
		{
			rhi::BufferBarrier b{};
			b.buffer = buffer;
			b.before = before;
			b.after = after;

			rhi_->bufferBarrier(cmd, b);
		};

		const rhi::EResourceState animatedPrevious =
			chunk.firstAnimate ? rhi::EResourceState::eUndefined : rhi::EResourceState::eShaderRead;

		barrier(chunk.animated, animatedPrevious, rhi::EResourceState::eShaderWrite);

		SculptDeformGpuConstants constants{};
		constants.origin[0] = chunk.origin.x;
		constants.origin[1] = chunk.origin.y;
		constants.origin[2] = chunk.origin.z;
		constants.cellSize = chunk.cellSize;
		constants.corners = chunk.cells + 1;
		constants.time = time;
		constants.amplitude = amplitude;
		constants.wavelength = wavelength;
		constants.speed = speed;

		rhi_->bindComputePipeline(cmd, deformPipeline_);
		rhi_->bindBindGroup(cmd, deformPipelineLayout_, 0, chunk.deformGroup);
		rhi_->pushConstants(cmd, deformPipelineLayout_, &constants, sizeof(constants), 0);

		const u32 groups = (chunk.cells + 1 + 3) / 4;
		rhi_->dispatch(cmd, groups, groups, groups);

		barrier(chunk.animated, rhi::EResourceState::eShaderWrite, rhi::EResourceState::eShaderRead);

		chunk.firstAnimate = false;
	}

	void SculptGpu::recordRemesh(rhi::CommandBufferHandle cmd)
	{
		if (!ready_)
			return;

		for (Chunk& chunk : chunks_)
		{
			if (!chunk.remeshPending || chunk.cells == 0)
				continue;

			recordBrushes(cmd, chunk);
			recordMarch(cmd, chunk, chunk.computeGroup);

			chunk.remeshPending = false;
		}
	}

	void SculptGpu::recordAnimate(
		rhi::CommandBufferHandle cmd,
		f32 time, f32 amplitude, f32 wavelength, f32 speed)
	{
		if (!ready_)
			return;

		for (Chunk& chunk : chunks_)
		{
			if (chunk.cells == 0)
				continue;

			recordBrushes(cmd, chunk);
			recordDeform(cmd, chunk, time, amplitude, wavelength, speed);
			recordMarch(cmd, chunk, chunk.computeGroupAnimated);

			chunk.remeshPending = false;
		}
	}

	void SculptGpu::recordDraw(
		rhi::CommandBufferHandle cmd,
		rhi::BindGroupHandle sceneGroup,
		rhi::BindGroupHandle bindlessGroup)
	{
		if (!ready_ || material_ == INVALID_HANDLE ||
			sceneGroup == INVALID_HANDLE || bindlessGroup == INVALID_HANDLE)
			return;

		bool bound = false;

		for (Chunk& chunk : chunks_)
		{
			if (!chunk.meshed)
				continue;

			if (!bound)
			{
				SculptDrawGpuConstants constants{};
				constants.materialIndex = material_;

				rhi_->bindGraphicsPipeline(cmd, drawPipeline_);
				rhi_->bindBindGroup(cmd, drawPipelineLayout_, 0, sceneGroup);
				rhi_->bindBindGroup(cmd, drawPipelineLayout_, 1, bindlessGroup);
				rhi_->pushConstants(cmd, drawPipelineLayout_, &constants, sizeof(constants), 0);

				bound = true;
			}

			rhi_->bindBindGroup(cmd, drawPipelineLayout_, 2, chunk.drawGroup);
			rhi_->drawIndirect(cmd, chunk.drawArgs, 0);
		}
	}
}
