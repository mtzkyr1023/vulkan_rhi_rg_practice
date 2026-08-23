
#include "ui/imgui_renderer.h"

#include "imgui.h"

namespace mv::ui
{
	namespace
	{
		constexpr u32 kInitialVertexCapacity = 8 * 1024;
		constexpr u32 kInitialIndexCapacity = 16 * 1024;

		// Binding numbers are unique within the group because Vulkan requires it; D3D12
		// maps them onto b0 / t1 / s2, which is what imgui.hlsl declares.
		constexpr u32 kConstantsBinding = 0;
		constexpr u32 kTextureBinding = 1;
		constexpr u32 kSamplerBinding = 2;
	}

	bool ImGuiRenderer::initialize(
		const std::shared_ptr<rhi::IRHI>& rhi,
		const ShaderCode& vs,
		const ShaderCode& ps,
		rhi::ETextureFormat depthFormat)
	{
		if (!vs.bytecode || !ps.bytecode)
			return false;

		rhi_ = rhi;

		if (!createFontTexture())
			return false;

		rhi::BindGroupLayoutDesc groupLayoutDesc{};
		groupLayoutDesc.bindings.push_back({
			.binding = kConstantsBinding, .count = 1,
			.type = rhi::EDescriptorType::eUniformBuffer, .stages = rhi::EShaderStage::eVertex });
		groupLayoutDesc.bindings.push_back({
			.binding = kTextureBinding, .count = 1,
			.type = rhi::EDescriptorType::eSampledImage, .stages = rhi::EShaderStage::eFragment });
		groupLayoutDesc.bindings.push_back({
			.binding = kSamplerBinding, .count = 1,
			.type = rhi::EDescriptorType::eSampler, .stages = rhi::EShaderStage::eFragment });

		bindGroupLayout_ = rhi_->createBindGroupLayout(groupLayoutDesc);

		rhi::PipelineLayoutDesc layoutDesc{};
		layoutDesc.bindGroups.push_back(bindGroupLayout_);
		pipelineLayout_ = rhi_->createPipelineLayout(layoutDesc);

		const u32 framesInFlight = rhi_->framesInFlight();
		frames_.resize(framesInFlight);
		bindGroups_.resize(framesInFlight);

		for (u32 i = 0; i < framesInFlight; i++)
		{
			// 256 bytes is D3D12's minimum constant buffer view size.
			rhi::BufferDesc constantDesc{};
			constantDesc.size = 256;
			constantDesc.usage = rhi::EBufferUsage::eUniform;
			constantDesc.memoryType = rhi::EMemoryType::eHostVisibleBuffer;

			frames_[i].constantBuffer = rhi_->createBuffer(constantDesc);

			ensureCapacity(frames_[i], kInitialVertexCapacity, kInitialIndexCapacity);

			rhi::BindGroupDesc groupDesc{};
			groupDesc.layout = bindGroupLayout_;
			groupDesc.uniformBuffers.push_back({ .binding = kConstantsBinding, .buffer = frames_[i].constantBuffer, .offset = 0, .range = 256 });
			groupDesc.sampledTextures.push_back({ .binding = kTextureBinding, .texture = fontTexture_ });
			groupDesc.samplers.push_back({ .binding = kSamplerBinding, .sampler = { .filter = rhi::EFilterMode::eLinear, .address = rhi::EAddressMode::eClampToEdge } });

			bindGroups_[i] = rhi_->createBindGroup(groupDesc);
		}

		rhi::ShaderDesc vsDesc{};
		vsDesc.stage = rhi::EShaderType::eVertex;
		vsDesc.bytecode = vs.bytecode;
		vsDesc.bytecodeSize = vs.size;
		vsDesc.entryPoint = "VSMain";

		rhi::ShaderDesc psDesc{};
		psDesc.stage = rhi::EShaderType::eFragment;
		psDesc.bytecode = ps.bytecode;
		psDesc.bytecodeSize = ps.size;
		psDesc.entryPoint = "PSMain";

		rhi::GraphicsPipelineDesc pipelineDesc{};
		pipelineDesc.vs = rhi_->createShader(vsDesc);
		pipelineDesc.ps = rhi_->createShader(psDesc);
		pipelineDesc.layoutHandle = pipelineLayout_;
		pipelineDesc.topology = rhi::EPrimitiveTopology::eTriangleList;
		pipelineDesc.rasterizer.cullMode = rhi::ECullMode::eNone;
		pipelineDesc.colorFormats.push_back(rhi_->backbufferFormat());

		// The UI draws on top of everything, so the format is declared to satisfy the render
		// pass but depth testing and writing stay off.
		pipelineDesc.depthFormat = depthFormat;
		pipelineDesc.depth.depthTestEnable = false;
		pipelineDesc.depth.depthWriteEnable = false;

		// Straight alpha blending, which is what ImGui's vertex colours assume.
		pipelineDesc.blend.blendEnable = true;
		pipelineDesc.blend.srcColorFactor = rhi::EBlendFactor::eSrcAlpha;
		pipelineDesc.blend.dstColorFactor = rhi::EBlendFactor::eOneMinusSrcAlpha;
		pipelineDesc.blend.colorOp = rhi::EBlendOp::eAdd;
		pipelineDesc.blend.srcAlphaFactor = rhi::EBlendFactor::eOne;
		pipelineDesc.blend.dstAlphaFactor = rhi::EBlendFactor::eOneMinusSrcAlpha;
		pipelineDesc.blend.alphaOp = rhi::EBlendOp::eAdd;

		// Matches ImDrawVert member for member.
		pipelineDesc.vertexLayout.bindings.push_back({ .binding = 0, .stride = sizeof(ImDrawVert), .perInstance = false });
		pipelineDesc.vertexLayout.attributes.push_back({
			.location = 0, .semanticName = "POSITION", .semanticIndex = 0,
			.binding = 0, .format = rhi::EVertexFormat::eFloat2, .offset = (u32)offsetof(ImDrawVert, pos) });
		pipelineDesc.vertexLayout.attributes.push_back({
			.location = 1, .semanticName = "TEXCOORD", .semanticIndex = 0,
			.binding = 0, .format = rhi::EVertexFormat::eFloat2, .offset = (u32)offsetof(ImDrawVert, uv) });
		pipelineDesc.vertexLayout.attributes.push_back({
			.location = 2, .semanticName = "COLOR", .semanticIndex = 0,
			.binding = 0, .format = rhi::EVertexFormat::eR8G8B8A8_UNORM, .offset = (u32)offsetof(ImDrawVert, col) });

		pipeline_ = rhi_->createGraphicsPipeline(pipelineDesc);

		return true;
	}

