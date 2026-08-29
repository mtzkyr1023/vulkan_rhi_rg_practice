
#ifndef _MV_MATERIAL_SYSTEM_H_
#define _MV_MATERIAL_SYSTEM_H_

#include <memory>
#include <unordered_map>
#include <vector>

#include "material/pipeline.h"

#include "rhi/rhi.h"

#include "util/types.h"

namespace mv
{
	namespace material
	{
		using namespace types;

		using MaterialHandle = u32;

		// The CPU-side material description. Texture slots are RHI handles here and are
		// turned into bindless indices when the material is registered.
		struct MaterialConstants
		{
			f32 baseColorFactor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };

			f32 metallicFactor = 1.0f;
			f32 roughnessFactor = 1.0f;
			f32 normalScale = 1.0f;
			f32 occlusionStrength = 1.0f;

			f32 emissiveFactor[3] = { 0.0f, 0.0f, 0.0f };

			// Zero for opaque and blended materials, which never clip.
			f32 alphaCutoff = 0.0f;
		};

		// Must match the GpuMaterial struct in model.hlsl, including padding.
		struct GpuMaterial
		{
			f32 baseColorFactor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };

			f32 metallicFactor = 1.0f;
			f32 roughnessFactor = 1.0f;
			f32 normalScale = 1.0f;
			f32 occlusionStrength = 1.0f;

			f32 emissiveFactor[3] = { 0.0f, 0.0f, 0.0f };
			f32 alphaCutoff = 0.0f;

			u32 baseColorTexture = 0;
			u32 metallicRoughnessTexture = 0;
			u32 normalTexture = 0;
			u32 occlusionTexture = 0;

			u32 samplerIndex = 0;
			u32 emissiveTexture = 0;
			u32 pad[2] = { 0, 0 };
		};

		// Any texture left INVALID_HANDLE is filled in with the matching neutral default,
		// so callers only supply the maps their material actually has.
		struct MaterialDesc
		{
			rhi::TextureHandle baseColorTexture = INVALID_HANDLE;
			rhi::TextureHandle metallicRoughnessTexture = INVALID_HANDLE;
			rhi::TextureHandle normalTexture = INVALID_HANDLE;
			rhi::TextureHandle occlusionTexture = INVALID_HANDLE;
			rhi::TextureHandle emissiveTexture = INVALID_HANDLE;

			rhi::SamplerDesc sampler;

			MaterialConstants constants;
			MaterialRenderState renderState;
		};

		struct Material
		{
			rhi::PipelineHandle pipeline = INVALID_HANDLE;

			MaterialRenderState renderState;
		};

		// Owns the shader binding contract for materials.
		//
		// Materials have no descriptors of their own: every texture lives in one bindless
		// array and every material's parameters live in one structured buffer, so a draw
		// only needs its material index, passed as a push constant. Set 0 and set 1 are
		// bound once per frame rather than once per material.
		class MaterialSystem
		{
		public:
			struct ShaderCode
			{
				const u32* bytecode = nullptr;
				u32 size = 0;
			};

			// What a draw passes as its push constant. drawIndex is only meaningful to the
			// visibility buffer pass, which writes it out for the resolve pass to look up.
			struct DrawConstants
			{
				u32 drawIndex = 0;
				u32 materialIndex = 0;

				// Only the shadow pass uses this, but the layout is shared, so every pass
				// pushes the same struct.
				u32 cascadeIndex = 0;
			};

			bool initialize(
				const std::shared_ptr<rhi::IRHI>& rhi,
				const ShaderCode& vs,
				const ShaderCode& ps,
				const std::vector<rhi::ETextureFormat>& colorFormats,
				rhi::ETextureFormat depthFormat);

			void deinitialize();

			// Idempotent: the same texture always maps to the same slot.
			u32 registerTexture(rhi::TextureHandle texture);

			// Re-points an existing bindless slot at a different texture, so materials that
			// already hold the index keep working. This is what lets a generator rebuild
			// its maps at a new size without inventing a new material every time.
			void replaceTexture(u32 textureIndex, rhi::TextureHandle texture);

			// Re-points a bindless slot at a subrange of its texture's mip chain. This is
			// how a streaming system exposes only the levels that are currently resident.
			void setTextureMipRange(u32 textureIndex, u32 baseMip);

			// Applies the same base mip to every registered texture, for testing the path.
			void setForcedBaseMip(u32 baseMip);

			MaterialHandle createMaterial(const MaterialDesc& desc);

			const Material& material(MaterialHandle handle) const { return materials_[handle]; }
			u32 materialCount() const { return (u32)materials_.size(); }
			u32 textureCount() const { return (u32)textureIndices_.size(); }

			rhi::BindGroupLayoutHandle sceneLayout() const { return sceneLayout_; }
			rhi::BindGroupLayoutHandle bindlessLayout() const { return bindlessLayout_; }
			rhi::PipelineLayoutHandle pipelineLayout() const { return pipelineLayout_; }

			rhi::BindGroupHandle bindlessBindGroup() const { return bindlessBindGroup_; }

			// 1x1 neutral maps, also useful to callers building materials by hand.
			rhi::TextureHandle whiteTexture() const { return whiteTexture_; }
			rhi::TextureHandle flatNormalTexture() const { return flatNormalTexture_; }

		private:
			rhi::TextureHandle createDefaultTexture(const u8 pixel[4]);

			u32 samplerIndexFor(const rhi::SamplerDesc& desc) const;

		private:
			std::shared_ptr<rhi::IRHI> rhi_;

			rhi::BindGroupLayoutHandle sceneLayout_ = INVALID_HANDLE;
			rhi::BindGroupLayoutHandle bindlessLayout_ = INVALID_HANDLE;
			rhi::PipelineLayoutHandle pipelineLayout_ = INVALID_HANDLE;

			rhi::BindGroupHandle bindlessBindGroup_ = INVALID_HANDLE;

			// Holds one GpuMaterial per material, indexed by MaterialHandle.
			rhi::BufferHandle materialBuffer_ = INVALID_HANDLE;

			rhi::TextureHandle whiteTexture_ = INVALID_HANDLE;
			rhi::TextureHandle flatNormalTexture_ = INVALID_HANDLE;

			std::unordered_map<rhi::TextureHandle, u32> textureIndices_;

			// The reverse mapping, so a bindless slot can be re-pointed by index.
			std::vector<rhi::TextureHandle> texturesByIndex_;

			MaterialPipelineCache pipelineCache_;

			std::vector<Material> materials_;
		};
	}
}

#endif
