
#ifndef _MV_GLTF_LOADER_H_
#define _MV_GLTF_LOADER_H_

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "material/material_system.h"

#include "rhi/rhi.h"
#include "util/math.h"

// Forward declared so cgltf stays an implementation detail of the .cpp.
struct cgltf_image;
struct cgltf_texture;

namespace mv
{
	namespace asset
	{
		using namespace types;

		struct ModelVertex
		{
			f32 position[3];
			f32 normal[3];
			f32 uv[2];
		};

		struct ModelPrimitive
		{
			// Range within the model's merged index buffer.
			u32 firstIndex = 0;
			u32 indexCount = 0;

			material::MaterialHandle material = INVALID_HANDLE;
		};

		struct Model
		{
			// All primitives share one vertex and one index buffer. A visibility buffer
			// resolves triangles in a fullscreen pass with no idea which draw produced
			// them, so the geometry has to be addressable as a single global array; the
			// vertex offsets are already folded into the indices for the same reason.
			rhi::BufferHandle vertexBuffer = INVALID_HANDLE;
			rhi::BufferHandle indexBuffer = INVALID_HANDLE;

			u32 vertexCount = 0;
			u32 indexCount = 0;

			// Sorted so that opaque and masked primitives come first and blended ones last,
			// which is the order they have to be drawn in.
			std::vector<ModelPrimitive> primitives;

			// Axis-aligned bounds of the loaded geometry, so a camera can frame it without
			// the caller knowing anything about the file.
			math::Vec3 boundsMin{};
			math::Vec3 boundsMax{};

			u32 materialCount = 0;
			u32 textureCount = 0;
		};

		// The decoded pixels of one texture, mip 0 first. Kept only when the caller asks
		// for it: a virtual texture system has to slice pages out of the source long after
		// the upload, and re-decoding a PNG for every page is not an option.
		struct TextureSource
		{
			rhi::TextureHandle texture = INVALID_HANDLE;

			u32 width = 0;
			u32 height = 0;

			// Base colour and emissive maps are sRGB encoded, which changes how their pages
			// have to be resampled and which atlas they can share.
			bool srgb = false;

			std::vector<std::vector<u8>> levels;
		};

		// Loads a .gltf or .glb into GPU resources. Materials are registered with the
		// material system, which owns them from then on.
		//
		// Node transforms are baked into the vertex data rather than kept as per-draw
		// matrices: the sample renders static models, and this keeps the draw loop free of
		// per-primitive constant buffer updates.
		class GltfLoader
		{
		public:
			bool load(
				const std::shared_ptr<rhi::IRHI>& rhi,
				material::MaterialSystem& materialSystem,
				const std::string& path,
				Model& outModel);

			// Must be set before load(). Costs the full decoded size of every texture in
			// RAM, so it is opt-in.
			void setRetainSources(bool retain) { retainSources_ = retain; }

			const std::vector<TextureSource>& textureSources() const { return textureSources_; }

			// The sources are only needed while the virtual textures are being built.
			void releaseSources() { textureSources_.clear(); textureSources_.shrink_to_fit(); }

		private:
			// A glTF image is routinely referenced by several materials, and decoding and
			// uploading it once per reference is the difference between a few textures and
			// a few hundred on a scene like Sponza. Keyed per file, since the pointers are
			// only meaningful while that file's cgltf_data is alive.
			rhi::TextureHandle loadTexture(
				const std::shared_ptr<rhi::IRHI>& rhi,
				const cgltf_texture* texture,
				bool srgb,
				const std::string& baseDirectory,
				std::unordered_map<const cgltf_image*, rhi::TextureHandle>& cache);

		private:
			bool retainSources_ = false;

			std::vector<TextureSource> textureSources_;
		};
	}
}

#endif
