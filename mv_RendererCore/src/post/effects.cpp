
#include "post/effects.h"

#include "imgui.h"

#include <algorithm>

namespace mv::post
{
	namespace
	{
		// The van der Corput sequence in an arbitrary base. Successive values fill the
		// interval evenly at every prefix length, which is what a temporal accumulation
		// needs: however many frames it has had, the samples so far are well spread.
		f32 halton(u32 index, u32 base)
		{
			f32 result = 0.0f;
			f32 fraction = 1.0f;

			while (index > 0)
			{
				fraction /= (f32)base;
				result += fraction * (f32)(index % base);
				index /= base;
			}

			return result;
		}
	}

	// --- Tonemap --------------------------------------------------------------

	void Tonemap::ui()
	{
		ImGui::SliderFloat("Exposure", &constants_.exposure, -4.0f, 4.0f, "%.2f EV");
	}

	// --- FXAA -----------------------------------------------------------------

	bool Fxaa::onInitialize(PostProcessStack& stack)
	{
		constants_.texelSizeX = 1.0f / (f32)stack.width();
		constants_.texelSizeY = 1.0f / (f32)stack.height();

		return true;
	}

	void Fxaa::ui()
	{
		ImGui::SliderFloat("Contrast threshold", &constants_.contrastThreshold, 0.0f, 0.1f, "%.4f");
		ImGui::SliderFloat("Relative threshold", &constants_.relativeThreshold, 0.0f, 0.5f, "%.3f");
	}

	// --- LensDistortion -------------------------------------------------------

	void LensDistortion::ui()
	{
		ImGui::SliderFloat("Vignette", &constants_.vignetteIntensity, 0.0f, 1.5f);
		ImGui::SliderFloat("Smoothness", &constants_.vignetteSmoothness, 0.05f, 1.0f);
		ImGui::SliderFloat("Aberration", &constants_.aberration, 0.0f, 0.02f, "%.4f");
	}

	// --- Bloom ----------------------------------------------------------------

	namespace
	{
		// Must match BloomComputeConstants in bloom_cs.hlsli.
		struct BloomComputeConstants
		{
			f32 threshold = 0.0f;
			f32 knee = 0.0f;
			f32 intensity = 0.0f;
			f32 texelX = 0.0f;

			f32 texelY = 0.0f;
			u32 targetWidth = 0;
			u32 targetHeight = 0;
			u32 pad = 0;
		};
	}

	bool Bloom::initialize(const std::shared_ptr<rhi::IRHI>& rhi, PostProcessStack& stack)
	{
		rhi_ = rhi;
		stack_ = &stack;

		const rhi::ShaderHandle extractPs = stack.loadPixelShader("bloom_extract.ps");
		const rhi::ShaderHandle downsamplePs = stack.loadPixelShader("bloom_downsample.ps");
		const rhi::ShaderHandle upsamplePs = stack.loadPixelShader("bloom_upsample.ps");
		const rhi::ShaderHandle compositePs = stack.loadPixelShader("bloom_composite.ps");

		if (extractPs == INVALID_HANDLE || downsamplePs == INVALID_HANDLE ||
			upsamplePs == INVALID_HANDLE || compositePs == INVALID_HANDLE)
		{
			return false;
		}

		extractPipeline_ = stack.createEffectPipeline(extractPs, stack.chainFormat());
		downsamplePipeline_ = stack.createEffectPipeline(downsamplePs, stack.chainFormat());
		upsamplePipeline_ = stack.createEffectPipeline(upsamplePs, stack.chainFormat());
		compositeChainPipeline_ = stack.createEffectPipeline(compositePs, stack.chainFormat());
		compositeOutputPipeline_ = stack.createEffectPipeline(compositePs, stack.outputFormat());

		// The pyramid runs as dispatches when the compute shaders are there. The composite
		// stays a fullscreen draw either way: it writes into the chain, and the chain's
		// targets are render targets that the stack hands round between effects.
		useCompute_ = createComputePipelines(stack);

		createResources(stack);

		return true;
	}

