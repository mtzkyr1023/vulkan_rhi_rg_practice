#include "clouds/cloud_renderer.h"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace mv::clouds
{
	namespace
	{
		// Must match the [numthreads] in the cloud shaders.
		constexpr u32 kVolumeTile = 4;
		constexpr u32 kScreenTile = 8;

		// Must match VolumeConstants / WeatherConstants in the bake shaders. The weather
		// one is a superset, so one struct covers both.
		struct VolumeGpuConstants
		{
			u32 size = 0;
			u32 seed = 0;
			f32 coverage = 0.0f;
			f32 pad = 0.0f;
		};

		// Must match CloudConstants in clouds.hlsli.
		struct CloudGpuConstants
		{
			f32 cameraPosition[3]{};
			f32 planetRadius = 0.0f;

			f32 lightDirection[3]{};
			f32 layerBottom = 0.0f;

			f32 sunColor[3]{};
			f32 layerTop = 0.0f;

			f32 windOffset[3]{};
			f32 coverageScale = 0.0f;

			f32 cameraForward[3]{};
			f32 tanHalfFov = 0.0f;

			f32 shapeScale = 0.0f;
			f32 detailScale = 0.0f;
			f32 detailStrength = 0.0f;
			f32 densityScale = 0.0f;

			f32 forwardScattering = 0.0f;
			f32 backwardScattering = 0.0f;
			f32 scatterBlend = 0.0f;
			f32 extinction = 0.0f;

			u32 targetSize[2]{};
			u32 viewSteps = 0;
			u32 lightSteps = 0;

			f32 maxDistance = 0.0f;
			f32 ambientStrength = 0.0f;
			f32 depthLinearA = 0.0f;
			f32 depthLinearB = 0.0f;
		};

		// Must match CloudFieldConstants in clouds.hlsli.
		struct FieldGpuConstants
		{
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
		};

		// Must match CloudShadowConstants in cloud_shadow.hlsl.
		struct ShadowGpuConstants
		{
			FieldGpuConstants field{};

			f32 lightDirection[3]{};
			f32 mapExtent = 0.0f;

			f32 planeRight[3]{};
			u32 mapSize = 0;

			f32 planeUp[3]{};
			u32 steps = 0;

			f32 planeOrigin[3]{};
			f32 pad = 0.0f;
		};

		// Must match CloudCompositeConstants in cloud_composite.hlsl.
		struct CompositeGpuConstants
		{
			f32 lowResTexel[2]{};
			u32 lowResSize[2]{};

			f32 depthTolerance = 0.0f;
			f32 pad[3]{};
		};

		FieldGpuConstants makeField(const CloudParams& params, const math::Vec3& windOffset)
		{
			FieldGpuConstants field{};

			field.windOffset[0] = windOffset.x;
			field.windOffset[1] = windOffset.y;
			field.windOffset[2] = windOffset.z;
			field.coverageScale = 1.0f / std::max(params.weatherScale, 1.0f);

			field.planetRadius = params.planetRadius;
			field.layerBottom = params.layerBottom;
			field.layerTop = params.layerTop;
			field.shapeScale = params.shapeScale;

			field.detailScale = params.detailScale;
			field.detailStrength = params.detailStrength;
			field.densityScale = params.densityScale;
			field.extinction = params.extinction;

			return field;
		}

		u32 groupCount(u32 extent, u32 tile)
		{
			return (extent + tile - 1) / tile;
		}
	}

	bool CloudRenderer::initialize(
		const std::shared_ptr<rhi::IRHI>& rhi,
		const Shaders& shaders,
		rhi::ETextureFormat sceneColorFormat)
	{
		if (!rhi)
			return false;

		rhi_ = rhi;

		// --- the three baked textures -------------------------------------------

		rhi::TextureDesc volumeDesc{};
		volumeDesc.width = kShapeSize;
		volumeDesc.height = kShapeSize;
		volumeDesc.depth = kShapeSize;
		volumeDesc.volume = true;
		volumeDesc.mipLevels = 1;
		volumeDesc.format = rhi::ETextureFormat::eR8G8B8A8_UNORM;
		volumeDesc.usage = rhi::ETextureUsage::eSampled | rhi::ETextureUsage::eStorage;
		volumeDesc.memoryType = rhi::EMemoryType::eDeviceLocalImage;

		shapeVolume_ = rhi_->createTexture(volumeDesc);

		volumeDesc.width = kDetailSize;
		volumeDesc.height = kDetailSize;
		volumeDesc.depth = kDetailSize;

		detailVolume_ = rhi_->createTexture(volumeDesc);

		rhi::TextureDesc weatherDesc{};
		weatherDesc.width = kWeatherSize;
		weatherDesc.height = kWeatherSize;
		weatherDesc.depth = 1;
		weatherDesc.mipLevels = 1;
		weatherDesc.format = rhi::ETextureFormat::eR8G8B8A8_UNORM;
		weatherDesc.usage = rhi::ETextureUsage::eSampled | rhi::ETextureUsage::eStorage;
		weatherDesc.memoryType = rhi::EMemoryType::eDeviceLocalImage;

		weatherMap_ = rhi_->createTexture(weatherDesc);

		// The shadow map is the same shape and the same format. Transmittance is one channel
		// of information in four, but the backend has no single-channel storage format and a
		// shadow wants eight bits far more than it wants a fourth channel.
		rhi::TextureDesc shadowDesc = weatherDesc;
		shadowDesc.width = kShadowSize;
		shadowDesc.height = kShadowSize;

		shadowMap_ = rhi_->createTexture(shadowDesc);

		// --- bake pipelines ------------------------------------------------------

		{
			rhi::BindGroupLayoutDesc desc{};
			desc.bindings.push_back({
				.binding = 0, .count = 1,
				.type = rhi::EDescriptorType::eStorageImage,
				.stages = rhi::EShaderStage::eCompute });

			volumeLayout_ = rhi_->createBindGroupLayout(desc);

			rhi::PipelineLayoutDesc layoutDesc{};
			layoutDesc.bindGroups.push_back(volumeLayout_);
			layoutDesc.pushConstantSize = sizeof(VolumeGpuConstants);

			volumePipelineLayout_ = rhi_->createPipelineLayout(layoutDesc);
		}

		auto makeCompute = [&](rhi::PipelineLayoutHandle layout, const u32* code, u32 size)
		{
			if (code == nullptr || size == 0)
				return (rhi::PipelineHandle)INVALID_HANDLE;

			rhi::ShaderDesc shaderDesc{ rhi::EShaderType::eCompute, code, size, "CSMain" };

			rhi::ComputePipelineDesc pipelineDesc{};
			pipelineDesc.cs = rhi_->createShader(shaderDesc);
			pipelineDesc.layoutHandle = layout;

			return rhi_->createComputePipeline(pipelineDesc);
		};

		shapePipeline_ = makeCompute(volumePipelineLayout_, shaders.shape, shaders.shapeSize);
		detailPipeline_ = makeCompute(volumePipelineLayout_, shaders.detail, shaders.detailSize);
		weatherPipeline_ = makeCompute(volumePipelineLayout_, shaders.weather, shaders.weatherSize);

		auto storageGroup = [&](rhi::TextureHandle texture)
		{
			rhi::BindGroupDesc desc{};
			desc.layout = volumeLayout_;
			desc.storageTextures.push_back({ .binding = 0, .texture = texture, .arrayIndex = 0, .mipLevel = 0 });

			return rhi_->createBindGroup(desc);
		};

		shapeGroup_ = storageGroup(shapeVolume_);
		detailGroup_ = storageGroup(detailVolume_);
		weatherGroup_ = storageGroup(weatherMap_);

		// --- march ---------------------------------------------------------------

		{
			rhi::BindGroupLayoutDesc desc{};

			for (u32 binding = 0; binding < 4; binding++)
			{
				desc.bindings.push_back({
					.binding = binding, .count = 1,
					.type = rhi::EDescriptorType::eSampledImage,
					.stages = rhi::EShaderStage::eCompute });
			}

			// Repeat for the tiling volumes, clamp for the depth buffer.
			desc.bindings.push_back({
				.binding = 4, .count = 1,
				.type = rhi::EDescriptorType::eSampler,
				.stages = rhi::EShaderStage::eCompute });
			desc.bindings.push_back({
				.binding = 5, .count = 1,
				.type = rhi::EDescriptorType::eSampler,
				.stages = rhi::EShaderStage::eCompute });

			desc.bindings.push_back({
				.binding = 6, .count = 1,
				.type = rhi::EDescriptorType::eStorageImage,
				.stages = rhi::EShaderStage::eCompute });

			marchLayout_ = rhi_->createBindGroupLayout(desc);

			rhi::PipelineLayoutDesc layoutDesc{};
			layoutDesc.bindGroups.push_back(marchLayout_);
			layoutDesc.pushConstantSize = sizeof(CloudGpuConstants);

			marchPipelineLayout_ = rhi_->createPipelineLayout(layoutDesc);
		}

		marchPipeline_ = makeCompute(marchPipelineLayout_, shaders.march, shaders.marchSize);

		// --- shadow map ----------------------------------------------------------

		{
			rhi::BindGroupLayoutDesc desc{};

			// The three volumes, one repeat sampler, one target. No depth buffer and no
			// clamp sampler: the bake knows nothing about the screen.
			for (u32 binding = 0; binding < 3; binding++)
			{
				desc.bindings.push_back({
					.binding = binding, .count = 1,
					.type = rhi::EDescriptorType::eSampledImage,
					.stages = rhi::EShaderStage::eCompute });
			}

			desc.bindings.push_back({
				.binding = 3, .count = 1,
				.type = rhi::EDescriptorType::eSampler,
				.stages = rhi::EShaderStage::eCompute });
			desc.bindings.push_back({
				.binding = 4, .count = 1,
				.type = rhi::EDescriptorType::eStorageImage,
				.stages = rhi::EShaderStage::eCompute });

			shadowLayout_ = rhi_->createBindGroupLayout(desc);

			rhi::PipelineLayoutDesc layoutDesc{};
			layoutDesc.bindGroups.push_back(shadowLayout_);
			layoutDesc.pushConstantSize = sizeof(ShadowGpuConstants);

			shadowPipelineLayout_ = rhi_->createPipelineLayout(layoutDesc);
		}

		shadowPipeline_ = makeCompute(shadowPipelineLayout_, shaders.shadow, shaders.shadowSize);

		{
			// Nothing here moves with the window, so unlike the march group this one is built
			// once and never rebuilt.
			rhi::BindGroupDesc desc{};
			desc.layout = shadowLayout_;
			desc.sampledTextures.push_back({ .binding = 0, .texture = shapeVolume_ });
			desc.sampledTextures.push_back({ .binding = 1, .texture = detailVolume_ });
			desc.sampledTextures.push_back({ .binding = 2, .texture = weatherMap_ });
			desc.samplers.push_back({
				.binding = 3,
				.sampler = { .filter = rhi::EFilterMode::eLinear, .address = rhi::EAddressMode::eRepeat } });
			desc.storageTextures.push_back({ .binding = 4, .texture = shadowMap_, .arrayIndex = 0, .mipLevel = 0 });

			shadowGroup_ = rhi_->createBindGroup(desc);
		}

		// --- composite -----------------------------------------------------------

		{
			rhi::BindGroupLayoutDesc desc{};
			desc.bindings.push_back({
				.binding = 0, .count = 1,
				.type = rhi::EDescriptorType::eSampledImage,
				.stages = rhi::EShaderStage::eFragment });
			desc.bindings.push_back({
				.binding = 1, .count = 1,
				.type = rhi::EDescriptorType::eSampledImage,
				.stages = rhi::EShaderStage::eFragment });
			desc.bindings.push_back({
				.binding = 2, .count = 1,
				.type = rhi::EDescriptorType::eSampler,
				.stages = rhi::EShaderStage::eFragment });

			compositeLayout_ = rhi_->createBindGroupLayout(desc);

			rhi::PipelineLayoutDesc layoutDesc{};
			layoutDesc.bindGroups.push_back(compositeLayout_);
			layoutDesc.pushConstantSize = sizeof(CompositeGpuConstants);

			compositePipelineLayout_ = rhi_->createPipelineLayout(layoutDesc);
		}

		if (shaders.compositeVs != nullptr && shaders.compositePs != nullptr)
		{
			rhi::ShaderDesc vsDesc{ rhi::EShaderType::eVertex, shaders.compositeVs, shaders.compositeVsSize, "VSMain" };
			rhi::ShaderDesc psDesc{ rhi::EShaderType::eFragment, shaders.compositePs, shaders.compositePsSize, "PSMain" };

			rhi::GraphicsPipelineDesc pipelineDesc{};
			pipelineDesc.vs = rhi_->createShader(vsDesc);
			pipelineDesc.ps = rhi_->createShader(psDesc);
			pipelineDesc.layoutHandle = compositePipelineLayout_;

			// The march produced premultiplied scattering with transmittance in alpha, so
			// this is exactly scene * transmittance + scattering.
			pipelineDesc.blend.blendEnable = true;
			pipelineDesc.blend.srcColorFactor = rhi::EBlendFactor::eOne;
			pipelineDesc.blend.dstColorFactor = rhi::EBlendFactor::eSrcAlpha;
			pipelineDesc.blend.colorOp = rhi::EBlendOp::eAdd;

			// Alpha is transmittance on both sides, so they multiply.
			pipelineDesc.blend.srcAlphaFactor = rhi::EBlendFactor::eZero;
			pipelineDesc.blend.dstAlphaFactor = rhi::EBlendFactor::eSrcAlpha;
			pipelineDesc.blend.alphaOp = rhi::EBlendOp::eAdd;

			pipelineDesc.depth.depthTestEnable = false;
			pipelineDesc.depth.depthWriteEnable = false;

			pipelineDesc.rasterizer.cullMode = rhi::ECullMode::eNone;

			pipelineDesc.colorFormats.push_back(sceneColorFormat);

			compositePipeline_ = rhi_->createGraphicsPipeline(pipelineDesc);
		}

		ready_ =
			shapePipeline_ != INVALID_HANDLE &&
			detailPipeline_ != INVALID_HANDLE &&
			weatherPipeline_ != INVALID_HANDLE &&
			marchPipeline_ != INVALID_HANDLE &&
			shadowPipeline_ != INVALID_HANDLE &&
			compositePipeline_ != INVALID_HANDLE;

		return ready_;
	}

	void CloudRenderer::releaseTargets()
	{
		if (marchTarget_ != INVALID_HANDLE)
		{
			rhi_->releaseImage(marchTarget_);
			marchTarget_ = INVALID_HANDLE;
		}
	}

	void CloudRenderer::deinitialize()
	{
		if (!rhi_)
			return;

		releaseTargets();

		if (shapeVolume_ != INVALID_HANDLE) rhi_->releaseImage(shapeVolume_);
		if (detailVolume_ != INVALID_HANDLE) rhi_->releaseImage(detailVolume_);
		if (weatherMap_ != INVALID_HANDLE) rhi_->releaseImage(weatherMap_);
		if (shadowMap_ != INVALID_HANDLE) rhi_->releaseImage(shadowMap_);

		shapeVolume_ = INVALID_HANDLE;
		detailVolume_ = INVALID_HANDLE;
		weatherMap_ = INVALID_HANDLE;
		shadowMap_ = INVALID_HANDLE;

		ready_ = false;
		rhi_.reset();
	}

	void CloudRenderer::resize(u32 width, u32 height)
	{
		if (!ready_)
			return;

		const u32 low = std::max(1u, width / 2);
		const u32 lowHeight = std::max(1u, height / 2);

		if (low == lowResWidth_ && lowHeight == lowResHeight_ && marchTarget_ != INVALID_HANDLE)
			return;

		releaseTargets();

		lowResWidth_ = low;
		lowResHeight_ = lowHeight;

		rhi::TextureDesc desc{};
		desc.width = lowResWidth_;
		desc.height = lowResHeight_;
		desc.depth = 1;
		desc.mipLevels = 1;

		// Half float: the march accumulates sun-scaled radiance, which runs well above one.
		desc.format = rhi::ETextureFormat::eR16G16B16A16_SFLOAT;
		desc.usage = rhi::ETextureUsage::eSampled | rhi::ETextureUsage::eStorage;
		desc.memoryType = rhi::EMemoryType::eDeviceLocalImage;

		marchTarget_ = rhi_->createTexture(desc);

		// The groups name the target, so they are rebuilt with it. The scene depth is
		// filled in per frame, because the graph owns it and it can move.
		rhi::BindGroupDesc marchDesc{};
		marchDesc.layout = marchLayout_;
		marchDesc.sampledTextures.push_back({ .binding = 0, .texture = shapeVolume_ });
		marchDesc.sampledTextures.push_back({ .binding = 1, .texture = detailVolume_ });
		marchDesc.sampledTextures.push_back({ .binding = 2, .texture = weatherMap_ });
		marchDesc.sampledTextures.push_back({ .binding = 3, .texture = weatherMap_ });
		marchDesc.samplers.push_back({
			.binding = 4,
			.sampler = { .filter = rhi::EFilterMode::eLinear, .address = rhi::EAddressMode::eRepeat } });
		marchDesc.samplers.push_back({
			.binding = 5,
			.sampler = { .filter = rhi::EFilterMode::eLinear, .address = rhi::EAddressMode::eClampToEdge } });
		marchDesc.storageTextures.push_back({ .binding = 6, .texture = marchTarget_, .arrayIndex = 0, .mipLevel = 0 });

		marchGroup_ = rhi_->createBindGroup(marchDesc);

		rhi::BindGroupDesc compositeDesc{};
		compositeDesc.layout = compositeLayout_;
		compositeDesc.sampledTextures.push_back({ .binding = 0, .texture = marchTarget_ });
		compositeDesc.sampledTextures.push_back({ .binding = 1, .texture = marchTarget_ });
		compositeDesc.samplers.push_back({
			.binding = 2,
			.sampler = { .filter = rhi::EFilterMode::eNearest, .address = rhi::EAddressMode::eClampToEdge } });

		compositeGroup_ = rhi_->createBindGroup(compositeDesc);

		// Both groups were just rebuilt, so whatever depth they named before is gone and
		// the next march has to point them at it again.
		boundDepth_ = INVALID_HANDLE;
	}

	void CloudRenderer::bake(const CloudParams& params)
	{
		if (!ready_)
			return;

		const auto start = std::chrono::high_resolution_clock::now();

		const rhi::CommandBufferHandle cmd = rhi_->beginImmediateCommands();

		auto toWrite = [&](rhi::TextureHandle texture)
		{
			rhi::TextureBarrier barrier{};
			barrier.texture = texture;
			barrier.before = rhi::EResourceState::eUndefined;
			barrier.after = rhi::EResourceState::eShaderWrite;

			rhi_->textureBarrier(cmd, barrier);
		};

		auto toRead = [&](rhi::TextureHandle texture)
		{
			rhi::TextureBarrier barrier{};
			barrier.texture = texture;
			barrier.before = rhi::EResourceState::eShaderWrite;
			barrier.after = rhi::EResourceState::eShaderRead;

			rhi_->textureBarrier(cmd, barrier);
		};

		toWrite(shapeVolume_);
		toWrite(detailVolume_);
		toWrite(weatherMap_);

		VolumeGpuConstants constants{};
		constants.seed = params.seed;

		constants.size = kShapeSize;
		rhi_->bindComputePipeline(cmd, shapePipeline_);
		rhi_->bindBindGroup(cmd, volumePipelineLayout_, 0, shapeGroup_);
		rhi_->pushConstants(cmd, volumePipelineLayout_, &constants, sizeof(constants), 0);
		rhi_->dispatch(cmd, groupCount(kShapeSize, kVolumeTile), groupCount(kShapeSize, kVolumeTile), groupCount(kShapeSize, kVolumeTile));

		constants.size = kDetailSize;
		rhi_->bindComputePipeline(cmd, detailPipeline_);
		rhi_->bindBindGroup(cmd, volumePipelineLayout_, 0, detailGroup_);
		rhi_->pushConstants(cmd, volumePipelineLayout_, &constants, sizeof(constants), 0);
		rhi_->dispatch(cmd, groupCount(kDetailSize, kVolumeTile), groupCount(kDetailSize, kVolumeTile), groupCount(kDetailSize, kVolumeTile));

		constants.size = kWeatherSize;
		constants.coverage = params.coverage;
		rhi_->bindComputePipeline(cmd, weatherPipeline_);
		rhi_->bindBindGroup(cmd, volumePipelineLayout_, 0, weatherGroup_);
		rhi_->pushConstants(cmd, volumePipelineLayout_, &constants, sizeof(constants), 0);
		rhi_->dispatch(cmd, groupCount(kWeatherSize, kScreenTile), groupCount(kWeatherSize, kScreenTile), 1);

		toRead(shapeVolume_);
		toRead(detailVolume_);
		toRead(weatherMap_);

		rhi_->endImmediateCommands(cmd);

		const auto end = std::chrono::high_resolution_clock::now();
		lastBakeMs_ = std::chrono::duration<f32, std::milli>(end - start).count();
	}

	void CloudRenderer::recordMarch(
		rhi::CommandBufferHandle cmd,
		const CloudParams& params,
		const View& view,
		const math::Vec3& lightDirection,
		const math::Vec3& sunColor,
		rhi::TextureHandle sceneDepth)
	{
		if (!ready_ || marchTarget_ == INVALID_HANDLE)
			return;

		// The graph owns the depth buffer and hands out a different one when the window
		// resizes, so the slot is re-pointed rather than baked into the group -- but only
		// when it actually changed, because rewriting a descriptor a frame in flight is
		// still reading is a race.
		if (sceneDepth != boundDepth_)
		{
			boundDepth_ = sceneDepth;

			rhi_->updateBindGroupTexture(marchGroup_, 3, 0, sceneDepth);
			rhi_->updateBindGroupTexture(compositeGroup_, 1, 0, sceneDepth);
		}

		rhi::TextureBarrier toWrite{};
		toWrite.texture = marchTarget_;
		toWrite.before = rhi::EResourceState::eUndefined;
		toWrite.after = rhi::EResourceState::eShaderWrite;
		rhi_->textureBarrier(cmd, toWrite);

		CloudGpuConstants constants{};

		constants.cameraPosition[0] = view.position.x;
		constants.cameraPosition[1] = view.position.y;
		constants.cameraPosition[2] = view.position.z;
		constants.planetRadius = params.planetRadius;

		const math::Vec3 forward = math::normalize(view.forward);
		constants.cameraForward[0] = forward.x;
		constants.cameraForward[1] = forward.y;
		constants.cameraForward[2] = forward.z;
		constants.tanHalfFov = std::tan(view.fovY * 0.5f);

		// The two coefficients of the projection that produced the depth buffer, so a raw
		// depth can be turned back into a view-space distance without an inverse matrix:
		//   depth = (z * A + B) / -z   =>   -z = B / (depth + A)
		constants.depthLinearA = view.farZ / (view.nearZ - view.farZ);
		constants.depthLinearB = view.nearZ * view.farZ / (view.nearZ - view.farZ);

		const math::Vec3 light = math::normalize(lightDirection);
		constants.lightDirection[0] = light.x;
		constants.lightDirection[1] = light.y;
		constants.lightDirection[2] = light.z;
		constants.layerBottom = params.layerBottom;

		constants.sunColor[0] = sunColor.x;
		constants.sunColor[1] = sunColor.y;
		constants.sunColor[2] = sunColor.z;
		constants.layerTop = params.layerTop;

		const math::Vec3 wind = math::normalize(params.windDirection) * (params.windSpeed * windDistance_);
		constants.windOffset[0] = wind.x;
		constants.windOffset[1] = wind.y;
		constants.windOffset[2] = wind.z;

		constants.coverageScale = 1.0f / std::max(params.weatherScale, 1.0f);

		constants.shapeScale = params.shapeScale;
		constants.detailScale = params.detailScale;
		constants.detailStrength = params.detailStrength;
		constants.densityScale = params.densityScale;

		constants.forwardScattering = params.forwardScattering;
		constants.backwardScattering = params.backwardScattering;
		constants.scatterBlend = params.scatterBlend;
		constants.extinction = params.extinction;

		constants.targetSize[0] = lowResWidth_;
		constants.targetSize[1] = lowResHeight_;
		constants.viewSteps = std::max(1u, params.viewSteps);
		constants.lightSteps = std::max(1u, params.lightSteps);

		constants.maxDistance = params.maxDistance;
		constants.ambientStrength = params.ambientStrength;

		rhi_->bindComputePipeline(cmd, marchPipeline_);
		rhi_->bindBindGroup(cmd, marchPipelineLayout_, 0, marchGroup_);
		rhi_->pushConstants(cmd, marchPipelineLayout_, &constants, sizeof(constants), 0);

		rhi_->dispatch(cmd, groupCount(lowResWidth_, kScreenTile), groupCount(lowResHeight_, kScreenTile), 1);

		rhi::TextureBarrier toRead{};
		toRead.texture = marchTarget_;
		toRead.before = rhi::EResourceState::eShaderWrite;
		toRead.after = rhi::EResourceState::eShaderRead;
		rhi_->textureBarrier(cmd, toRead);
	}

	void CloudRenderer::recordShadow(
		rhi::CommandBufferHandle cmd,
		const CloudParams& params,
		const math::Vec3& lightDirection,
		const math::Vec3& focus)
	{
		if (!ready_ || shadowMap_ == INVALID_HANDLE)
			return;

		shadowExtent_ = (std::max)(params.shadowExtent, 1.0f);

		// The light-space frame, built the way a shadow map builds its view: two axes
		// perpendicular to the sun. Everything on one sun ray projects to one point in
		// this frame, which is what makes the lookup exact at any receiver height.
		const math::Vec3 toSun = math::normalize(lightDirection) * -1.0f;

		shadowRight_ = math::normalize(math::cross(toSun, { 0.0f, 1.0f, 0.0f }));
		shadowUp_ = math::cross(shadowRight_, toSun);

		// Snapped to a texel in light space. Without this the map slides by a fraction of
		// a texel every frame as the camera moves, and every shadow edge in the scene
		// crawls with it -- the same reason a cascade's origin is snapped. The component
		// along the sun does not matter: the projection ignores it.
		const f32 texel = shadowExtent_ / (f32)kShadowSize;

		const f32 su = std::floor(math::dot(focus, shadowRight_) / texel) * texel;
		const f32 sv = std::floor(math::dot(focus, shadowUp_) / texel) * texel;

		shadowOrigin_ = shadowRight_ * su + shadowUp_ * sv;

		rhi::TextureBarrier toWrite{};
		toWrite.texture = shadowMap_;
		toWrite.before = rhi::EResourceState::eUndefined;
		toWrite.after = rhi::EResourceState::eShaderWrite;
		rhi_->textureBarrier(cmd, toWrite);

		const math::Vec3 wind = math::normalize(params.windDirection) * (params.windSpeed * windDistance_);

		ShadowGpuConstants constants{};
		constants.field = makeField(params, wind);

		const math::Vec3 light = math::normalize(lightDirection);
		constants.lightDirection[0] = light.x;
		constants.lightDirection[1] = light.y;
		constants.lightDirection[2] = light.z;

		constants.mapExtent = shadowExtent_;

		constants.planeRight[0] = shadowRight_.x;
		constants.planeRight[1] = shadowRight_.y;
		constants.planeRight[2] = shadowRight_.z;
		constants.mapSize = kShadowSize;

		constants.planeUp[0] = shadowUp_.x;
		constants.planeUp[1] = shadowUp_.y;
		constants.planeUp[2] = shadowUp_.z;
		constants.steps = (std::max)(1u, params.shadowSteps);

		constants.planeOrigin[0] = shadowOrigin_.x;
		constants.planeOrigin[1] = shadowOrigin_.y;
		constants.planeOrigin[2] = shadowOrigin_.z;

		rhi_->bindComputePipeline(cmd, shadowPipeline_);
		rhi_->bindBindGroup(cmd, shadowPipelineLayout_, 0, shadowGroup_);
		rhi_->pushConstants(cmd, shadowPipelineLayout_, &constants, sizeof(constants), 0);

		rhi_->dispatch(cmd, groupCount(kShadowSize, kScreenTile), groupCount(kShadowSize, kScreenTile), 1);

		rhi::TextureBarrier toRead{};
		toRead.texture = shadowMap_;
		toRead.before = rhi::EResourceState::eShaderWrite;
		toRead.after = rhi::EResourceState::eShaderRead;
		rhi_->textureBarrier(cmd, toRead);
	}

	void CloudRenderer::recordComposite(rhi::CommandBufferHandle cmd, rhi::TextureHandle sceneDepth)
	{
		if (!ready_ || marchTarget_ == INVALID_HANDLE)
			return;

		CompositeGpuConstants constants{};
		constants.lowResTexel[0] = 1.0f / (f32)lowResWidth_;
		constants.lowResTexel[1] = 1.0f / (f32)lowResHeight_;
		constants.lowResSize[0] = lowResWidth_;
		constants.lowResSize[1] = lowResHeight_;

		// Depth-buffer units, which are heavily non-linear: most of the range is spent very
		// close to the camera, so a tolerance that rejects a hill silhouette at a hundred
		// metres has to be small.
		constants.depthTolerance = 0.0005f;

		rhi_->bindGraphicsPipeline(cmd, compositePipeline_);
		rhi_->bindBindGroup(cmd, compositePipelineLayout_, 0, compositeGroup_);
		rhi_->pushConstants(cmd, compositePipelineLayout_, &constants, sizeof(constants), 0);

		rhi_->draw(cmd, 3, 1, 0, 0);
	}
}
