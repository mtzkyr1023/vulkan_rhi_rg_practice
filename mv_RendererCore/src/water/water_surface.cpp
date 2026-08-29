#include "water/water_surface.h"

#include <algorithm>
#include <cmath>

namespace mv::water
{
	namespace
	{
		// Must match the [numthreads] in water_ssr.hlsl.
		constexpr u32 kSsrTile = 8;

		// Must match WaterConstants in water_common.hlsli.
		struct WaterGpuConstants
		{
			f32 cameraPosition[3]{};
			f32 level = 0.0f;

			f32 cameraForward[3]{};
			f32 tanHalfFov = 0.0f;

			f32 lightDirection[3]{};
			f32 time = 0.0f;

			f32 sunIntensity = 0.0f;
			f32 iblIntensity = 0.0f;
			f32 waveScale = 0.0f;
			f32 pad0 = 0.0f;

			f32 extinction[3]{};
			f32 waveHeight = 0.0f;

			f32 scatterColor[3]{};
			f32 roughness = 0.0f;

			f32 viewportSize[2]{};
			f32 depthLinearA = 0.0f;
			f32 depthLinearB = 0.0f;

			f32 shoreFade = 0.0f;
			f32 reflectionStrength = 0.0f;
			f32 specularStrength = 0.0f;
			f32 ssrStrength = 0.0f;
		};

		// One filler for both pipelines. The SSR march and the surface draw have to agree
		// about the camera, the plane and above all the wave field -- a reflection bounced
		// off waves even slightly different from the ones drawn slides across them -- and
		// two hand-copied fills is how they would come to disagree.
		WaterGpuConstants makeConstants(
			const WaterParams& params,
			const WaterSurface::View& view,
			const math::Vec3& lightDirection,
			f32 sunIntensity,
			f32 iblIntensity,
			f32 time)
		{
			WaterGpuConstants constants{};

			constants.cameraPosition[0] = view.position.x;
			constants.cameraPosition[1] = view.position.y;
			constants.cameraPosition[2] = view.position.z;
			constants.level = params.level;

			const math::Vec3 forward = math::normalize(view.forward);
			constants.cameraForward[0] = forward.x;
			constants.cameraForward[1] = forward.y;
			constants.cameraForward[2] = forward.z;
			constants.tanHalfFov = std::tan(view.fovY * 0.5f);

			const math::Vec3 light = math::normalize(lightDirection);
			constants.lightDirection[0] = light.x;
			constants.lightDirection[1] = light.y;
			constants.lightDirection[2] = light.z;
			constants.time = time;

			constants.sunIntensity = sunIntensity;
			constants.iblIntensity = iblIntensity;
			constants.waveScale = params.waveScale;

			constants.extinction[0] = params.extinction.x;
			constants.extinction[1] = params.extinction.y;
			constants.extinction[2] = params.extinction.z;
			constants.waveHeight = params.waveHeight;

			constants.scatterColor[0] = params.scatterColor.x;
			constants.scatterColor[1] = params.scatterColor.y;
			constants.scatterColor[2] = params.scatterColor.z;
			constants.roughness = (std::max)(params.roughness, 0.01f);

			constants.viewportSize[0] = (f32)view.width;
			constants.viewportSize[1] = (f32)view.height;

			// The two coefficients of the projection that produced the depth buffer, so a
			// raw depth becomes a view-space distance without an inverse matrix.
			constants.depthLinearA = view.farZ / (view.nearZ - view.farZ);
			constants.depthLinearB = view.nearZ * view.farZ / (view.nearZ - view.farZ);

			constants.shoreFade = params.shoreFade;
			constants.reflectionStrength = params.reflectionStrength;
			constants.specularStrength = params.specularStrength;
			constants.ssrStrength = params.ssrStrength;

			return constants;
		}

		u32 groupCount(u32 extent)
		{
			return (extent + kSsrTile - 1) / kSsrTile;
		}
	}

	bool WaterSurface::initialize(
		const std::shared_ptr<rhi::IRHI>& rhi,
		const Shaders& shaders,
		rhi::ETextureFormat sceneColorFormat)
	{
		if (!rhi || shaders.vs == nullptr || shaders.ps == nullptr || shaders.ssr == nullptr)
			return false;

		rhi_ = rhi;

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

			// What the SSR march found, crossfaded over the cube by its own confidence.
			desc.bindings.push_back({
				.binding = 3, .count = 1,
				.type = rhi::EDescriptorType::eSampledImage,
				.stages = rhi::EShaderStage::eFragment });

			layout_ = rhi_->createBindGroupLayout(desc);

			rhi::PipelineLayoutDesc layoutDesc{};
			layoutDesc.bindGroups.push_back(layout_);
			layoutDesc.pushConstantSize = sizeof(WaterGpuConstants);

			pipelineLayout_ = rhi_->createPipelineLayout(layoutDesc);
		}

		// --- the SSR march ---------------------------------------------------------