	bool Bloom::createComputePipelines(PostProcessStack& stack)
	{
		const rhi::ShaderHandle extractCs = stack.loadComputeShader("bloom_extract_cs.cs");
		const rhi::ShaderHandle downsampleCs = stack.loadComputeShader("bloom_downsample_cs.cs");
		const rhi::ShaderHandle upsampleCs = stack.loadComputeShader("bloom_upsample_cs.cs");

		if (extractCs == INVALID_HANDLE || downsampleCs == INVALID_HANDLE || upsampleCs == INVALID_HANDLE)
			return false;

		rhi::BindGroupLayoutDesc layoutDesc{};
		layoutDesc.bindings.push_back({ .binding = 0, .count = 1, .type = rhi::EDescriptorType::eSampledImage, .stages = rhi::EShaderStage::eCompute });
		layoutDesc.bindings.push_back({ .binding = 1, .count = 1, .type = rhi::EDescriptorType::eSampledImage, .stages = rhi::EShaderStage::eCompute });
		layoutDesc.bindings.push_back({ .binding = 2, .count = 1, .type = rhi::EDescriptorType::eSampler, .stages = rhi::EShaderStage::eCompute });
		layoutDesc.bindings.push_back({ .binding = 3, .count = 1, .type = rhi::EDescriptorType::eStorageImage, .stages = rhi::EShaderStage::eCompute });

		computeLayout_ = rhi_->createBindGroupLayout(layoutDesc);

		rhi::PipelineLayoutDesc pipelineLayoutDesc{};
		pipelineLayoutDesc.bindGroups.push_back(computeLayout_);
		pipelineLayoutDesc.pushConstantSize = sizeof(BloomComputeConstants);

		computePipelineLayout_ = rhi_->createPipelineLayout(pipelineLayoutDesc);

		auto makePipeline = [&](rhi::ShaderHandle cs)
		{
			rhi::ComputePipelineDesc desc{};
			desc.cs = cs;
			desc.layoutHandle = computePipelineLayout_;

			return rhi_->createComputePipeline(desc);
		};

		extractCsPipeline_ = makePipeline(extractCs);
		downsampleCsPipeline_ = makePipeline(downsampleCs);
		upsampleCsPipeline_ = makePipeline(upsampleCs);

		return extractCsPipeline_ != INVALID_HANDLE
			&& downsampleCsPipeline_ != INVALID_HANDLE
			&& upsampleCsPipeline_ != INVALID_HANDLE;
	}

	rhi::BindGroupHandle Bloom::createComputeGroup(
		rhi::TextureHandle source, rhi::TextureHandle accum, rhi::TextureHandle target)
	{
		rhi::BindGroupDesc desc{};
		desc.layout = computeLayout_;

		// An unwritten descriptor is not valid to bind even where the shader never reads
		// it, so extract and downsample point their unused accum slot at their source.
		desc.sampledTextures.push_back({ .binding = 0, .texture = source });
		desc.sampledTextures.push_back({ .binding = 1, .texture = (accum != INVALID_HANDLE) ? accum : source });

		// Clamped, for the same reason every other screen-space tap is: wrapping would fold
		// the far edge of the image onto a tap that ran off the near one.
		desc.samplers.push_back({
			.binding = 2,
			.sampler = { .filter = rhi::EFilterMode::eLinear, .address = rhi::EAddressMode::eClampToEdge } });

		desc.storageTextures.push_back({ .binding = 3, .texture = target, .arrayIndex = 0, .mipLevel = 0 });

		return rhi_->createBindGroup(desc);
	}

	void Bloom::onResize(PostProcessStack& stack)
	{
		for (u32 i = 0; i < kMipCount; i++)
		{
			if (downMips_[i] != INVALID_HANDLE)
			{
				rhi_->freeImage(downMips_[i]);
			}

			// The smallest level is shared with the downward chain rather than owned.
			if (upMips_[i] != INVALID_HANDLE && upMips_[i] != downMips_[i])
			{
				rhi_->freeImage(upMips_[i]);
			}
		}

		createResources(stack);
	}

