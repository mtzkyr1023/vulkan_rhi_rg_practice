#include "compute/terrain_builder.h"

#include "terrain/terrain.h"

#include <algorithm>
#include <cstring>

namespace mv::compute
{
	namespace
	{
		// Must match the [numthreads] in the terrain shaders.
		constexpr u32 kTileSize = 8;
		constexpr u32 kLinearGroupSize = 64;

		// Must match NoiseParams in noise.hlsli.
		struct GpuNoiseParams
		{
			u32 basis;
			u32 fractal;
			f32 frequency;
			u32 octaves;

			f32 lacunarity;
			f32 gain;
			u32 seed;
			f32 warpStrength;

			f32 warpFrequency;
			u32 tileable;
			u32 pad[2]{};
		};

		// Must match TerrainConstants in terrain.hlsli.
		struct GpuTerrainConstants
		{
			GpuNoiseParams noise;

			u32 fieldSize;
			u32 resolution;
			u32 textureSize;
			f32 worldSize;

			f32 heightScale;
			f32 rockHeight;
			f32 snowHeight;
			f32 rockSlope;

			f32 waterHeight;
			u32 pad[3]{};
		};

		GpuNoiseParams toGpu(const noise::NoiseDesc& desc)
		{
			GpuNoiseParams gpu{};
			gpu.basis = (u32)desc.basis;
			gpu.fractal = (u32)desc.fractal;
			gpu.frequency = desc.frequency;
			gpu.octaves = desc.octaves;
			gpu.lacunarity = desc.lacunarity;
			gpu.gain = desc.gain;
			gpu.seed = desc.seed;
			gpu.warpStrength = desc.warpStrength;
			gpu.warpFrequency = desc.warpFrequency;
			gpu.tileable = desc.tileable ? 1u : 0u;

			return gpu;
		}

		u32 tileCount(u32 extent)
		{
			return (extent + kTileSize - 1) / kTileSize;
		}
	}

	rhi::PipelineHandle TerrainBuilder::createPipeline(
		rhi::BindGroupLayoutHandle layout,
		rhi::PipelineLayoutHandle& outPipelineLayout,
		const u32* bytecode,
		u32 size)
	{
		if (bytecode == nullptr || size == 0)
			return INVALID_HANDLE;

		rhi::PipelineLayoutDesc pipelineLayoutDesc{};
		pipelineLayoutDesc.bindGroups.push_back(layout);
		pipelineLayoutDesc.pushConstantSize = sizeof(GpuTerrainConstants);

		outPipelineLayout = rhi_->createPipelineLayout(pipelineLayoutDesc);

		rhi::ShaderDesc shaderDesc{ rhi::EShaderType::eCompute, bytecode, size, "CSMain" };

		rhi::ComputePipelineDesc pipelineDesc{};
		pipelineDesc.cs = rhi_->createShader(shaderDesc);
		pipelineDesc.layoutHandle = outPipelineLayout;

		return rhi_->createComputePipeline(pipelineDesc);
	}