		{
			rhi::BindGroupLayoutDesc desc{};
			desc.bindings.push_back({
				.binding = 0, .count = 1,
				.type = rhi::EDescriptorType::eSampledImage,
				.stages = rhi::EShaderStage::eCompute });
			desc.bindings.push_back({
				.binding = 1, .count = 1,
				.type = rhi::EDescriptorType::eSampledImage,
				.stages = rhi::EShaderStage::eCompute });
			desc.bindings.push_back({
				.binding = 2, .count = 1,
				.type = rhi::EDescriptorType::eSampler,
				.stages = rhi::EShaderStage::eCompute });
			desc.bindings.push_back({
				.binding = 3, .count = 1,
				.type = rhi::EDescriptorType::eStorageImage,
				.stages = rhi::EShaderStage::eCompute });

			ssrLayout_ = rhi_->createBindGroupLayout(desc);

			rhi::PipelineLayoutDesc layoutDesc{};
			layoutDesc.bindGroups.push_back(ssrLayout_);
			layoutDesc.pushConstantSize = sizeof(WaterGpuConstants);

			ssrPipelineLayout_ = rhi_->createPipelineLayout(layoutDesc);

			rhi::ShaderDesc csDesc{ rhi::EShaderType::eCompute, shaders.ssr, shaders.ssrSize, "CSMain" };

			rhi::ComputePipelineDesc pipelineDesc{};
			pipelineDesc.cs = rhi_->createShader(csDesc);
			pipelineDesc.layoutHandle = ssrPipelineLayout_;

			ssrPipeline_ = rhi_->createComputePipeline(pipelineDesc);
		}

		rhi::ShaderDesc vsDesc{ rhi::EShaderType::eVertex, shaders.vs, shaders.vsSize, "VSMain" };
		rhi::ShaderDesc psDesc{ rhi::EShaderType::eFragment, shaders.ps, shaders.psSize, "PSMain" };

		rhi::GraphicsPipelineDesc pipelineDesc{};
		pipelineDesc.vs = rhi_->createShader(vsDesc);
		pipelineDesc.ps = rhi_->createShader(psDesc);
		pipelineDesc.layoutHandle = pipelineLayout_;

		// Premultiplied: the shader already scaled the body of the water by its opacity and
		// deliberately did not scale the sun glint, which sits on the surface whatever is
		// under it.
		pipelineDesc.blend.blendEnable = true;
		pipelineDesc.blend.srcColorFactor = rhi::EBlendFactor::eOne;
		pipelineDesc.blend.dstColorFactor = rhi::EBlendFactor::eOneMinusSrcAlpha;
		pipelineDesc.blend.colorOp = rhi::EBlendOp::eAdd;

		pipelineDesc.blend.srcAlphaFactor = rhi::EBlendFactor::eOne;
		pipelineDesc.blend.dstAlphaFactor = rhi::EBlendFactor::eOneMinusSrcAlpha;
		pipelineDesc.blend.alphaOp = rhi::EBlendOp::eAdd;

		// The plane is solved in the shader and compared against the scene depth there, so
		// there is nothing for the depth unit to do.
		pipelineDesc.depth.depthTestEnable = false;
		pipelineDesc.depth.depthWriteEnable = false;

		pipelineDesc.rasterizer.cullMode = rhi::ECullMode::eNone;

		pipelineDesc.colorFormats.push_back(sceneColorFormat);

		pipeline_ = rhi_->createGraphicsPipeline(pipelineDesc);

		ready_ = pipeline_ != INVALID_HANDLE && ssrPipeline_ != INVALID_HANDLE;

