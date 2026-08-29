
#include "post/post_process.h"

#include "imgui.h"

namespace mv::post
{
	namespace
	{
		// Set 2 of the shared layout. Three texture slots rather than one, because the
		// interesting effects are the ones that need more than the chain input: temporal
		// anti-aliasing wants its history and the depth buffer, bloom wants the level below
		// the one it is writing.
		constexpr u32 kTexture0Binding = 0;
		constexpr u32 kTexture1Binding = 1;
		constexpr u32 kTexture2Binding = 2;
		constexpr u32 kSamplerBinding = 3;

		// Every effect gets the same small block of parameters. It is a push constant so an
		// effect needs no buffer of its own and nothing has to be double buffered.
		constexpr u32 kEffectConstantsSize = 64;
	}

	bool PostProcessStack::initialize(const std::shared_ptr<rhi::IRHI>& rhi, const Desc& desc)
	{
		rhi_ = rhi;
		width_ = desc.width;
		height_ = desc.height;
		backbufferFormat_ = desc.backbufferFormat;
		loadShader_ = desc.loadShader;

		// The scene target and the ping-pong pair are all the same thing: a full screen
		// half-float image that is written as a render target and then read as a texture.
		rhi::TextureDesc textureDesc{};
		textureDesc.width = width_;
		textureDesc.height = height_;
		textureDesc.depth = 1;
		textureDesc.usage = rhi::ETextureUsage::eColorAttachment | rhi::ETextureUsage::eSampled;
		textureDesc.mipLevels = 1;
		textureDesc.format = kChainFormat;
		textureDesc.memoryType = rhi::EMemoryType::eDeviceLocalImage;

		sceneColor_ = rhi_->createTexture(textureDesc);
		chain_[0] = rhi_->createTexture(textureDesc);
		chain_[1] = rhi_->createTexture(textureDesc);

		// sceneColor is the exception: the render graph tracks it, because the geometry
		// passes write it before the chain ever runs.
		registerOwnedTexture(chain_[0]);
		registerOwnedTexture(chain_[1]);

		const std::vector<u32> vsCode = loadShader_("fullscreen.vs");
		if (vsCode.empty())
			return false;

		rhi::ShaderDesc vsDesc{};
		vsDesc.stage = rhi::EShaderType::eVertex;
		vsDesc.bytecode = vsCode.data();
		vsDesc.bytecodeSize = (u32)(vsCode.size() * sizeof(u32));
		vsDesc.entryPoint = "VSMain";

		fullscreenVs_ = rhi_->createShader(vsDesc);

		rhi::BindGroupLayoutDesc layoutDesc{};
		layoutDesc.bindings.push_back({ .binding = kTexture0Binding, .count = 1, .type = rhi::EDescriptorType::eSampledImage, .stages = rhi::EShaderStage::eFragment });
		layoutDesc.bindings.push_back({ .binding = kTexture1Binding, .count = 1, .type = rhi::EDescriptorType::eSampledImage, .stages = rhi::EShaderStage::eFragment });
		layoutDesc.bindings.push_back({ .binding = kTexture2Binding, .count = 1, .type = rhi::EDescriptorType::eSampledImage, .stages = rhi::EShaderStage::eFragment });
		layoutDesc.bindings.push_back({ .binding = kSamplerBinding, .count = 1, .type = rhi::EDescriptorType::eSampler, .stages = rhi::EShaderStage::eFragment });

		resourceLayout_ = rhi_->createBindGroupLayout(layoutDesc);

		rhi::PipelineLayoutDesc pipelineLayoutDesc{};
		pipelineLayoutDesc.bindGroups.push_back(desc.sceneLayout);
		pipelineLayoutDesc.bindGroups.push_back(desc.bindlessLayout);
		pipelineLayoutDesc.bindGroups.push_back(resourceLayout_);
		pipelineLayoutDesc.pushConstantSize = kEffectConstantsSize;

		pipelineLayout_ = rhi_->createPipelineLayout(pipelineLayoutDesc);

		return true;
	}