	void ImGuiRenderer::deinitialize()
	{
		if (!rhi_) return;

		for (auto& frame : frames_)
		{
			if (frame.vertexBuffer != INVALID_HANDLE) rhi_->freeBuffer(frame.vertexBuffer);
			if (frame.indexBuffer != INVALID_HANDLE) rhi_->freeBuffer(frame.indexBuffer);
			if (frame.constantBuffer != INVALID_HANDLE) rhi_->freeBuffer(frame.constantBuffer);
		}
		frames_.clear();

		if (fontTexture_ != INVALID_HANDLE)
		{
			rhi_->freeImage(fontTexture_);
			fontTexture_ = INVALID_HANDLE;
		}

		rhi_.reset();
	}

	bool ImGuiRenderer::createFontTexture()
	{
		ImGuiIO& io = ImGui::GetIO();

		unsigned char* pixels = nullptr;
		int width = 0;
		int height = 0;
		io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

		if (!pixels || width <= 0 || height <= 0)
			return false;

		rhi::TextureDesc desc{};
		desc.width = (u32)width;
		desc.height = (u32)height;
		desc.depth = 1;
		desc.usage = rhi::ETextureUsage::eSampled | rhi::ETextureUsage::eTransferDst;
		desc.format = rhi::ETextureFormat::eR8G8B8A8_UNORM;
		desc.memoryType = rhi::EMemoryType::eDeviceLocalImage;

		fontTexture_ = rhi_->createTexture(desc);

		// The UI is drawn at 1:1, so the atlas never minifies and needs no mips.
		const rhi::TextureUpload upload{ pixels, (u64)width * height * 4 };
		rhi_->uploadTexture(fontTexture_, &upload, 1);

		// The atlas is bound through our own bind group, so the id only has to be non-zero
		// for ImGui's own validation; the renderer never reads it back.
		io.Fonts->SetTexID((ImTextureID)(intptr_t)(fontTexture_ + 1));

		return true;
	}