	void Bloom::createResources(PostProcessStack& stack)
	{
		// The pyramid starts at half resolution: bloom is low frequency by definition and
		// nothing above that survives the first blur anyway.
		rhi::TextureDesc desc{};
		desc.depth = 1;
		desc.usage = rhi::ETextureUsage::eColorAttachment | rhi::ETextureUsage::eSampled;

		// The compute pyramid writes these through a UAV. eColorAttachment stays regardless
		// so the pixel-shader path is still available at runtime, and so the levels are
		// created in the shader-read state the stack's barriers expect.
		if (useCompute_)
			desc.usage = desc.usage | rhi::ETextureUsage::eStorage;

		desc.mipLevels = 1;
		desc.format = kChainFormat;
		desc.memoryType = rhi::EMemoryType::eDeviceLocalImage;

		for (u32 i = 0; i < kMipCount; i++)
		{
			mipWidth_[i] = std::max(1u, stack.width() >> (i + 1));
			mipHeight_[i] = std::max(1u, stack.height() >> (i + 1));

			desc.width = mipWidth_[i];
			desc.height = mipHeight_[i];

			downMips_[i] = rhi_->createTexture(desc);
			stack.registerOwnedTexture(downMips_[i]);

			// The smallest level is never written on the way up; the loop starts by reading
			// it, so it needs no separate target of its own.
			if (i + 1 < kMipCount)
			{
				upMips_[i] = rhi_->createTexture(desc);
				stack.registerOwnedTexture(upMips_[i]);
			}
			else
			{
				upMips_[i] = downMips_[i];
			}
		}

		for (u32 slot = 0; slot < eInputSlotCount; slot++)
		{
			stack.assignResourceGroup(extractGroups_[slot], stack.chainTexture((EInputSlot)slot));
			stack.assignResourceGroup(compositeGroups_[slot], stack.chainTexture((EInputSlot)slot), upMips_[0]);
		}

		// Level i is filled from level i - 1.
		for (u32 i = 1; i < kMipCount; i++)
		{
			stack.assignResourceGroup(downsampleGroups_[i], downMips_[i - 1]);
		}

		// Going back up, level i - 1 is the blurred magnification of level i added to what
		// the downward pass left at level i - 1.
		for (u32 i = 1; i < kMipCount; i++)
		{
			stack.assignResourceGroup(upsampleGroups_[i], upMips_[i], downMips_[i - 1]);
		}

		if (!useCompute_)
			return;

		// The same wiring for the dispatch path. These groups name a writable target as
		// well as the sources, which is the one thing the stack's resource groups cannot
		// express -- hence the separate set rather than a flag on the existing one.
		for (u32 slot = 0; slot < eInputSlotCount; slot++)
		{
			extractCsGroups_[slot] = createComputeGroup(stack.chainTexture((EInputSlot)slot), INVALID_HANDLE, downMips_[0]);
		}

		for (u32 i = 1; i < kMipCount; i++)
		{
			downsampleCsGroups_[i] = createComputeGroup(downMips_[i - 1], INVALID_HANDLE, downMips_[i]);
			upsampleCsGroups_[i] = createComputeGroup(upMips_[i], downMips_[i - 1], upMips_[i - 1]);
		}
	}

	void Bloom::deinitialize()
	{
		if (!rhi_) return;

		for (u32 i = 0; i < kMipCount; i++)
		{
			if (downMips_[i] != INVALID_HANDLE)
			{
				rhi_->freeImage(downMips_[i]);
			}

			// The smallest level is shared with the downward chain rather than owned.
			if (upMips_[i] != INVALID_HANDLE && upMips_[i] != downMips_[i])
			{
				rhi_->freeImage(upMips_[i]);
			}

			downMips_[i] = INVALID_HANDLE;
			upMips_[i] = INVALID_HANDLE;
		}

		rhi_.reset();
		stack_ = nullptr;
	}

	void Bloom::drawInto(
		const EffectContext& context, rhi::TextureHandle target, u32 width, u32 height,
		rhi::PipelineHandle pipeline, rhi::BindGroupHandle resources)
	{
		rhi::IRHI* rhi = context.rhi;
		const rhi::CommandBufferHandle cmd = context.cmd;

		rhi::RenderPassColorTarget colorTarget{};
		colorTarget.texture = target;
		colorTarget.clear = true;

		rhi::RenderPassDesc passDesc{};
		passDesc.colorTargets.push_back(colorTarget);

		stack_->beginTarget(context, target);
		rhi->beginRenderPass(cmd, passDesc);

		rhi->setViewport(cmd, 0.0f, 0.0f, (f32)width, (f32)height);
		rhi->setScissor(cmd, 0, 0, width, height);

		const rhi::PipelineLayoutHandle layout = stack_->pipelineLayout();

		rhi->bindGraphicsPipeline(cmd, pipeline);
		rhi->bindBindGroup(cmd, layout, 0, context.sceneBindGroup);
		rhi->bindBindGroup(cmd, layout, 1, context.bindlessBindGroup);
		rhi->bindBindGroup(cmd, layout, 2, resources);

		// The texel size is the level's own, because every pass here reads a different
		// resolution and a blur kernel is measured in texels of what it reads.
		Constants constants = constants_;
		constants.texelSizeX = 1.0f / (f32)width;
		constants.texelSizeY = 1.0f / (f32)height;

		rhi->pushConstants(cmd, layout, &constants, sizeof(constants), 0);

		rhi->draw(cmd, 3, 1, 0, 0);

		rhi->endRenderPass(cmd);
		stack_->endTarget(context, target);
	}