	void PostProcessStack::deinitialize()
	{
		if (!rhi_) return;

		for (auto& effect : effects_)
		{
			effect->deinitialize();
		}
		effects_.clear();

		if (sceneColor_ != INVALID_HANDLE) rhi_->freeImage(sceneColor_);
		if (chain_[0] != INVALID_HANDLE) rhi_->freeImage(chain_[0]);
		if (chain_[1] != INVALID_HANDLE) rhi_->freeImage(chain_[1]);

		sceneColor_ = INVALID_HANDLE;
		chain_[0] = INVALID_HANDLE;
		chain_[1] = INVALID_HANDLE;

		rhi_.reset();
	}

	void PostProcessStack::resize(u32 width, u32 height)
	{
		if (width == 0 || height == 0 || (width == width_ && height == height_))
			return;

		width_ = width;
		height_ = height;

		rhi_->freeImage(sceneColor_);
		rhi_->freeImage(chain_[0]);
		rhi_->freeImage(chain_[1]);

		// Everything the effects registered is about to be replaced too, so the ownership
		// list starts over rather than accumulating dead handles across resizes.
		ownedTextures_.clear();

		rhi::TextureDesc textureDesc{};
		textureDesc.width = width_;
		textureDesc.height = height_;
		textureDesc.depth = 1;
		textureDesc.usage = rhi::ETextureUsage::eColorAttachment | rhi::ETextureUsage::eSampled;
		textureDesc.mipLevels = 1;
		textureDesc.format = kChainFormat;
		textureDesc.memoryType = rhi::EMemoryType::eDeviceLocalImage;

		sceneColor_ = rhi_->createTexture(textureDesc);
		chain_[0] = rhi_->createTexture(textureDesc);
		chain_[1] = rhi_->createTexture(textureDesc);

		registerOwnedTexture(chain_[0]);
		registerOwnedTexture(chain_[1]);

		for (auto& effect : effects_)
		{
			effect->onResize(*this);

			// Anything accumulated across frames describes the old size.
			effect->reset();
			effect->ranLastFrame = false;
		}
	}

	rhi::TextureHandle PostProcessStack::chainTexture(EInputSlot slot) const
	{
		switch (slot)
		{
		case eInputPingA: return chain_[0];
		case eInputPingB: return chain_[1];
		case eInputScene:
		default:          return sceneColor_;
		}
	}

	void PostProcessStack::add(std::unique_ptr<IPostEffect> effect)
	{
		if (!effect->initialize(rhi_, *this))
			return;

		effects_.push_back(std::move(effect));
	}

	rhi::BindGroupHandle PostProcessStack::createResourceGroup(
		rhi::TextureHandle texture0, rhi::TextureHandle texture1, rhi::TextureHandle texture2)
	{
		rhi::BindGroupDesc desc{};
		desc.layout = resourceLayout_;

		// An unwritten descriptor is not valid to bind even if the shader never reads it,
		// so the slots an effect does not use are pointed at the one it does.
		desc.sampledTextures.push_back({ .binding = kTexture0Binding, .texture = texture0 });
		desc.sampledTextures.push_back({ .binding = kTexture1Binding, .texture = (texture1 != INVALID_HANDLE) ? texture1 : texture0 });
		desc.sampledTextures.push_back({ .binding = kTexture2Binding, .texture = (texture2 != INVALID_HANDLE) ? texture2 : texture0 });

		// Clamped: every effect samples in screen space, where wrapping would fold the far
		// edge of the image onto a tap that ran off the near one.
		desc.samplers.push_back({
			.binding = kSamplerBinding,
			.sampler = { .filter = rhi::EFilterMode::eLinear, .address = rhi::EAddressMode::eClampToEdge } });

		return rhi_->createBindGroup(desc);
	}

	void PostProcessStack::updateResourceGroup(
		rhi::BindGroupHandle group, rhi::TextureHandle texture0, rhi::TextureHandle texture1, rhi::TextureHandle texture2)
	{
		rhi_->updateBindGroupTexture(group, kTexture0Binding, 0, texture0);
		rhi_->updateBindGroupTexture(group, kTexture1Binding, 0, (texture1 != INVALID_HANDLE) ? texture1 : texture0);
		rhi_->updateBindGroupTexture(group, kTexture2Binding, 0, (texture2 != INVALID_HANDLE) ? texture2 : texture0);
	}