	bool TerrainBuilder::initialize(const std::shared_ptr<rhi::IRHI>& rhi, const Shaders& shaders)
	{
		if (!rhi)
			return false;

		rhi_ = rhi;

		auto readWriteBuffer = [](u32 binding)
		{
			return rhi::BindingDesc{
				.binding = binding, .count = 1,
				.type = rhi::EDescriptorType::eStorageBufferReadWrite,
				.stages = rhi::EShaderStage::eCompute };
		};

		auto readBuffer = [](u32 binding)
		{
			return rhi::BindingDesc{
				.binding = binding, .count = 1,
				.type = rhi::EDescriptorType::eStorageBuffer,
				.stages = rhi::EShaderStage::eCompute };
		};

		auto storageImage = [](u32 binding)
		{
			return rhi::BindingDesc{
				.binding = binding, .count = 1,
				.type = rhi::EDescriptorType::eStorageImage,
				.stages = rhi::EShaderStage::eCompute };
		};

		{
			rhi::BindGroupLayoutDesc desc{};
			desc.bindings.push_back(readWriteBuffer(0));  // heights
			desc.bindings.push_back(readWriteBuffer(1));  // range
			heightLayout_ = rhi_->createBindGroupLayout(desc);
		}
		{
			rhi::BindGroupLayoutDesc desc{};
			desc.bindings.push_back(readWriteBuffer(0));  // heights
			desc.bindings.push_back(readBuffer(1));       // range
			normaliseLayout_ = rhi_->createBindGroupLayout(desc);
		}
		{
			rhi::BindGroupLayoutDesc desc{};
			desc.bindings.push_back(readBuffer(0));       // heights
			desc.bindings.push_back(readWriteBuffer(1));  // vertices
			desc.bindings.push_back(readWriteBuffer(2));  // indices
			meshLayout_ = rhi_->createBindGroupLayout(desc);
		}
		{
			rhi::BindGroupLayoutDesc desc{};
			desc.bindings.push_back(readBuffer(0));       // heights
			desc.bindings.push_back(storageImage(1));     // base colour
			desc.bindings.push_back(storageImage(2));     // normal
			desc.bindings.push_back(storageImage(3));     // roughness
			bakeLayout_ = rhi_->createBindGroupLayout(desc);
		}

		heightPipeline_ = createPipeline(heightLayout_, heightPipelineLayout_, shaders.height, shaders.heightSize);
		normalisePipeline_ = createPipeline(normaliseLayout_, normalisePipelineLayout_, shaders.normalise, shaders.normaliseSize);
		meshPipeline_ = createPipeline(meshLayout_, meshPipelineLayout_, shaders.mesh, shaders.meshSize);
		bakePipeline_ = createPipeline(bakeLayout_, bakePipelineLayout_, shaders.bake, shaders.bakeSize);

		rangeClear_.initialize(rhi_, shaders.fill, shaders.fillSize);

		ready_ =
			heightPipeline_ != INVALID_HANDLE &&
			normalisePipeline_ != INVALID_HANDLE &&
			meshPipeline_ != INVALID_HANDLE &&
			bakePipeline_ != INVALID_HANDLE &&
			rangeClear_.isReady();

		return ready_;
	}

	void TerrainBuilder::deinitialize()
	{
		rangeClear_.deinitialize();

		if (rhi_)
		{
			if (heightBuffer_ != INVALID_HANDLE) rhi_->releaseBuffer(heightBuffer_);
			if (rangeBuffer_ != INVALID_HANDLE) rhi_->releaseBuffer(rangeBuffer_);
			if (heightReadback_ != INVALID_HANDLE) rhi_->releaseBuffer(heightReadback_);
		}

		heightBuffer_ = INVALID_HANDLE;
		rangeBuffer_ = INVALID_HANDLE;
		heightReadback_ = INVALID_HANDLE;
		fieldCapacity_ = 0;

		ready_ = false;
		rhi_.reset();
	}

	void TerrainBuilder::ensureFieldBuffers(u32 fieldSize)
	{
		if (fieldSize <= fieldCapacity_ && heightBuffer_ != INVALID_HANDLE)
			return;

		if (heightBuffer_ != INVALID_HANDLE) rhi_->releaseBuffer(heightBuffer_);
		if (heightReadback_ != INVALID_HANDLE) rhi_->releaseBuffer(heightReadback_);

		const u64 size = (u64)fieldSize * fieldSize * sizeof(f32);

		rhi::BufferDesc heightDesc{};
		heightDesc.size = size;
		heightDesc.usage = rhi::EBufferUsage::eStorageReadWrite | rhi::EBufferUsage::eTransferSrc;
		heightDesc.memoryType = rhi::EMemoryType::eDeviceLocalBuffer;

		heightBuffer_ = rhi_->createBuffer(heightDesc);

		rhi::BufferDesc readbackDesc{};
		readbackDesc.size = size;
		readbackDesc.usage = rhi::EBufferUsage::eTransferDst;
		readbackDesc.memoryType = rhi::EMemoryType::eReadback;

		heightReadback_ = rhi_->createBuffer(readbackDesc);

		fieldCapacity_ = fieldSize;

		// Two slots, and only ever the same two.
		if (rangeBuffer_ == INVALID_HANDLE)
		{
			rhi::BufferDesc rangeDesc{};
			rangeDesc.size = 2 * sizeof(u32);
			rangeDesc.usage = rhi::EBufferUsage::eStorageReadWrite;
			rangeDesc.memoryType = rhi::EMemoryType::eDeviceLocalBuffer;

			rangeBuffer_ = rhi_->createBuffer(rangeDesc);
			rangeClear_.setTarget(rangeBuffer_, 2);
		}
	}

