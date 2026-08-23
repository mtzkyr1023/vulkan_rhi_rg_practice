
#include "material/material_system.h"

#include "asset/gltf_loader.h"

namespace mv::material
{
	namespace
	{
		// The one place these numbers are declared. They must match the register
		// assignments in model.hlsl: set N maps onto HLSL space N, and a binding maps onto
		// the register of the matching type.
		//
		// set 0: per-frame scene data, plus the two virtual texture buffers. They belong
		// here rather than in set 1 because the unbounded texture array there takes every
		// t register above its base, and set 0 is already bound by every pass that samples
		// a material.
		constexpr u32 kSceneConstantsBinding = 0;
		constexpr u32 kVirtualTextureInfoBinding = 1;
		constexpr u32 kVirtualTexturePageTableBinding = 2;

		// set 1: everything global. The material buffer sits at t0 and the texture array
		// starts at t1, because an unbounded range swallows every register above its base.
		constexpr u32 kMaterialBufferBinding = 0;
		constexpr u32 kTextureArrayBinding = 1;
		constexpr u32 kSamplerArrayBinding = 2;

		// Vulkan needs a declared upper bound even for a "bindless" array; D3D12 treats the
		// range as unbounded and only uses this to size its descriptor table.
		constexpr u32 kMaxBindlessTextures = 4096;
		constexpr u32 kMaxMaterials = 4096;

		// Fixed sampler presets, indexed from the material data.
		constexpr u32 kSamplerCount = 4;

		constexpr u32 kSamplerLinearRepeat = 0;
		constexpr u32 kSamplerLinearClamp = 1;
		constexpr u32 kSamplerNearestRepeat = 2;
		constexpr u32 kSamplerNearestClamp = 3;

		// Clamped down by the backend if the device supports less.
		constexpr u32 kMaxAnisotropy = 16;
	}