	void Bloom::dispatchInto(
		const EffectContext& context, rhi::TextureHandle target, u32 width, u32 height,
		rhi::PipelineHandle pipeline, rhi::BindGroupHandle resources)
	{
		rhi::IRHI* rhi = context.rhi;
		const rhi::CommandBufferHandle cmd = context.cmd;

		// Out of the shader-read state every chain texture rests in, and back afterwards.
		// The level is sampled by the next pass, which is what makes both transitions
		// necessary rather than leaving it writable across the pyramid.
		rhi::TextureBarrier toWrite{};
		toWrite.texture = target;
		toWrite.before = rhi::EResourceState::eUndefined;
		toWrite.after = rhi::EResourceState::eShaderWrite;
		rhi->textureBarrier(cmd, toWrite);

		rhi->bindComputePipeline(cmd, pipeline);
		rhi->bindBindGroup(cmd, computePipelineLayout_, 0, resources);

		BloomComputeConstants constants{};
		constants.threshold = constants_.threshold;
		constants.knee = constants_.knee;
		constants.intensity = constants_.intensity;

		// The texel size is the level's own, because every pass here reads a different
		// resolution and a blur kernel is measured in texels of what it reads.
		constants.texelX = 1.0f / (f32)width;
		constants.texelY = 1.0f / (f32)height;
		constants.targetWidth = width;
		constants.targetHeight = height;

		rhi->pushConstants(cmd, computePipelineLayout_, &constants, sizeof(constants), 0);

		constexpr u32 kGroupSize = 8;
		rhi->dispatch(cmd, (width + kGroupSize - 1) / kGroupSize, (height + kGroupSize - 1) / kGroupSize, 1);

		rhi::TextureBarrier toRead{};
		toRead.texture = target;
		toRead.before = rhi::EResourceState::eShaderWrite;
		toRead.after = rhi::EResourceState::eShaderRead;
		rhi->textureBarrier(cmd, toRead);
	}

	void Bloom::record(const EffectContext& context)
	{
		if (useCompute_)
		{
			// Bright pixels only, at half resolution.
			dispatchInto(context, downMips_[0], mipWidth_[0], mipHeight_[0], extractCsPipeline_, extractCsGroups_[context.inputSlot]);

			// Down the pyramid, each level blurring what the one above already blurred.
			for (u32 i = 1; i < kMipCount; i++)
			{
				dispatchInto(context, downMips_[i], mipWidth_[i], mipHeight_[i], downsampleCsPipeline_, downsampleCsGroups_[i]);
			}

			// And back up, adding each level into the one above it.
			for (u32 i = kMipCount - 1; i >= 1; i--)
			{
				dispatchInto(context, upMips_[i - 1], mipWidth_[i - 1], mipHeight_[i - 1], upsampleCsPipeline_, upsampleCsGroups_[i]);
			}
		}
		else
		{
			// Bright pixels only, at half resolution.
			drawInto(context, downMips_[0], mipWidth_[0], mipHeight_[0], extractPipeline_, extractGroups_[context.inputSlot]);

			// Down the pyramid, each level blurring what the one above already blurred.
			for (u32 i = 1; i < kMipCount; i++)
			{
				drawInto(context, downMips_[i], mipWidth_[i], mipHeight_[i], downsamplePipeline_, downsampleGroups_[i]);
			}

			// And back up, adding each level into the one above it. Summing the levels
			// rather than taking the smallest is what gives the falloff its long tail:
			// every scale contributes, weighted by how many levels it survived.
			for (u32 i = kMipCount - 1; i >= 1; i--)
			{
				drawInto(context, upMips_[i - 1], mipWidth_[i - 1], mipHeight_[i - 1], upsamplePipeline_, upsampleGroups_[i]);
			}
		}

		// Finally add the result to the untouched chain input.
		const bool toChain =
			(context.output == stack_->chainTexture(eInputPingA)) ||
			(context.output == stack_->chainTexture(eInputPingB));

		rhi::IRHI* rhi = context.rhi;
		const rhi::CommandBufferHandle cmd = context.cmd;

		rhi::RenderPassColorTarget colorTarget{};
		colorTarget.texture = context.output;
		colorTarget.clear = true;

		rhi::RenderPassDesc passDesc{};
		passDesc.colorTargets.push_back(colorTarget);

		stack_->beginTarget(context, context.output);
		rhi->beginRenderPass(cmd, passDesc);

		rhi->setViewport(cmd, 0.0f, 0.0f, (f32)context.width, (f32)context.height);
		rhi->setScissor(cmd, 0, 0, context.width, context.height);

		const rhi::PipelineLayoutHandle layout = stack_->pipelineLayout();

		rhi->bindGraphicsPipeline(cmd, toChain ? compositeChainPipeline_ : compositeOutputPipeline_);
		rhi->bindBindGroup(cmd, layout, 0, context.sceneBindGroup);
		rhi->bindBindGroup(cmd, layout, 1, context.bindlessBindGroup);
		rhi->bindBindGroup(cmd, layout, 2, compositeGroups_[context.inputSlot]);

		rhi->pushConstants(cmd, layout, &constants_, sizeof(constants_), 0);

		rhi->draw(cmd, 3, 1, 0, 0);

		rhi->endRenderPass(cmd);
		stack_->endTarget(context, context.output);
	}

