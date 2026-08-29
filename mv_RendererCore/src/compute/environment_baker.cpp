#include "compute/environment_baker.h"

#include "env/environment.h"

#include <algorithm>
#include <cstring>

namespace mv::compute
{
	namespace
	{
		// Must match the [numthreads] in env_sky.hlsl and env_prefilter.hlsl.
		constexpr u32 kTileSize = 8;

		// Must match SkyConstants in env.hlsli.
		struct SkyGpuConstants
		{
			f32 lightDirection[3]{};
			f32 turbidity = 0.0f;

			f32 groundAlbedo[3]{};
			f32 sunIntensity = 0.0f;

			u32 faceSize = 0;
			u32 mipLevel = 0;

			f32 roughness = 0.0f;
			u32 sourceSize = 0;
		};

		// Must match SkyCloudConstants in env_sky.hlsl. Exactly a hundred and twenty-eight
		// bytes, which is what a push constant block is guaranteed to have: the layer comes in
		// as its field half plus the four lighting terms the march actually reads, and nothing
		// about a camera, a screen or a depth buffer comes in at all.
		struct SkyCloudGpuConstants
		{
			SkyGpuConstants sky{};

			f32 windOffset[3]{};
			f32 coverageScale = 0.0f;

			f32 planetRadius = 0.0f;
			f32 layerBottom = 0.0f;
			f32 layerTop = 0.0f;
			f32 shapeScale = 0.0f;

			f32 detailScale = 0.0f;
			f32 detailStrength = 0.0f;
			f32 densityScale = 0.0f;
			f32 extinction = 0.0f;

			f32 cloudSunColor[3]{};
			f32 cloudAmbient = 0.0f;

			f32 forwardScattering = 0.0f;
			f32 backwardScattering = 0.0f;
			f32 scatterBlend = 0.0f;
			u32 cloudSteps = 0;
		};

		static_assert(sizeof(SkyCloudGpuConstants) == 128, "the sky push constant must fit the guaranteed block");

		u32 tileCount(u32 extent)
		{
			return (extent + kTileSize - 1) / kTileSize;
		}
	}

	bool EnvironmentBaker::initialize(const std::shared_ptr<rhi::IRHI>& rhi, const Shaders& shaders)
	{
		if (!rhi || shaders.sky == nullptr || shaders.prefilter == nullptr)
			return false;

		rhi_ = rhi;

		auto storageImage = [](u32 binding)
		{
			return rhi::BindingDesc{
				.binding = binding, .count = 1,
				.type = rhi::EDescriptorType::eStorageImage,
				.stages = rhi::EShaderStage::eCompute };
		};

		{
			rhi::BindGroupLayoutDesc desc{};
			desc.bindings.push_back(storageImage(0));  // the cube's level 0
			desc.bindings.push_back({
				.binding = 1, .count = 1,
				.type = rhi::EDescriptorType::eStorageBufferReadWrite,
				.stages = rhi::EShaderStage::eCompute });  // the radiance the CPU projects

			// The cloud layer, marched into the cube so that what the scene reflects and
			// what it is lit by agree with what it can see overhead.
			for (u32 binding = 2; binding <= 4; binding++)
			{
				desc.bindings.push_back({
					.binding = binding, .count = 1,
					.type = rhi::EDescriptorType::eSampledImage,
					.stages = rhi::EShaderStage::eCompute });
			}

			desc.bindings.push_back({
				.binding = 5, .count = 1,
				.type = rhi::EDescriptorType::eSampler,
				.stages = rhi::EShaderStage::eCompute });

			skyLayout_ = rhi_->createBindGroupLayout(desc);
		}
		{
			rhi::BindGroupLayoutDesc desc{};
			desc.bindings.push_back(storageImage(0));  // level 0, read
			desc.bindings.push_back(storageImage(1));  // the level being written

			prefilterLayout_ = rhi_->createBindGroupLayout(desc);
		}

		auto makePipeline = [&](rhi::BindGroupLayoutHandle layout, rhi::PipelineLayoutHandle& outLayout, const u32* code, u32 size, u32 pushSize)
		{
			rhi::PipelineLayoutDesc pipelineLayoutDesc{};
			pipelineLayoutDesc.bindGroups.push_back(layout);
			pipelineLayoutDesc.pushConstantSize = pushSize;

			outLayout = rhi_->createPipelineLayout(pipelineLayoutDesc);

			rhi::ShaderDesc shaderDesc{ rhi::EShaderType::eCompute, code, size, "CSMain" };

			rhi::ComputePipelineDesc pipelineDesc{};
			pipelineDesc.cs = rhi_->createShader(shaderDesc);
			pipelineDesc.layoutHandle = outLayout;

			return rhi_->createComputePipeline(pipelineDesc);
		};

		skyPipeline_ = makePipeline(skyLayout_, skyPipelineLayout_, shaders.sky, shaders.skySize, sizeof(SkyCloudGpuConstants));
		prefilterPipeline_ = makePipeline(prefilterLayout_, prefilterPipelineLayout_, shaders.prefilter, shaders.prefilterSize, sizeof(SkyGpuConstants));

		// One texel each, for a bake that supplies no cloud volumes. They are never read --
		// the pass takes zero steps then -- but a descriptor still has to name something,
		// and on Vulkan it has to name something in the right layout, which is what the
		// barrier below is for.
		{
			rhi::TextureDesc desc{};
			desc.width = 1;
			desc.height = 1;
			desc.depth = 1;
			desc.mipLevels = 1;
			desc.format = rhi::ETextureFormat::eR8G8B8A8_UNORM;
			desc.usage = rhi::ETextureUsage::eSampled;
			desc.memoryType = rhi::EMemoryType::eDeviceLocalImage;

			dummyMap_ = rhi_->createTexture(desc);

			desc.volume = true;
			dummyVolume_ = rhi_->createTexture(desc);

			const rhi::CommandBufferHandle cmd = rhi_->beginImmediateCommands();

			for (rhi::TextureHandle texture : { dummyMap_, dummyVolume_ })
			{
				rhi::TextureBarrier barrier{};
				barrier.texture = texture;
				barrier.before = rhi::EResourceState::eUndefined;
				barrier.after = rhi::EResourceState::eShaderRead;

				rhi_->textureBarrier(cmd, barrier);
			}

			rhi_->endImmediateCommands(cmd);
		}

		ready_ = skyPipeline_ != INVALID_HANDLE && prefilterPipeline_ != INVALID_HANDLE;

		return ready_;
	}

