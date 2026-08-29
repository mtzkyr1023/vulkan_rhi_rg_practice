#include "fog/height_fog.h"

#include <algorithm>
#include <cmath>

namespace mv::fog
{
	namespace
	{
		// Must match FogConstants in fog.hlsl.
		struct FogGpuConstants
		{
			f32 cameraPosition[3]{};
			f32 density = 0.0f;

			f32 cameraForward[3]{};
			f32 tanHalfFov = 0.0f;

			f32 viewportSize[2]{};
			f32 depthLinearA = 0.0f;
			f32 depthLinearB = 0.0f;

			f32 heightFalloff = 0.0f;
			f32 startDistance = 0.0f;
			f32 maxOpacity = 0.0f;
			f32 sunIntensity = 0.0f;

			f32 lightDirection[3]{};
			f32 shaftIntensity = 0.0f;

			f32 shaftDistance = 0.0f;
			f32 shaftAnisotropy = 0.0f;
			u32 shaftSteps = 0;
			f32 pad = 0.0f;
		};
	}

	bool HeightFog::initialize(
		const std::shared_ptr<rhi::IRHI>& rhi,
		const Shaders& shaders,
		rhi::ETextureFormat sceneColorFormat,
		rhi::BindGroupLayoutHandle sceneLayout,
		rhi::BindGroupLayoutHandle bindlessLayout)
	{
		if (!rhi || shaders.vs == nullptr || shaders.ps == nullptr)
			return false;

		if (sceneLayout == INVALID_HANDLE || bindlessLayout == INVALID_HANDLE)
			return false;

		rhi_ = rhi;

		{
			// Set 2: only what the scene set does not already carry, which is one texture.
			// The shadow atlas, the cloud shadow map, the environment cube and the sampler
			// presets all arrive through the borrowed sets.
			rhi::BindGroupLayoutDesc desc{};
			desc.bindings.push_back({
				.binding = 0, .count = 1,
				.type = rhi::EDescriptorType::eSampledImage,
				.stages = rhi::EShaderStage::eFragment });

			layout_ = rhi_->createBindGroupLayout(desc);

			rhi::PipelineLayoutDesc layoutDesc{};
			layoutDesc.bindGroups.push_back(sceneLayout);
			layoutDesc.bindGroups.push_back(bindlessLayout);
			layoutDesc.bindGroups.push_back(layout_);
			layoutDesc.pushConstantSize = sizeof(FogGpuConstants);

			pipelineLayout_ = rhi_->createPipelineLayout(layoutDesc);
		}

		rhi::ShaderDesc vsDesc{ rhi::EShaderType::eVertex, shaders.vs, shaders.vsSize, "VSMain" };
		rhi::ShaderDesc psDesc{ rhi::EShaderType::eFragment, shaders.ps, shaders.psSize, "PSMain" };

		rhi::GraphicsPipelineDesc pipelineDesc{};
		pipelineDesc.vs = rhi_->createShader(vsDesc);
		pipelineDesc.ps = rhi_->createShader(psDesc);
		pipelineDesc.layoutHandle = pipelineLayout_;

		// Premultiplied over, with the shafts riding additively in the source colour: the
		// blend gives scene * (1 - amount) + fogColor * amount + shaft.
		pipelineDesc.blend.blendEnable = true;
		pipelineDesc.blend.srcColorFactor = rhi::EBlendFactor::eOne;
		pipelineDesc.blend.dstColorFactor = rhi::EBlendFactor::eOneMinusSrcAlpha;
		pipelineDesc.blend.colorOp = rhi::EBlendOp::eAdd;

		pipelineDesc.blend.srcAlphaFactor = rhi::EBlendFactor::eOne;
		pipelineDesc.blend.dstAlphaFactor = rhi::EBlendFactor::eOneMinusSrcAlpha;
		pipelineDesc.blend.alphaOp = rhi::EBlendOp::eAdd;

		pipelineDesc.depth.depthTestEnable = false;
		pipelineDesc.depth.depthWriteEnable = false;

		pipelineDesc.rasterizer.cullMode = rhi::ECullMode::eNone;

		pipelineDesc.colorFormats.push_back(sceneColorFormat);

		pipeline_ = rhi_->createGraphicsPipeline(pipelineDesc);

		ready_ = pipeline_ != INVALID_HANDLE;

		return ready_;
	}

	void HeightFog::deinitialize()
	{
		ready_ = false;
		boundDepth_ = INVALID_HANDLE;

		rhi_.reset();
	}

	void HeightFog::record(
		rhi::CommandBufferHandle cmd,
		const FogParams& params,
		const View& view,
		const math::Vec3& lightDirection,
		f32 sunIntensity,
		rhi::BindGroupHandle sceneGroup,
		rhi::BindGroupHandle bindlessGroup,
		rhi::TextureHandle sceneDepth)
	{
		if (!ready_ || sceneDepth == INVALID_HANDLE ||
			sceneGroup == INVALID_HANDLE || bindlessGroup == INVALID_HANDLE)
			return;

		if (group_ == INVALID_HANDLE)
		{
			rhi::BindGroupDesc desc{};
			desc.layout = layout_;
			desc.sampledTextures.push_back({ .binding = 0, .texture = sceneDepth });

			group_ = rhi_->createBindGroup(desc);

			boundDepth_ = sceneDepth;
		}
		else if (sceneDepth != boundDepth_)
		{
			boundDepth_ = sceneDepth;
			rhi_->updateBindGroupTexture(group_, 0, 0, sceneDepth);
		}

		FogGpuConstants constants{};

		constants.cameraPosition[0] = view.position.x;
		constants.cameraPosition[1] = view.position.y;
		constants.cameraPosition[2] = view.position.z;
		constants.density = params.density;

		const math::Vec3 forward = math::normalize(view.forward);
		constants.cameraForward[0] = forward.x;
		constants.cameraForward[1] = forward.y;
		constants.cameraForward[2] = forward.z;
		constants.tanHalfFov = std::tan(view.fovY * 0.5f);

		constants.viewportSize[0] = (f32)view.width;
		constants.viewportSize[1] = (f32)view.height;
		constants.depthLinearA = view.farZ / (view.nearZ - view.farZ);
		constants.depthLinearB = view.nearZ * view.farZ / (view.nearZ - view.farZ);

		constants.heightFalloff = (std::max)(params.heightFalloff, 1e-5f);
		constants.startDistance = params.startDistance;
		constants.maxOpacity = params.maxOpacity;
		constants.sunIntensity = sunIntensity;

		const math::Vec3 light = math::normalize(lightDirection);
		constants.lightDirection[0] = light.x;
		constants.lightDirection[1] = light.y;
		constants.lightDirection[2] = light.z;
		constants.shaftIntensity = params.shaftIntensity;

		constants.shaftDistance = params.shaftDistance;
		constants.shaftAnisotropy = params.shaftAnisotropy;
		constants.shaftSteps = params.shaftSteps;

		rhi_->bindGraphicsPipeline(cmd, pipeline_);
		rhi_->bindBindGroup(cmd, pipelineLayout_, 0, sceneGroup);
		rhi_->bindBindGroup(cmd, pipelineLayout_, 1, bindlessGroup);
		rhi_->bindBindGroup(cmd, pipelineLayout_, 2, group_);
		rhi_->pushConstants(cmd, pipelineLayout_, &constants, sizeof(constants), 0);

		rhi_->draw(cmd, 3, 1, 0, 0);
	}
}