	void Bloom::ui()
	{
		ImGui::SliderFloat("Threshold", &constants_.threshold, 0.0f, 4.0f);
		ImGui::SliderFloat("Knee", &constants_.knee, 0.0f, 1.0f);
		ImGui::SliderFloat("Intensity", &constants_.intensity, 0.0f, 0.5f, "%.3f");
	}

	// --- TemporalAntiAliasing -------------------------------------------------

	math::Vec3 TemporalAntiAliasing::jitter(u32 frameCounter, u32 width, u32 height) const
	{
		if (!enabled)
			return { 0.0f, 0.0f, 0.0f };

		// Bases 2 and 3, the standard pair, over a period of 8 frames.
		const u32 index = (frameCounter % 8u) + 1u;

		const f32 x = halton(index, 2) - 0.5f;
		const f32 y = halton(index, 3) - 0.5f;

		// Into normalised device coordinates, where the projection can absorb it.
		return { x * 2.0f / (f32)width, y * 2.0f / (f32)height, 0.0f };
	}

	bool TemporalAntiAliasing::initialize(const std::shared_ptr<rhi::IRHI>& rhi, PostProcessStack& stack)
	{
		rhi_ = rhi;
		stack_ = &stack;

		const rhi::ShaderHandle resolvePs = stack.loadPixelShader("taa.ps");
		const rhi::ShaderHandle copyPs = stack.loadPixelShader("fullscreen.ps");

		if (resolvePs == INVALID_HANDLE || copyPs == INVALID_HANDLE)
			return false;

		pipeline_ = stack.createEffectPipeline(resolvePs, stack.chainFormat());
		copyChainPipeline_ = stack.createEffectPipeline(copyPs, stack.chainFormat());
		copyOutputPipeline_ = stack.createEffectPipeline(copyPs, stack.outputFormat());

		rhi::TextureDesc desc{};
		desc.width = stack.width();
		desc.height = stack.height();
		desc.depth = 1;
		desc.usage = rhi::ETextureUsage::eColorAttachment | rhi::ETextureUsage::eSampled;
		desc.mipLevels = 1;
		desc.format = kChainFormat;
		desc.memoryType = rhi::EMemoryType::eDeviceLocalImage;

		history_[0] = rhi_->createTexture(desc);
		history_[1] = rhi_->createTexture(desc);

		stack.registerOwnedTexture(history_[0]);
		stack.registerOwnedTexture(history_[1]);

		// The velocity buffer has to be set before the effect is added to the stack, because
		// the bind groups are built here rather than lazily.
		if (velocity_ == INVALID_HANDLE)
			return false;

		createBindGroups(stack);

		return true;
	}

	void TemporalAntiAliasing::onResize(PostProcessStack& stack)
	{
		rhi_->freeImage(history_[0]);
		rhi_->freeImage(history_[1]);

		rhi::TextureDesc desc{};
		desc.width = stack.width();
		desc.height = stack.height();
		desc.depth = 1;
		desc.usage = rhi::ETextureUsage::eColorAttachment | rhi::ETextureUsage::eSampled;
		desc.mipLevels = 1;
		desc.format = kChainFormat;
		desc.memoryType = rhi::EMemoryType::eDeviceLocalImage;

		history_[0] = rhi_->createTexture(desc);
		history_[1] = rhi_->createTexture(desc);

		stack.registerOwnedTexture(history_[0]);
		stack.registerOwnedTexture(history_[1]);

		createBindGroups(stack);
	}