	void EnvironmentBaker::deinitialize()
	{
		if (rhi_)
		{
			if (radianceBuffer_ != INVALID_HANDLE) rhi_->releaseBuffer(radianceBuffer_);
			if (radianceReadback_ != INVALID_HANDLE) rhi_->releaseBuffer(radianceReadback_);
			if (dummyVolume_ != INVALID_HANDLE) rhi_->releaseImage(dummyVolume_);
			if (dummyMap_ != INVALID_HANDLE) rhi_->releaseImage(dummyMap_);
		}

		dummyVolume_ = INVALID_HANDLE;
		dummyMap_ = INVALID_HANDLE;

		radianceBuffer_ = INVALID_HANDLE;
		radianceReadback_ = INVALID_HANDLE;
		radianceCapacity_ = 0;

		ready_ = false;
		rhi_.reset();
	}

	void EnvironmentBaker::bake(
		const env::SkyParams& params,
		rhi::TextureHandle cubemap,
		u32 faceSize,
		u32 mipCount,
		std::vector<math::Vec3>& outRadiance,
		const CloudLayer& clouds)
	{
		if (!ready_ || cubemap == INVALID_HANDLE)
			return;

		const u32 texelCount = faceSize * faceSize * rhi::eCubeFaceCount;

		// float3 in a structured buffer is 12 bytes under D3D packing rules, which is what
		// the SPIR-V build is forced into as well by -fvk-use-dx-layout.
		const u64 radianceSize = (u64)texelCount * sizeof(math::Vec3);

		if (texelCount > radianceCapacity_)
		{
			if (radianceBuffer_ != INVALID_HANDLE) rhi_->releaseBuffer(radianceBuffer_);
			if (radianceReadback_ != INVALID_HANDLE) rhi_->releaseBuffer(radianceReadback_);

			rhi::BufferDesc desc{};
			desc.size = radianceSize;
			desc.usage = rhi::EBufferUsage::eStorageReadWrite | rhi::EBufferUsage::eTransferSrc;
			desc.memoryType = rhi::EMemoryType::eDeviceLocalBuffer;

			radianceBuffer_ = rhi_->createBuffer(desc);

			rhi::BufferDesc readbackDesc{};
			readbackDesc.size = radianceSize;
			readbackDesc.usage = rhi::EBufferUsage::eTransferDst;
			readbackDesc.memoryType = rhi::EMemoryType::eReadback;

			radianceReadback_ = rhi_->createBuffer(readbackDesc);

			radianceCapacity_ = texelCount;
		}

		SkyCloudGpuConstants constants{};
		constants.sky.lightDirection[0] = params.lightDirection.x;
		constants.sky.lightDirection[1] = params.lightDirection.y;
		constants.sky.lightDirection[2] = params.lightDirection.z;
		constants.sky.turbidity = params.turbidity;
		constants.sky.groundAlbedo[0] = params.groundAlbedo.x;
		constants.sky.groundAlbedo[1] = params.groundAlbedo.y;
		constants.sky.groundAlbedo[2] = params.groundAlbedo.z;
		constants.sky.sunIntensity = params.sunIntensity;

		// The three volumes have to arrive together or not at all: a march with one of them
		// missing would sample a stand-in as if it were weather.
		const bool hasClouds =
			clouds.steps > 0 &&
			clouds.shape != INVALID_HANDLE &&
			clouds.detail != INVALID_HANDLE &&
			clouds.weather != INVALID_HANDLE;

		if (hasClouds)
		{
			constants.windOffset[0] = clouds.windOffset[0];
			constants.windOffset[1] = clouds.windOffset[1];
			constants.windOffset[2] = clouds.windOffset[2];
			constants.coverageScale = clouds.coverageScale;

			constants.planetRadius = clouds.planetRadius;
			constants.layerBottom = clouds.layerBottom;
			constants.layerTop = clouds.layerTop;
			constants.shapeScale = clouds.shapeScale;

			constants.detailScale = clouds.detailScale;
			constants.detailStrength = clouds.detailStrength;
			constants.densityScale = clouds.densityScale;
			constants.extinction = clouds.extinction;

			constants.cloudSunColor[0] = clouds.sunColor[0];
			constants.cloudSunColor[1] = clouds.sunColor[1];
			constants.cloudSunColor[2] = clouds.sunColor[2];
			constants.cloudAmbient = clouds.ambientStrength;

			constants.forwardScattering = clouds.forwardScattering;
			constants.backwardScattering = clouds.backwardScattering;
			constants.scatterBlend = clouds.scatterBlend;
			constants.cloudSteps = clouds.steps;
		}

		rhi::BindGroupDesc skyGroupDesc{};
		skyGroupDesc.layout = skyLayout_;
		skyGroupDesc.storageTextures.push_back({ .binding = 0, .texture = cubemap, .arrayIndex = 0, .mipLevel = 0 });
		skyGroupDesc.storageBuffers.push_back({ .binding = 1, .buffer = radianceBuffer_, .offset = 0, .stride = sizeof(math::Vec3), .count = texelCount });
		skyGroupDesc.sampledTextures.push_back({ .binding = 2, .texture = hasClouds ? clouds.shape : dummyVolume_ });
		skyGroupDesc.sampledTextures.push_back({ .binding = 3, .texture = hasClouds ? clouds.detail : dummyVolume_ });
		skyGroupDesc.sampledTextures.push_back({ .binding = 4, .texture = hasClouds ? clouds.weather : dummyMap_ });
		skyGroupDesc.samplers.push_back({
			.binding = 5,
			.sampler = { .filter = rhi::EFilterMode::eLinear, .address = rhi::EAddressMode::eRepeat } });

		const rhi::BindGroupHandle skyGroup = rhi_->createBindGroup(skyGroupDesc);

		const rhi::CommandBufferHandle cmd = rhi_->beginImmediateCommands();

		auto imageBarrier = [&](rhi::EResourceState before, rhi::EResourceState after)
		{
			rhi::TextureBarrier barrier{};
			barrier.texture = cubemap;
			barrier.before = before;
			barrier.after = after;

			rhi_->textureBarrier(cmd, barrier);
		};

		// From wherever it is: the cube is created once and rebaked whenever the sun moves,
		// so the first bake finds it untouched and later ones find it in shader-read.
		imageBarrier(rhi::EResourceState::eUndefined, rhi::EResourceState::eShaderWrite);

		// 1. The sky, into level 0 and into the radiance buffer.
		constants.sky.faceSize = faceSize;
		constants.sky.mipLevel = 0;

		rhi_->bindComputePipeline(cmd, skyPipeline_);
		rhi_->bindBindGroup(cmd, skyPipelineLayout_, 0, skyGroup);
		rhi_->pushConstants(cmd, skyPipelineLayout_, &constants, sizeof(constants), 0);
		rhi_->dispatch(cmd, tileCount(faceSize), tileCount(faceSize), rhi::eCubeFaceCount);

		// 2. Each roughness level, all filtering level 0. Reading the level the previous
		// step wrote is what the barrier between them is for.
		std::vector<rhi::BindGroupHandle> prefilterGroups;
		prefilterGroups.reserve(mipCount);

		rhi_->bindComputePipeline(cmd, prefilterPipeline_);

		for (u32 mip = 1; mip < mipCount; mip++)
		{
			imageBarrier(rhi::EResourceState::eShaderWrite, rhi::EResourceState::eShaderWrite);

			rhi::BindGroupDesc groupDesc{};
			groupDesc.layout = prefilterLayout_;
			groupDesc.storageTextures.push_back({ .binding = 0, .texture = cubemap, .arrayIndex = 0, .mipLevel = 0 });
			groupDesc.storageTextures.push_back({ .binding = 1, .texture = cubemap, .arrayIndex = 0, .mipLevel = mip });

			prefilterGroups.push_back(rhi_->createBindGroup(groupDesc));

			const u32 mipSize = std::max(1u, faceSize >> mip);

			constants.sky.faceSize = mipSize;
			constants.sky.mipLevel = mip;
			constants.sky.sourceSize = faceSize;

			// Level 0 is roughness zero and the last level is fully rough; the levels
			// between them are spaced evenly, which is what the shader's lookup assumes.
			constants.sky.roughness = (f32)mip / (f32)(mipCount - 1);

			rhi_->bindBindGroup(cmd, prefilterPipelineLayout_, 0, prefilterGroups.back());
			rhi_->pushConstants(cmd, prefilterPipelineLayout_, &constants.sky, sizeof(constants.sky), 0);
			rhi_->dispatch(cmd, tileCount(mipSize), tileCount(mipSize), rhi::eCubeFaceCount);
		}

		// 3. The radiance, for the projection that stays on the CPU. Taken now rather than at
		// the very end, because step 4 runs the sky pass again and overwrites the buffer.
		{
			rhi::BufferBarrier barrier{};
			barrier.buffer = radianceBuffer_;
			barrier.before = rhi::EResourceState::eShaderWrite;
			// eCopySrc, not the eTransferSrc sitting four lines below it in the same enum.
			// They read as synonyms but only this one is mapped in the Vulkan backend; the
			// other falls through to a zero stage mask, which validation rejects outright.
			barrier.after = rhi::EResourceState::eCopySrc;

			rhi_->bufferBarrier(cmd, barrier);
		}

		rhi_->copyBuffer(cmd, radianceReadback_, radianceBuffer_, radianceSize);

		// 4. Level 0 again, this time without the clouds.
		//
		// The chain above needed them: the prefiltered levels are what a rough surface
		// reflects and they are filtered from level 0, and the coefficients just copied out
		// are what everything diffuse is lit by. But level 0 is also what the skybox pass
		// draws, and there the clouds are actively harmful. They arrive twice -- once
		// blurred to a hundred and twenty-eight texels a face by this cube, once sharp from
		// the volumetric pass -- and the composite blends the second over the first rather
		// than replacing it, so the coarse copy shows through every gap in the fine one as
		// blocks of cloud in clear sky.
		//
		// So the layer stays in the mips and in the coefficients, and comes back out of the
		// level the sky is actually drawn from.
		if (hasClouds)
		{
			imageBarrier(rhi::EResourceState::eShaderWrite, rhi::EResourceState::eShaderWrite);

			constants.sky.faceSize = faceSize;
			constants.sky.mipLevel = 0;
			constants.cloudSteps = 0;

			rhi_->bindComputePipeline(cmd, skyPipeline_);
			rhi_->bindBindGroup(cmd, skyPipelineLayout_, 0, skyGroup);
			rhi_->pushConstants(cmd, skyPipelineLayout_, &constants, sizeof(constants), 0);
			rhi_->dispatch(cmd, tileCount(faceSize), tileCount(faceSize), rhi::eCubeFaceCount);
		}

		imageBarrier(rhi::EResourceState::eShaderWrite, rhi::EResourceState::eShaderRead);

		rhi_->endImmediateCommands(cmd);

		outRadiance.resize(texelCount);

		if (const void* mapped = rhi_->mapBuffer(radianceReadback_))
		{
			memcpy(outRadiance.data(), mapped, (size_t)radianceSize);
			rhi_->unmapBuffer(radianceReadback_);
		}
	}
}