	void PostProcessStack::assignResourceGroup(
		rhi::BindGroupHandle& group, rhi::TextureHandle texture0, rhi::TextureHandle texture1, rhi::TextureHandle texture2)
	{
		if (group == INVALID_HANDLE)
		{
			group = createResourceGroup(texture0, texture1, texture2);
		}
		else
		{
			updateResourceGroup(group, texture0, texture1, texture2);
		}
	}

	rhi::PipelineHandle PostProcessStack::createEffectPipeline(rhi::ShaderHandle ps, rhi::ETextureFormat colorFormat)
	{
		rhi::GraphicsPipelineDesc desc{};
		desc.vs = fullscreenVs_;
		desc.ps = ps;
		desc.layoutHandle = pipelineLayout_;

		// One oversized triangle, no vertex buffer, no depth.
		desc.rasterizer.cullMode = rhi::ECullMode::eNone;
		desc.depth.depthTestEnable = false;
		desc.depth.depthWriteEnable = false;

		desc.colorFormats.push_back(colorFormat);

		return rhi_->createGraphicsPipeline(desc);
	}

	void PostProcessStack::registerOwnedTexture(rhi::TextureHandle texture)
	{
		if (texture != INVALID_HANDLE)
		{
			ownedTextures_.push_back(texture);
		}
	}

	bool PostProcessStack::ownsTexture(rhi::TextureHandle texture) const
	{
		for (const auto& owned : ownedTextures_)
		{
			if (owned == texture)
				return true;
		}

		return false;
	}

	void PostProcessStack::beginTarget(const EffectContext& context, rhi::TextureHandle target)
	{
		if (!ownsTexture(target))
			return;

		context.rhi->textureBarrier(context.cmd,
			{ .texture = target, .before = rhi::EResourceState::eShaderRead, .after = rhi::EResourceState::eRenderTarget });
	}

	void PostProcessStack::endTarget(const EffectContext& context, rhi::TextureHandle target)
	{
		if (!ownsTexture(target))
			return;

		// Back to where it started, so the next frame finds every chain texture in the same
		// state and the bracketing stays symmetric however many effects ran.
		context.rhi->textureBarrier(context.cmd,
			{ .texture = target, .before = rhi::EResourceState::eRenderTarget, .after = rhi::EResourceState::eShaderRead });
	}

	rhi::ShaderHandle PostProcessStack::loadPixelShader(const char* name)
	{
		const std::vector<u32> code = loadShader_(name);
		if (code.empty())
			return INVALID_HANDLE;

		rhi::ShaderDesc desc{};
		desc.stage = rhi::EShaderType::eFragment;
		desc.bytecode = code.data();
		desc.bytecodeSize = (u32)(code.size() * sizeof(u32));
		desc.entryPoint = "PSMain";

		return rhi_->createShader(desc);
	}

	rhi::ShaderHandle PostProcessStack::loadComputeShader(const char* name)
	{
		const std::vector<u32> code = loadShader_(name);
		if (code.empty())
			return INVALID_HANDLE;

		rhi::ShaderDesc desc{};
		desc.stage = rhi::EShaderType::eCompute;
		desc.bytecode = code.data();
		desc.bytecodeSize = (u32)(code.size() * sizeof(u32));
		desc.entryPoint = "CSMain";

		return rhi_->createShader(desc);
	}

	void PostProcessStack::execute(const EffectContext& base)
	{
		// Which effects actually run. Needed up front because the last one is the only one
		// that renders to the backbuffer, and the ping-pong parity depends on the count.
		std::vector<IPostEffect*> active;
		for (auto& effect : effects_)
		{
			// Whatever an effect accumulated across frames is meaningless if the chain ran
			// without it in between, so an effect that comes back is told to start over.
			if (effect->enabled && !effect->ranLastFrame)
			{
				effect->reset();
			}

			effect->ranLastFrame = effect->enabled;

			if (effect->enabled)
			{
				active.push_back(effect.get());
			}
		}

		if (active.empty())
			return;

		EffectContext context = base;
		context.inputSlot = eInputScene;

		for (size_t i = 0; i < active.size(); i++)
		{
			const bool last = (i + 1 == active.size());

			// Alternate between the two chain textures, except that the final step writes
			// the backbuffer directly rather than a fourth copy.
			context.output = last ? base.output : chain_[i & 1];

			active[i]->record(context);

			context.inputSlot = (i & 1) ? eInputPingB : eInputPingA;
		}
	}