	void TemporalAntiAliasing::createBindGroups(PostProcessStack& stack)
	{
		for (u32 slot = 0; slot < eInputSlotCount; slot++)
		{
			for (u32 parity = 0; parity < 2; parity++)
			{
				stack.assignResourceGroup(resolveGroups_[slot * 2 + parity],
					stack.chainTexture((EInputSlot)slot), history_[parity], velocity_);
			}
		}

		stack.assignResourceGroup(copyGroups_[0], history_[0]);
		stack.assignResourceGroup(copyGroups_[1], history_[1]);
	}

	void TemporalAntiAliasing::deinitialize()
	{
		if (!rhi_) return;

		for (auto& history : history_)
		{
			if (history != INVALID_HANDLE)
			{
				rhi_->freeImage(history);
				history = INVALID_HANDLE;
			}
		}

		rhi_.reset();
		stack_ = nullptr;
	}

	void TemporalAntiAliasing::record(const EffectContext& context)
	{
		const u32 write = parity_;
		const u32 read = 1u - parity_;

		rhi::IRHI* rhi = context.rhi;
		const rhi::CommandBufferHandle cmd = context.cmd;
		const rhi::PipelineLayoutHandle layout = stack_->pipelineLayout();

		// Resolve into the history slot being written, reading the other one.
		{
			rhi::RenderPassColorTarget target{};
			target.texture = history_[write];
			target.clear = true;

			rhi::RenderPassDesc passDesc{};
			passDesc.colorTargets.push_back(target);

			stack_->beginTarget(context, history_[write]);
			rhi->beginRenderPass(cmd, passDesc);

			rhi->setViewport(cmd, 0.0f, 0.0f, (f32)context.width, (f32)context.height);
			rhi->setScissor(cmd, 0, 0, context.width, context.height);

			rhi->bindGraphicsPipeline(cmd, pipeline_);
			rhi->bindBindGroup(cmd, layout, 0, context.sceneBindGroup);
			rhi->bindBindGroup(cmd, layout, 1, context.bindlessBindGroup);
			rhi->bindBindGroup(cmd, layout, 2, resolveGroups_[context.inputSlot * 2 + read]);

			Constants constants = constants_;
			constants.historyValid = historyValid_ ? 1.0f : 0.0f;

			rhi->pushConstants(cmd, layout, &constants, sizeof(constants), 0);

			rhi->draw(cmd, 3, 1, 0, 0);

			rhi->endRenderPass(cmd);
			stack_->endTarget(context, history_[write]);
		}

		// The resolved frame lives in the history, and the chain expects it in the output,
		// so it is copied across. A blit rather than resolving straight into the chain
		// because the history has to survive into the next frame untouched.
		{
			const bool toChain =
				(context.output == stack_->chainTexture(eInputPingA)) ||
				(context.output == stack_->chainTexture(eInputPingB));

			rhi::RenderPassColorTarget target{};
			target.texture = context.output;
			target.clear = true;

			rhi::RenderPassDesc passDesc{};
			passDesc.colorTargets.push_back(target);

			stack_->beginTarget(context, context.output);
			rhi->beginRenderPass(cmd, passDesc);

			rhi->setViewport(cmd, 0.0f, 0.0f, (f32)context.width, (f32)context.height);
			rhi->setScissor(cmd, 0, 0, context.width, context.height);

			rhi->bindGraphicsPipeline(cmd, toChain ? copyChainPipeline_ : copyOutputPipeline_);
			rhi->bindBindGroup(cmd, layout, 0, context.sceneBindGroup);
			rhi->bindBindGroup(cmd, layout, 1, context.bindlessBindGroup);
			rhi->bindBindGroup(cmd, layout, 2, copyGroups_[write]);

			rhi->draw(cmd, 3, 1, 0, 0);

			rhi->endRenderPass(cmd);
			stack_->endTarget(context, context.output);
		}

		parity_ = read;
		historyValid_ = true;
	}

	void TemporalAntiAliasing::ui()
	{
		ImGui::SliderFloat("Blend", &constants_.blendFactor, 0.01f, 0.5f, "%.3f");
		ImGui::SliderFloat("Clamp", &constants_.clampScale, 0.25f, 4.0f);
		ImGui::TextDisabled(historyValid_ ? "history accumulating" : "history reset");
	}
}