	void TerrainBuilder::build(
		const noise::NoiseDesc& noiseDesc,
		const terrain::TerrainDesc& desc,
		u32 fieldSize,
		const Output& output,
		std::vector<f32>& outHeights)
	{
		if (!ready_)
			return;

		ensureFieldBuffers(fieldSize);

		GpuTerrainConstants constants{};
		constants.noise = toGpu(noiseDesc);
		constants.fieldSize = fieldSize;
		constants.resolution = desc.resolution;
		constants.textureSize = desc.textureSize;
		constants.worldSize = desc.worldSize;
		constants.heightScale = desc.heightScale;
		constants.rockHeight = desc.rockHeight;
		constants.snowHeight = desc.snowHeight;
		constants.rockSlope = desc.rockSlope;
		constants.waterHeight = desc.waterHeight;

		const u32 quads = std::max(1u, desc.resolution - 1);
		const u32 vertexCount = desc.resolution * desc.resolution;
		const u32 indexCount = quads * quads * 6;
		const u32 fieldCount = fieldSize * fieldSize;

		// The bind groups name buffers that are rebuilt whenever the resolution changes, so
		// they are created per build rather than kept. They cost nothing next to the
		// dispatches, and the alternative is four groups whose descriptors have to be
		// re-pointed in exactly the right order.
		rhi::BindGroupDesc heightGroupDesc{};
		heightGroupDesc.layout = heightLayout_;
		heightGroupDesc.storageBuffers.push_back({ .binding = 0, .buffer = heightBuffer_, .offset = 0, .stride = sizeof(f32), .count = fieldCount });
		heightGroupDesc.storageBuffers.push_back({ .binding = 1, .buffer = rangeBuffer_, .offset = 0, .stride = sizeof(u32), .count = 2 });
		const rhi::BindGroupHandle heightGroup = rhi_->createBindGroup(heightGroupDesc);

		rhi::BindGroupDesc normaliseGroupDesc{};
		normaliseGroupDesc.layout = normaliseLayout_;
		normaliseGroupDesc.storageBuffers.push_back({ .binding = 0, .buffer = heightBuffer_, .offset = 0, .stride = sizeof(f32), .count = fieldCount });
		normaliseGroupDesc.storageBuffers.push_back({ .binding = 1, .buffer = rangeBuffer_, .offset = 0, .stride = sizeof(u32), .count = 2 });
		const rhi::BindGroupHandle normaliseGroup = rhi_->createBindGroup(normaliseGroupDesc);

		rhi::BindGroupDesc meshGroupDesc{};
		meshGroupDesc.layout = meshLayout_;
		meshGroupDesc.storageBuffers.push_back({ .binding = 0, .buffer = heightBuffer_, .offset = 0, .stride = sizeof(f32), .count = fieldCount });
		meshGroupDesc.storageBuffers.push_back({ .binding = 1, .buffer = output.vertexBuffer, .offset = 0, .stride = 32, .count = vertexCount });
		meshGroupDesc.storageBuffers.push_back({ .binding = 2, .buffer = output.indexBuffer, .offset = 0, .stride = sizeof(u32), .count = indexCount });
		const rhi::BindGroupHandle meshGroup = rhi_->createBindGroup(meshGroupDesc);

		rhi::BindGroupDesc bakeGroupDesc{};
		bakeGroupDesc.layout = bakeLayout_;
		bakeGroupDesc.storageBuffers.push_back({ .binding = 0, .buffer = heightBuffer_, .offset = 0, .stride = sizeof(f32), .count = fieldCount });
		bakeGroupDesc.storageTextures.push_back({ .binding = 1, .texture = output.baseColor, .arrayIndex = 0, .mipLevel = 0 });
		bakeGroupDesc.storageTextures.push_back({ .binding = 2, .texture = output.normal, .arrayIndex = 0, .mipLevel = 0 });
		bakeGroupDesc.storageTextures.push_back({ .binding = 3, .texture = output.roughness, .arrayIndex = 0, .mipLevel = 0 });
		const rhi::BindGroupHandle bakeGroup = rhi_->createBindGroup(bakeGroupDesc);

		const rhi::CommandBufferHandle cmd = rhi_->beginImmediateCommands();

		auto bufferBarrier = [&](rhi::BufferHandle buffer)
		{
			rhi::BufferBarrier barrier{};
			barrier.buffer = buffer;
			barrier.before = rhi::EResourceState::eShaderWrite;
			barrier.after = rhi::EResourceState::eShaderWrite;

			rhi_->bufferBarrier(cmd, barrier);
		};

		// The three maps have to be writable before the bake and readable after it.
		auto imageBarrier = [&](rhi::TextureHandle texture, rhi::EResourceState before, rhi::EResourceState after)
		{
			rhi::TextureBarrier barrier{};
			barrier.texture = texture;
			barrier.before = before;
			barrier.after = after;

			rhi_->textureBarrier(cmd, barrier);
		};

		// 1. The reduction accumulates into the range buffer, so it starts at the maximum.
		rangeClear_.record(cmd, 0xFFFFFFFFu);

		// 2. Evaluate the field.
		rhi_->bindComputePipeline(cmd, heightPipeline_);
		rhi_->bindBindGroup(cmd, heightPipelineLayout_, 0, heightGroup);
		rhi_->pushConstants(cmd, heightPipelineLayout_, &constants, sizeof(constants), 0);
		rhi_->dispatch(cmd, tileCount(fieldSize), tileCount(fieldSize), 1);

		bufferBarrier(heightBuffer_);
		bufferBarrier(rangeBuffer_);

		// 3. Rescale it, now that both extremes are known.
		rhi_->bindComputePipeline(cmd, normalisePipeline_);
		rhi_->bindBindGroup(cmd, normalisePipelineLayout_, 0, normaliseGroup);
		rhi_->pushConstants(cmd, normalisePipelineLayout_, &constants, sizeof(constants), 0);
		rhi_->dispatch(cmd, (fieldCount + kLinearGroupSize - 1) / kLinearGroupSize, 1, 1);

		bufferBarrier(heightBuffer_);

		// 4. Vertices and indices.
		rhi_->bindComputePipeline(cmd, meshPipeline_);
		rhi_->bindBindGroup(cmd, meshPipelineLayout_, 0, meshGroup);
		rhi_->pushConstants(cmd, meshPipelineLayout_, &constants, sizeof(constants), 0);
		rhi_->dispatch(cmd, tileCount(desc.resolution), tileCount(desc.resolution), 1);

		// 5. The three maps. From eUndefined, because nothing has uploaded them: they were
		// created for this bake and go straight from allocation to being written, and a
		// recycled handle could be resting in anything.
		imageBarrier(output.baseColor, rhi::EResourceState::eUndefined, rhi::EResourceState::eShaderWrite);
		imageBarrier(output.normal, rhi::EResourceState::eUndefined, rhi::EResourceState::eShaderWrite);
		imageBarrier(output.roughness, rhi::EResourceState::eUndefined, rhi::EResourceState::eShaderWrite);

		rhi_->bindComputePipeline(cmd, bakePipeline_);
		rhi_->bindBindGroup(cmd, bakePipelineLayout_, 0, bakeGroup);
		rhi_->pushConstants(cmd, bakePipelineLayout_, &constants, sizeof(constants), 0);
		rhi_->dispatch(cmd, tileCount(desc.textureSize), tileCount(desc.textureSize), 1);

		// Back to shader-read, which is where every texture in this renderer rests between
		// uses. The mip generator runs next and transitions them itself; handing it a
		// texture already in the state it is about to move to would be a lie it checks.
		imageBarrier(output.baseColor, rhi::EResourceState::eShaderWrite, rhi::EResourceState::eShaderRead);
		imageBarrier(output.normal, rhi::EResourceState::eShaderWrite, rhi::EResourceState::eShaderRead);
		imageBarrier(output.roughness, rhi::EResourceState::eShaderWrite, rhi::EResourceState::eShaderRead);

		// 6. The heights, for the ground query. copyBuffer inserts the barrier that orders
		// this after the writes above.
		rhi_->copyBuffer(cmd, heightReadback_, heightBuffer_, (u64)fieldCount * sizeof(f32));

		rhi_->endImmediateCommands(cmd);

		outHeights.resize(fieldCount);

		if (const void* mapped = rhi_->mapBuffer(heightReadback_))
		{
			memcpy(outHeights.data(), mapped, (size_t)fieldCount * sizeof(f32));
			rhi_->unmapBuffer(heightReadback_);
		}
	}
}