	void ImGuiRenderer::ensureCapacity(FrameBuffers& frame, u32 vertexCount, u32 indexCount)
	{
		if (vertexCount > frame.vertexCapacity)
		{
			if (frame.vertexBuffer != INVALID_HANDLE)
				rhi_->freeBuffer(frame.vertexBuffer);

			// Grow with headroom so a slowly increasing UI does not reallocate every frame.
			frame.vertexCapacity = vertexCount + vertexCount / 2;

			rhi::BufferDesc desc{};
			desc.size = (u64)frame.vertexCapacity * sizeof(ImDrawVert);
			desc.usage = rhi::EBufferUsage::eVertex;
			desc.memoryType = rhi::EMemoryType::eHostVisibleBuffer;

			frame.vertexBuffer = rhi_->createBuffer(desc);
		}

		if (indexCount > frame.indexCapacity)
		{
			if (frame.indexBuffer != INVALID_HANDLE)
				rhi_->freeBuffer(frame.indexBuffer);

			frame.indexCapacity = indexCount + indexCount / 2;

			rhi::BufferDesc desc{};
			desc.size = (u64)frame.indexCapacity * sizeof(ImDrawIdx);
			desc.usage = rhi::EBufferUsage::eIndex;
			desc.memoryType = rhi::EMemoryType::eHostVisibleBuffer;

			frame.indexBuffer = rhi_->createBuffer(desc);
		}
	}