	// --- FullscreenEffect -----------------------------------------------------

	bool FullscreenEffect::initialize(const std::shared_ptr<rhi::IRHI>& rhi, PostProcessStack& stack)
	{
		rhi_ = rhi;
		stack_ = &stack;

		const rhi::ShaderHandle ps = stack.loadPixelShader(shaderName());
		if (ps == INVALID_HANDLE)
			return false;

		chainPipeline_ = stack.createEffectPipeline(ps, stack.chainFormat());
		outputPipeline_ = stack.createEffectPipeline(ps, stack.outputFormat());

		for (u32 slot = 0; slot < eInputSlotCount; slot++)
		{
			stack.assignResourceGroup(inputGroups_[slot], stack.chainTexture((EInputSlot)slot));
		}

		return onInitialize(stack);
	}

	void FullscreenEffect::deinitialize()
	{
		rhi_.reset();
		stack_ = nullptr;
	}

	void FullscreenEffect::drawFullscreen(const EffectContext& context, rhi::BindGroupHandle resources)
	{
		rhi::IRHI* rhi = context.rhi;
		const rhi::CommandBufferHandle cmd = context.cmd;

		rhi::RenderPassColorTarget target{};
		target.texture = context.output;
		// Cleared even though the draw covers every pixel: D3D12 requires a render target to
		// be cleared, discarded or copied into before anything else reads or writes it, and
		// the chain textures are fresh every time the swap chain is recreated.
		target.clear = true;

		rhi::RenderPassDesc passDesc{};
		passDesc.colorTargets.push_back(target);

		stack_->beginTarget(context, context.output);
		rhi->beginRenderPass(cmd, passDesc);

		rhi->setViewport(cmd, 0.0f, 0.0f, (f32)context.width, (f32)context.height);
		rhi->setScissor(cmd, 0, 0, context.width, context.height);

		const rhi::PipelineLayoutHandle layout = stack_->pipelineLayout();

		// Which of the two pipelines depends on where the chain put us, not on the effect.
		rhi->bindGraphicsPipeline(cmd, (context.output == stack_->chainTexture(eInputPingA) || context.output == stack_->chainTexture(eInputPingB))
			? chainPipeline_
			: outputPipeline_);

		rhi->bindBindGroup(cmd, layout, 0, context.sceneBindGroup);
		rhi->bindBindGroup(cmd, layout, 1, context.bindlessBindGroup);
		rhi->bindBindGroup(cmd, layout, 2, resources);

		u32 constantSize = 0;
		if (const void* data = constants(constantSize))
		{
			if (constantSize > 0)
			{
				rhi->pushConstants(cmd, layout, data, constantSize, 0);
			}
		}

		rhi->draw(cmd, 3, 1, 0, 0);

		rhi->endRenderPass(cmd);
		stack_->endTarget(context, context.output);
	}

	void FullscreenEffect::record(const EffectContext& context)
	{
		drawFullscreen(context, inputGroups_[context.inputSlot]);
	}

	void FullscreenEffect::onResize(PostProcessStack& stack)
	{
		// The chain textures are new objects, so the descriptors naming the old ones are
		// stale. The pipelines are not: only the size changed.
		for (u32 slot = 0; slot < eInputSlotCount; slot++)
		{
			stack.assignResourceGroup(inputGroups_[slot], stack.chainTexture((EInputSlot)slot));
		}
	}

	void PostProcessStack::ui()
	{
		for (auto& effect : effects_)
		{
			ImGui::PushID(effect->name());
			ImGui::Checkbox(effect->name(), &effect->enabled);
			effect->ui();
			ImGui::PopID();
		}
	}
}