		return ready_;
	}

	void WaterSurface::deinitialize()
	{
		if (rhi_ && reflectionTarget_ != INVALID_HANDLE)
			rhi_->releaseImage(reflectionTarget_);

		reflectionTarget_ = INVALID_HANDLE;

		ready_ = false;
		boundDepth_ = INVALID_HANDLE;
		boundCube_ = INVALID_HANDLE;
		boundSsrDepth_ = INVALID_HANDLE;
		boundSsrColor_ = INVALID_HANDLE;

		rhi_.reset();
	}

	void WaterSurface::resize(u32 width, u32 height)
	{
		if (!ready_)
			return;

		if (reflectionTarget_ != INVALID_HANDLE)
			rhi_->releaseImage(reflectionTarget_);

		rhi::TextureDesc desc{};
		desc.width = (std::max)(width, 1u);
		desc.height = (std::max)(height, 1u);
		desc.depth = 1;
		desc.mipLevels = 1;

		// Half float: it holds the HDR scene colour it copied its hits from.
		desc.format = rhi::ETextureFormat::eR16G16B16A16_SFLOAT;
		desc.usage = rhi::ETextureUsage::eSampled | rhi::ETextureUsage::eStorage;
		desc.memoryType = rhi::EMemoryType::eDeviceLocalImage;

		reflectionTarget_ = rhi_->createTexture(desc);

		// The engine only resizes with the device idle, so re-pointing existing groups at
		// the replacement is safe here in a way it never is mid-frame.
		if (ssrGroup_ != INVALID_HANDLE)
			rhi_->updateBindGroupStorageTexture(ssrGroup_, 3, 0, reflectionTarget_, 0);

		if (group_ != INVALID_HANDLE)
			rhi_->updateBindGroupTexture(group_, 3, 0, reflectionTarget_);
	}

	void WaterSurface::recordSSR(
		rhi::CommandBufferHandle cmd,
		const WaterParams& params,
		const View& view,
		rhi::TextureHandle sceneDepth,
		rhi::TextureHandle sceneColor)
	{
		if (!ready_ || reflectionTarget_ == INVALID_HANDLE ||
			sceneDepth == INVALID_HANDLE || sceneColor == INVALID_HANDLE)
			return;

		if (ssrGroup_ == INVALID_HANDLE)
		{
			rhi::BindGroupDesc desc{};
			desc.layout = ssrLayout_;
			desc.sampledTextures.push_back({ .binding = 0, .texture = sceneDepth });
			desc.sampledTextures.push_back({ .binding = 1, .texture = sceneColor });
			desc.samplers.push_back({
				.binding = 2,
				.sampler = { .filter = rhi::EFilterMode::eLinear, .address = rhi::EAddressMode::eClampToEdge } });
			desc.storageTextures.push_back({ .binding = 3, .texture = reflectionTarget_, .arrayIndex = 0, .mipLevel = 0 });

			ssrGroup_ = rhi_->createBindGroup(desc);

			boundSsrDepth_ = sceneDepth;
			boundSsrColor_ = sceneColor;
		}
		else
		{
			if (sceneDepth != boundSsrDepth_)
			{
				boundSsrDepth_ = sceneDepth;
				rhi_->updateBindGroupTexture(ssrGroup_, 0, 0, sceneDepth);
			}

			if (sceneColor != boundSsrColor_)
			{
				boundSsrColor_ = sceneColor;
				rhi_->updateBindGroupTexture(ssrGroup_, 1, 0, sceneColor);
			}
		}

		rhi::TextureBarrier toWrite{};
		toWrite.texture = reflectionTarget_;
		toWrite.before = rhi::EResourceState::eUndefined;
		toWrite.after = rhi::EResourceState::eShaderWrite;
		rhi_->textureBarrier(cmd, toWrite);

		// The march never reads the light, but one shared filler is what keeps the two
		// pipelines' views of the surface identical, so it gets the whole block anyway.
		const WaterGpuConstants constants = makeConstants(params, view, { 0.0f, -1.0f, 0.0f }, 0.0f, 0.0f, time_);

		rhi_->bindComputePipeline(cmd, ssrPipeline_);
		rhi_->bindBindGroup(cmd, ssrPipelineLayout_, 0, ssrGroup_);
		rhi_->pushConstants(cmd, ssrPipelineLayout_, &constants, sizeof(constants), 0);

		rhi_->dispatch(cmd, groupCount(view.width), groupCount(view.height), 1);

		rhi::TextureBarrier toRead{};
		toRead.texture = reflectionTarget_;
		toRead.before = rhi::EResourceState::eShaderWrite;
		toRead.after = rhi::EResourceState::eShaderRead;
		rhi_->textureBarrier(cmd, toRead);
	}

	void WaterSurface::record(
		rhi::CommandBufferHandle cmd,
		const WaterParams& params,
		const View& view,
		const math::Vec3& lightDirection,
		f32 sunIntensity,
		f32 iblIntensity,
		rhi::TextureHandle sceneDepth,
		rhi::TextureHandle environmentCube)
	{
		if (!ready_ || reflectionTarget_ == INVALID_HANDLE ||
			sceneDepth == INVALID_HANDLE || environmentCube == INVALID_HANDLE)
			return;

		// Built on the first record rather than at initialize: neither the depth buffer nor
		// the cube exists yet when the pipeline is created.
		if (group_ == INVALID_HANDLE)
		{
			rhi::BindGroupDesc desc{};
			desc.layout = layout_;
			desc.sampledTextures.push_back({ .binding = 0, .texture = sceneDepth });
			desc.sampledTextures.push_back({ .binding = 1, .texture = environmentCube });
			desc.samplers.push_back({
				.binding = 2,
				.sampler = { .filter = rhi::EFilterMode::eLinear, .address = rhi::EAddressMode::eClampToEdge } });
			desc.sampledTextures.push_back({ .binding = 3, .texture = reflectionTarget_ });

			group_ = rhi_->createBindGroup(desc);

			boundDepth_ = sceneDepth;
			boundCube_ = environmentCube;
		}
		else
		{
			if (sceneDepth != boundDepth_)
			{
				boundDepth_ = sceneDepth;
				rhi_->updateBindGroupTexture(group_, 0, 0, sceneDepth);
			}

			if (environmentCube != boundCube_)
			{
				boundCube_ = environmentCube;
				rhi_->updateBindGroupTexture(group_, 1, 0, environmentCube);
			}
		}

		const WaterGpuConstants constants = makeConstants(params, view, lightDirection, sunIntensity, iblIntensity, time_);

		rhi_->bindGraphicsPipeline(cmd, pipeline_);
		rhi_->bindBindGroup(cmd, pipelineLayout_, 0, group_);
		rhi_->pushConstants(cmd, pipelineLayout_, &constants, sizeof(constants), 0);

		rhi_->draw(cmd, 3, 1, 0, 0);
	}
}