	void ImGuiRenderer::render(rhi::CommandBufferHandle cmd, u32 frameIndex)
	{
		const ImDrawData* drawData = ImGui::GetDrawData();
		if (!drawData || drawData->CmdListsCount == 0)
			return;

		const int framebufferWidth = (int)(drawData->DisplaySize.x * drawData->FramebufferScale.x);
		const int framebufferHeight = (int)(drawData->DisplaySize.y * drawData->FramebufferScale.y);
		if (framebufferWidth <= 0 || framebufferHeight <= 0)
			return;

		FrameBuffers& frame = frames_[frameIndex];
		ensureCapacity(frame, (u32)drawData->TotalVtxCount, (u32)drawData->TotalIdxCount);

		// All draw lists are packed into one vertex and one index buffer; per-list base
		// offsets are then folded into the draw calls.
		{
			ImDrawVert* vertexDst = static_cast<ImDrawVert*>(rhi_->mapBuffer(frame.vertexBuffer));
			ImDrawIdx* indexDst = static_cast<ImDrawIdx*>(rhi_->mapBuffer(frame.indexBuffer));

			for (int i = 0; i < drawData->CmdListsCount; i++)
			{
				const ImDrawList* list = drawData->CmdLists[i];

				memcpy(vertexDst, list->VtxBuffer.Data, (size_t)list->VtxBuffer.Size * sizeof(ImDrawVert));
				memcpy(indexDst, list->IdxBuffer.Data, (size_t)list->IdxBuffer.Size * sizeof(ImDrawIdx));

				vertexDst += list->VtxBuffer.Size;
				indexDst += list->IdxBuffer.Size;
			}

			rhi_->unmapBuffer(frame.indexBuffer);
			rhi_->unmapBuffer(frame.vertexBuffer);
		}

		// Screen space to clip space, D3D convention (+Y up in clip, Y down on screen).
		// The Vulkan backend flips its viewport, so the same matrix serves both.
		{
			const float L = drawData->DisplayPos.x;
			const float R = drawData->DisplayPos.x + drawData->DisplaySize.x;
			const float T = drawData->DisplayPos.y;
			const float B = drawData->DisplayPos.y + drawData->DisplaySize.y;

			const float projection[16] =
			{
				2.0f / (R - L),     0.0f,               0.0f, 0.0f,
				0.0f,               2.0f / (T - B),     0.0f, 0.0f,
				0.0f,               0.0f,               0.5f, 0.0f,
				(R + L) / (L - R),  (T + B) / (B - T),  0.5f, 1.0f,
			};

			rhi_->writeBuffer(frame.constantBuffer, projection, sizeof(projection), 0);
		}

		rhi_->bindGraphicsPipeline(cmd, pipeline_);
		rhi_->bindBindGroup(cmd, pipelineLayout_, 0, bindGroups_[frameIndex]);

		rhi_->setViewport(cmd, 0.0f, 0.0f, (float)framebufferWidth, (float)framebufferHeight);

		rhi_->bindVertexBuffer(cmd, 0, frame.vertexBuffer, sizeof(ImDrawVert), 0);
		rhi_->bindIndexBuffer(cmd, frame.indexBuffer, sizeof(ImDrawIdx) == 2 ? rhi::EIndexFormat::eUint16 : rhi::EIndexFormat::eUint32, 0);

		const ImVec2 clipOffset = drawData->DisplayPos;
		const ImVec2 clipScale = drawData->FramebufferScale;

		u32 globalVertexOffset = 0;
		u32 globalIndexOffset = 0;

		for (int i = 0; i < drawData->CmdListsCount; i++)
		{
			const ImDrawList* list = drawData->CmdLists[i];

			for (int c = 0; c < list->CmdBuffer.Size; c++)
			{
				const ImDrawCmd& drawCmd = list->CmdBuffer[c];

				if (drawCmd.UserCallback)
				{
					drawCmd.UserCallback(list, &drawCmd);
					continue;
				}

				float clipMinX = (drawCmd.ClipRect.x - clipOffset.x) * clipScale.x;
				float clipMinY = (drawCmd.ClipRect.y - clipOffset.y) * clipScale.y;
				float clipMaxX = (drawCmd.ClipRect.z - clipOffset.x) * clipScale.x;
				float clipMaxY = (drawCmd.ClipRect.w - clipOffset.y) * clipScale.y;

				clipMinX = (clipMinX < 0.0f) ? 0.0f : clipMinX;
				clipMinY = (clipMinY < 0.0f) ? 0.0f : clipMinY;
				clipMaxX = (clipMaxX > (float)framebufferWidth) ? (float)framebufferWidth : clipMaxX;
				clipMaxY = (clipMaxY > (float)framebufferHeight) ? (float)framebufferHeight : clipMaxY;

				if (clipMaxX <= clipMinX || clipMaxY <= clipMinY)
					continue;

				rhi_->setScissor(cmd, (s32)clipMinX, (s32)clipMinY, (u32)(clipMaxX - clipMinX), (u32)(clipMaxY - clipMinY));

				rhi_->drawIndexed(
					cmd,
					drawCmd.ElemCount,
					1,
					drawCmd.IdxOffset + globalIndexOffset,
					(s32)(drawCmd.VtxOffset + globalVertexOffset),
					0);
			}

			globalVertexOffset += (u32)list->VtxBuffer.Size;
			globalIndexOffset += (u32)list->IdxBuffer.Size;
		}

		// Leave the scissor covering the whole target so later passes are not clipped by
		// whatever the last ImGui command happened to set.
		rhi_->setScissor(cmd, 0, 0, (u32)framebufferWidth, (u32)framebufferHeight);
	}
}