	bool MaterialSystem::initialize(
		const std::shared_ptr<rhi::IRHI>& rhi,
		const ShaderCode& vs,
		const ShaderCode& ps,
		rhi::ETextureFormat colorFormat,
		rhi::ETextureFormat depthFormat)
	{
		if (!vs.bytecode || !ps.bytecode)
			return false;

		rhi_ = rhi;

		if (!rhi_->supportsBindless())
			return false;

		// set 0: per-frame scene constants. The vertex shader reads viewProj and the pixel
		// shader reads the light out of the same buffer, so both stages are declared.
		rhi::BindGroupLayoutDesc sceneLayoutDesc{};
		sceneLayoutDesc.bindings.push_back({
			.binding = kSceneConstantsBinding, .count = 1,
			.type = rhi::EDescriptorType::eUniformBuffer,
			.stages = rhi::EShaderStage::eVertex | rhi::EShaderStage::eFragment });
		sceneLayoutDesc.bindings.push_back({
			.binding = kVirtualTextureInfoBinding, .count = 1,
			.type = rhi::EDescriptorType::eStorageBuffer,
			.stages = rhi::EShaderStage::eFragment });
		sceneLayoutDesc.bindings.push_back({
			.binding = kVirtualTexturePageTableBinding, .count = 1,
			.type = rhi::EDescriptorType::eStorageBuffer,
			.stages = rhi::EShaderStage::eFragment });

		sceneLayout_ = rhi_->createBindGroupLayout(sceneLayoutDesc);

		// set 1: the material buffer, the bindless texture array and the sampler presets.
		rhi::BindGroupLayoutDesc bindlessLayoutDesc{};
		bindlessLayoutDesc.bindings.push_back({
			.binding = kMaterialBufferBinding, .count = 1,
			.type = rhi::EDescriptorType::eStorageBuffer,
			.stages = rhi::EShaderStage::eFragment });
		bindlessLayoutDesc.bindings.push_back({
			.binding = kTextureArrayBinding, .count = kMaxBindlessTextures,
			.type = rhi::EDescriptorType::eSampledImage,
			.stages = rhi::EShaderStage::eFragment,
			.bindless = true });
		bindlessLayoutDesc.bindings.push_back({
			.binding = kSamplerArrayBinding, .count = kSamplerCount,
			.type = rhi::EDescriptorType::eSampler,
			.stages = rhi::EShaderStage::eFragment });

		bindlessLayout_ = rhi_->createBindGroupLayout(bindlessLayoutDesc);

		rhi::PipelineLayoutDesc layoutDesc{};
		layoutDesc.bindGroups.push_back(sceneLayout_);
		layoutDesc.bindGroups.push_back(bindlessLayout_);
		layoutDesc.pushConstantSize = sizeof(DrawConstants);
		pipelineLayout_ = rhi_->createPipelineLayout(layoutDesc);

		const u8 white[4] = { 255, 255, 255, 255 };
		whiteTexture_ = createDefaultTexture(white);

		// (0, 0, 1) encoded into 0..1: a normal pointing straight out of the surface.
		const u8 flatNormal[4] = { 128, 128, 255, 255 };
		flatNormalTexture_ = createDefaultTexture(flatNormal);

		rhi::BufferDesc materialBufferDesc{};
		materialBufferDesc.size = (u64)kMaxMaterials * sizeof(GpuMaterial);
		materialBufferDesc.usage = rhi::EBufferUsage::eStorage;
		materialBufferDesc.memoryType = rhi::EMemoryType::eHostVisibleBuffer;

		materialBuffer_ = rhi_->createBuffer(materialBufferDesc);

		// The group is created before any texture exists: the array is partially bound and
		// its slots are filled in as textures register.
		rhi::BindGroupDesc groupDesc{};
		groupDesc.layout = bindlessLayout_;
		groupDesc.storageBuffers.push_back({
			.binding = kMaterialBufferBinding, .buffer = materialBuffer_,
			.offset = 0, .stride = sizeof(GpuMaterial), .count = kMaxMaterials });

		// The linear presets are anisotropic: surfaces seen at a grazing angle are where
		// mip selection alone blurs the most, and that is most of a scene like Sponza.
		groupDesc.samplers.push_back({ .binding = kSamplerArrayBinding, .sampler = { .filter = rhi::EFilterMode::eLinear,  .address = rhi::EAddressMode::eRepeat,      .maxAnisotropy = kMaxAnisotropy }, .arrayIndex = kSamplerLinearRepeat });
		groupDesc.samplers.push_back({ .binding = kSamplerArrayBinding, .sampler = { .filter = rhi::EFilterMode::eLinear,  .address = rhi::EAddressMode::eClampToEdge, .maxAnisotropy = kMaxAnisotropy }, .arrayIndex = kSamplerLinearClamp });
		groupDesc.samplers.push_back({ .binding = kSamplerArrayBinding, .sampler = { .filter = rhi::EFilterMode::eNearest, .address = rhi::EAddressMode::eRepeat },      .arrayIndex = kSamplerNearestRepeat });
		groupDesc.samplers.push_back({ .binding = kSamplerArrayBinding, .sampler = { .filter = rhi::EFilterMode::eNearest, .address = rhi::EAddressMode::eClampToEdge }, .arrayIndex = kSamplerNearestClamp });

		bindlessBindGroup_ = rhi_->createBindGroup(groupDesc);

		// Registered first so the neutral defaults occupy stable, always-valid slots.
		registerTexture(whiteTexture_);
		registerTexture(flatNormalTexture_);

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

		MaterialPipelineCache::Desc cacheDesc{};
		cacheDesc.vs = rhi_->createShader(vsDesc);
		cacheDesc.ps = rhi_->createShader(psDesc);
		cacheDesc.layout = pipelineLayout_;
		cacheDesc.colorFormat = colorFormat;
		cacheDesc.depthFormat = depthFormat;

		cacheDesc.vertexLayout.bindings.push_back({ .binding = 0, .stride = sizeof(asset::ModelVertex), .perInstance = false });
		cacheDesc.vertexLayout.attributes.push_back({
			.location = 0, .semanticName = "POSITION", .semanticIndex = 0,
			.binding = 0, .format = rhi::EVertexFormat::eFloat3, .offset = (u32)offsetof(asset::ModelVertex, position) });
		cacheDesc.vertexLayout.attributes.push_back({
			.location = 1, .semanticName = "NORMAL", .semanticIndex = 0,
			.binding = 0, .format = rhi::EVertexFormat::eFloat3, .offset = (u32)offsetof(asset::ModelVertex, normal) });
		cacheDesc.vertexLayout.attributes.push_back({
			.location = 2, .semanticName = "TEXCOORD", .semanticIndex = 0,
			.binding = 0, .format = rhi::EVertexFormat::eFloat2, .offset = (u32)offsetof(asset::ModelVertex, uv) });

		pipelineCache_.initialize(rhi_, cacheDesc);

		return true;
	}

	void MaterialSystem::deinitialize()
	{
		if (!rhi_) return;

		if (materialBuffer_ != INVALID_HANDLE)
		{
			rhi_->freeBuffer(materialBuffer_);
			materialBuffer_ = INVALID_HANDLE;
		}

		materials_.clear();
		textureIndices_.clear();

		pipelineCache_.deinitialize();

		rhi_.reset();
	}

	rhi::TextureHandle MaterialSystem::createDefaultTexture(const u8 pixel[4])
	{
		rhi::TextureDesc desc{};
		desc.width = 1;
		desc.height = 1;
		desc.depth = 1;
		desc.usage = rhi::ETextureUsage::eSampled | rhi::ETextureUsage::eTransferDst;
		// White reads as 1.0 whether it is decoded as sRGB or as linear, so one UNORM
		// texture can stand in for both kinds of missing map.
		desc.format = rhi::ETextureFormat::eR8G8B8A8_UNORM;
		desc.memoryType = rhi::EMemoryType::eDeviceLocalImage;

		const rhi::TextureHandle handle = rhi_->createTexture(desc);

		const rhi::TextureUpload upload{ pixel, 4 };
		rhi_->uploadTexture(handle, &upload, 1);

		return handle;
	}

	u32 MaterialSystem::registerTexture(rhi::TextureHandle texture)
	{
		if (texture == INVALID_HANDLE)
			return 0;

		const auto existing = textureIndices_.find(texture);
		if (existing != textureIndices_.end())
			return existing->second;

		const u32 index = (u32)textureIndices_.size();
		textureIndices_.emplace(texture, index);
		texturesByIndex_.push_back(texture);

		rhi_->updateBindGroupTexture(bindlessBindGroup_, kTextureArrayBinding, index, texture);

		return index;
	}

	void MaterialSystem::setTextureMipRange(u32 textureIndex, u32 baseMip)
	{
		if (textureIndex >= texturesByIndex_.size())
			return;

		rhi_->updateBindGroupTexture(
			bindlessBindGroup_, kTextureArrayBinding, textureIndex, texturesByIndex_[textureIndex], baseMip, 0);
	}

	void MaterialSystem::setForcedBaseMip(u32 baseMip)
	{
		for (u32 i = 0; i < (u32)texturesByIndex_.size(); i++)
		{
			setTextureMipRange(i, baseMip);
		}
	}

	u32 MaterialSystem::samplerIndexFor(const rhi::SamplerDesc& desc) const
	{
		const bool clamp = (desc.address == rhi::EAddressMode::eClampToEdge);

		if (desc.filter == rhi::EFilterMode::eNearest)
			return clamp ? kSamplerNearestClamp : kSamplerNearestRepeat;

		return clamp ? kSamplerLinearClamp : kSamplerLinearRepeat;
	}

	MaterialHandle MaterialSystem::createMaterial(const MaterialDesc& desc)
	{
		auto resolve = [&](rhi::TextureHandle texture, rhi::TextureHandle fallback)
		{
			return registerTexture((texture != INVALID_HANDLE) ? texture : fallback);
		};

		GpuMaterial gpu{};
		memcpy(gpu.baseColorFactor, desc.constants.baseColorFactor, sizeof(gpu.baseColorFactor));
		gpu.metallicFactor = desc.constants.metallicFactor;
		gpu.roughnessFactor = desc.constants.roughnessFactor;
		gpu.normalScale = desc.constants.normalScale;
		gpu.occlusionStrength = desc.constants.occlusionStrength;
		memcpy(gpu.emissiveFactor, desc.constants.emissiveFactor, sizeof(gpu.emissiveFactor));
		gpu.alphaCutoff = desc.constants.alphaCutoff;

		gpu.baseColorTexture = resolve(desc.baseColorTexture, whiteTexture_);
		gpu.metallicRoughnessTexture = resolve(desc.metallicRoughnessTexture, whiteTexture_);
		gpu.normalTexture = resolve(desc.normalTexture, flatNormalTexture_);
		gpu.occlusionTexture = resolve(desc.occlusionTexture, whiteTexture_);
		gpu.emissiveTexture = resolve(desc.emissiveTexture, whiteTexture_);
		gpu.samplerIndex = samplerIndexFor(desc.sampler);

		const MaterialHandle handle = (MaterialHandle)materials_.size();

		// The buffer is host visible and only written before rendering starts, so the entry
		// can go straight in at its index.
		rhi_->writeBuffer(materialBuffer_, &gpu, sizeof(gpu), (u64)handle * sizeof(GpuMaterial));

		Material material{};
		material.renderState = desc.renderState;
		material.pipeline = pipelineCache_.get(desc.renderState);

		materials_.push_back(material);

		return handle;
	}
}
