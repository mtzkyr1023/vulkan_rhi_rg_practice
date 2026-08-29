
// cgltf and stb_image use the classic CRT string and file functions, which /sdl promotes
// from a deprecation warning to an error. Scoped to this translation unit, which is the
// only place those two libraries are compiled.
#define _CRT_SECURE_NO_WARNINGS

#include "asset/gltf_loader.h"

#define CGLTF_IMPLEMENTATION
#include "cgltf.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "stb_image_resize2.h"

#include <Windows.h>

#include <algorithm>
#include <cfloat>

namespace mv::asset
{
	namespace
	{
		math::Mat4 nodeLocalMatrix(const cgltf_node* node)
		{
			// cgltf returns column-major data for the column-vector convention (v' = M * v),
			// where values[col * 4 + row] is M[row][col]. Our Mat4 is row-major for the
			// row-vector convention (v' = v * A), which needs A = transpose(M):
			//   A[r][c] = M[c][r] = values[r * 4 + c]
			// so the element order already matches and this is a straight copy, not a
			// transpose. Transposing here inverts every rotation and drops the translation.
			cgltf_float values[16];
			cgltf_node_transform_local(node, values);

			math::Mat4 result;
			for (u32 i = 0; i < 16; i++)
			{
				result.m[i] = (f32)values[i];
			}

			return result;
		}

		void accumulateBounds(const math::Vec3& p, math::Vec3& boundsMin, math::Vec3& boundsMax)
		{
			boundsMin.x = (p.x < boundsMin.x) ? p.x : boundsMin.x;
			boundsMin.y = (p.y < boundsMin.y) ? p.y : boundsMin.y;
			boundsMin.z = (p.z < boundsMin.z) ? p.z : boundsMin.z;

			boundsMax.x = (p.x > boundsMax.x) ? p.x : boundsMax.x;
			boundsMax.y = (p.y > boundsMax.y) ? p.y : boundsMax.y;
			boundsMax.z = (p.z > boundsMax.z) ? p.z : boundsMax.z;
		}

		rhi::SamplerDesc toSamplerDesc(const cgltf_sampler* sampler)
		{
			rhi::SamplerDesc desc{};
			if (!sampler)
				return desc;

			// glTF spells these as raw GL enums.
			constexpr int kNearest = 0x2600;
			constexpr int kNearestMipmapNearest = 0x2700;
			constexpr int kNearestMipmapLinear = 0x2702;
			constexpr int kClampToEdge = 0x812F;

			const int filter = sampler->mag_filter ? sampler->mag_filter : sampler->min_filter;
			if (filter == kNearest || filter == kNearestMipmapNearest || filter == kNearestMipmapLinear)
			{
				desc.filter = rhi::EFilterMode::eNearest;
			}

			if (sampler->wrap_s == kClampToEdge || sampler->wrap_t == kClampToEdge)
			{
				desc.address = rhi::EAddressMode::eClampToEdge;
			}

			return desc;
		}

		// Builds the mip chain on the CPU and returns the levels below the top one.
		//
		// Neither API offers a portable GPU downsample here: Vulkan has vkCmdBlitImage but
		// D3D12 has no blit at all, so a GPU path would mean a compute shader and per-mip
		// storage views. Doing it on the CPU keeps one implementation for both backends.
		//
		// srgb selects the sRGB-aware resize, because averaging sRGB-encoded texels
		// directly darkens the result.
		std::vector<std::vector<u8>> buildMipChain(const u8* pixels, u32 width, u32 height, bool srgb)
		{
			std::vector<std::vector<u8>> levels;

			const u8* source = pixels;
			u32 sourceWidth = width;
			u32 sourceHeight = height;

			while (sourceWidth > 1 || sourceHeight > 1)
			{
				const u32 levelWidth = (sourceWidth > 1) ? sourceWidth / 2 : 1;
				const u32 levelHeight = (sourceHeight > 1) ? sourceHeight / 2 : 1;

				std::vector<u8> level((size_t)levelWidth * levelHeight * 4);

				const stbir_pixel_layout layout = STBIR_RGBA;
				if (srgb)
				{
					stbir_resize_uint8_srgb(
						source, (int)sourceWidth, (int)sourceHeight, 0,
						level.data(), (int)levelWidth, (int)levelHeight, 0,
						layout);
				}
				else
				{
					stbir_resize_uint8_linear(
						source, (int)sourceWidth, (int)sourceHeight, 0,
						level.data(), (int)levelWidth, (int)levelHeight, 0,
						layout);
				}

				levels.push_back(std::move(level));

				source = levels.back().data();
				sourceWidth = levelWidth;
				sourceHeight = levelHeight;
			}

			return levels;
		}

		material::EAlphaMode toAlphaMode(cgltf_alpha_mode mode)
		{
			switch (mode)
			{
			case cgltf_alpha_mode_mask:  return material::EAlphaMode::eMask;
			case cgltf_alpha_mode_blend: return material::EAlphaMode::eBlend;
			case cgltf_alpha_mode_opaque:
			default:                     return material::EAlphaMode::eOpaque;
			}
		}
	}

	rhi::TextureHandle GltfLoader::loadTexture(
		const std::shared_ptr<rhi::IRHI>& rhi,
		const cgltf_texture* texture,
		bool srgb,
		const std::string& baseDirectory,
		std::unordered_map<const cgltf_image*, rhi::TextureHandle>& cache)
	{
		if (!texture || !texture->image)
			return INVALID_HANDLE;

		const cgltf_image* image = texture->image;

		const auto cached = cache.find(image);
		if (cached != cache.end())
			return cached->second;

		// glTF stores images as encoded PNG/JPEG, so they have to be decoded before upload.
		int width = 0;
		int height = 0;
		int channels = 0;
		stbi_uc* pixels = nullptr;

		if (image->buffer_view)
		{
			// .glb, or a .gltf with the image embedded in a buffer.
			const cgltf_buffer_view* view = image->buffer_view;
			const u8* encoded = static_cast<const u8*>(view->buffer->data) + view->offset;

			pixels = stbi_load_from_memory(encoded, (int)view->size, &width, &height, &channels, 4);
		}
		else if (image->uri && strncmp(image->uri, "data:", 5) != 0)
		{
			// A .gltf that keeps its images as separate files next to it. The URI is
			// percent-encoded and relative to the document, so it has to be decoded and
			// resolved before it names anything on disk.
			std::string uri = image->uri;
			cgltf_decode_uri(uri.data());
			uri.resize(strlen(uri.c_str()));

			const std::string path = baseDirectory + uri;

			pixels = stbi_load(path.c_str(), &width, &height, &channels, 4);
			if (!pixels)
			{
				OutputDebugStringA(("Failed to load image: " + path + "\n").c_str());
			}
		}

		if (!pixels)
			return INVALID_HANDLE;

		rhi::TextureDesc desc{};
		desc.width = (u32)width;
		desc.height = (u32)height;
		desc.depth = 1;
		desc.usage = rhi::ETextureUsage::eSampled | rhi::ETextureUsage::eTransferDst;
		// 0 asks for a full chain. Without mips, minified surfaces alias badly at grazing
		// angles, which is exactly where a scene like Sponza spends most of its pixels.
		desc.mipLevels = 0;
		// Colour maps are authored in sRGB and must be decoded to linear before lighting;
		// data maps (metallic-roughness, normal, occlusion) are already linear.
		desc.format = srgb ? rhi::ETextureFormat::eR8G8B8A8_SRGB : rhi::ETextureFormat::eR8G8B8A8_UNORM;
		desc.memoryType = rhi::EMemoryType::eDeviceLocalImage;

		const rhi::TextureHandle handle = rhi->createTexture(desc);

		const std::vector<std::vector<u8>> mips = buildMipChain(pixels, (u32)width, (u32)height, srgb);

		std::vector<rhi::TextureUpload> uploads;
		uploads.reserve(mips.size() + 1);
		uploads.push_back({ pixels, (u64)width * height * 4 });
		for (const auto& level : mips)
		{
			uploads.push_back({ level.data(), (u64)level.size() });
		}

		rhi->uploadTexture(handle, uploads.data(), (u32)uploads.size());

		if (retainSources_)
		{
			TextureSource source{};
			source.texture = handle;
			source.width = (u32)width;
			source.height = (u32)height;
			source.srgb = srgb;

			// mip 0 comes from stb's buffer, which is freed below, so it has to be copied.
			source.levels.reserve(mips.size() + 1);
			source.levels.emplace_back(pixels, pixels + (size_t)width * height * 4);
			for (const auto& level : mips)
			{
				source.levels.push_back(level);
			}

			textureSources_.push_back(std::move(source));
		}

		stbi_image_free(pixels);

		cache.emplace(image, handle);

		return handle;
	}

	u32 GltfLoader::registerMaterials(
		const std::shared_ptr<rhi::IRHI>& rhi,
		material::MaterialSystem& materialSystem,
		const cgltf_data* data,
		const std::string& baseDirectory,
		std::vector<material::MaterialHandle>& outMaterials,
		u32& outTextureCount)
	{
		std::unordered_map<const cgltf_image*, rhi::TextureHandle> textureCache;

		outMaterials.reserve(data->materials_count + 1);

		for (cgltf_size i = 0; i < data->materials_count; i++)
		{
			const cgltf_material& source = data->materials[i];

			material::MaterialDesc desc{};
			desc.renderState.alphaMode = toAlphaMode(source.alpha_mode);
			desc.renderState.doubleSided = source.double_sided != 0;

			// Only masked materials clip; leaving the cutoff at zero makes the shared
			// shader a no-op for the other modes.
			if (desc.renderState.alphaMode == material::EAlphaMode::eMask)
			{
				desc.constants.alphaCutoff = (f32)source.alpha_cutoff;
			}

			if (source.has_pbr_metallic_roughness)
			{
				const cgltf_pbr_metallic_roughness& pbr = source.pbr_metallic_roughness;

				for (u32 c = 0; c < 4; c++)
				{
					desc.constants.baseColorFactor[c] = (f32)pbr.base_color_factor[c];
				}
				desc.constants.metallicFactor = (f32)pbr.metallic_factor;
				desc.constants.roughnessFactor = (f32)pbr.roughness_factor;

				desc.baseColorTexture = loadTexture(rhi, pbr.base_color_texture.texture, true, baseDirectory, textureCache);
				desc.metallicRoughnessTexture = loadTexture(rhi, pbr.metallic_roughness_texture.texture, false, baseDirectory, textureCache);

				if (pbr.base_color_texture.texture)
				{
					desc.sampler = toSamplerDesc(pbr.base_color_texture.texture->sampler);
				}
			}

			desc.normalTexture = loadTexture(rhi, source.normal_texture.texture, false, baseDirectory, textureCache);
			if (source.normal_texture.texture)
			{
				desc.constants.normalScale = (f32)source.normal_texture.scale;
			}

			desc.occlusionTexture = loadTexture(rhi, source.occlusion_texture.texture, false, baseDirectory, textureCache);
			if (source.occlusion_texture.texture)
			{
				desc.constants.occlusionStrength = (f32)source.occlusion_texture.scale;
			}

			desc.emissiveTexture = loadTexture(rhi, source.emissive_texture.texture, true, baseDirectory, textureCache);
			for (u32 c = 0; c < 3; c++)
			{
				desc.constants.emissiveFactor[c] = (f32)source.emissive_factor[c];
			}
			if (source.has_emissive_strength)
			{
				for (u32 c = 0; c < 3; c++)
				{
					desc.constants.emissiveFactor[c] *= (f32)source.emissive_strength.emissive_strength;
				}
			}

			outMaterials.push_back(materialSystem.createMaterial(desc));
		}

		// Primitives with no material fall back to this one, appended last.
		const u32 defaultMaterialIndex = (u32)outMaterials.size();
		{
			material::MaterialDesc desc{};
			// An untextured default reads better as a rough dielectric than as a mirror.
			desc.constants.metallicFactor = 0.0f;
			desc.constants.roughnessFactor = 0.8f;

			outMaterials.push_back(materialSystem.createMaterial(desc));
		}

		outTextureCount = (u32)textureCache.size();

		return defaultMaterialIndex;
	}

	bool GltfLoader::load(
		const std::shared_ptr<rhi::IRHI>& rhi,
		material::MaterialSystem& materialSystem,
		const std::string& path,
		Model& outModel)
	{
		cgltf_options options{};
		cgltf_data* data = nullptr;

		if (cgltf_parse_file(&options, path.c_str(), &data) != cgltf_result_success)
			return false;

		// Pulls in external .bin files and, for .glb, points the buffers at the embedded
		// chunk. Without this the accessors have nothing to read.
		if (cgltf_load_buffers(&options, data, path.c_str()) != cgltf_result_success)
		{
			cgltf_free(data);
			return false;
		}

		outModel.boundsMin = { FLT_MAX, FLT_MAX, FLT_MAX };
		outModel.boundsMax = { -FLT_MAX, -FLT_MAX, -FLT_MAX };

		// External image URIs are relative to the document, not to the working directory.
		std::string baseDirectory = path;
		const size_t slash = baseDirectory.find_last_of("/\\");
		baseDirectory = (slash == std::string::npos) ? std::string() : baseDirectory.substr(0, slash + 1);

		// --- materials -------------------------------------------------------------

		std::vector<material::MaterialHandle> materials;
		u32 textureCount = 0;

		const u32 defaultMaterialIndex =
			registerMaterials(rhi, materialSystem, data, baseDirectory, materials, textureCount);

		outModel.materialCount = (u32)materials.size();
		outModel.textureCount = textureCount;

		// --- geometry --------------------------------------------------------------

		// Everything accumulates into these and is uploaded once at the end.
		std::vector<ModelVertex> mergedVertices;
		std::vector<u32> mergedIndices;

		std::vector<ModelVertex> vertices;
		std::vector<u32> indices;

		for (cgltf_size n = 0; n < data->nodes_count; n++)
		{
			const cgltf_node& node = data->nodes[n];
			if (!node.mesh)
				continue;

			// Walk up to the root so parent transforms are included.
			math::Mat4 world = math::Mat4::identity();
			for (const cgltf_node* current = &node; current; current = current->parent)
			{
				world = world * nodeLocalMatrix(current);
			}
			const math::Mat4 normalTransform = math::normalMatrix(world);

			for (cgltf_size p = 0; p < node.mesh->primitives_count; p++)
			{
				const cgltf_primitive& primitive = node.mesh->primitives[p];

				if (primitive.type != cgltf_primitive_type_triangles || !primitive.indices)
					continue;

				const cgltf_accessor* positionAccessor = nullptr;
				const cgltf_accessor* normalAccessor = nullptr;
				const cgltf_accessor* uvAccessor = nullptr;

				for (cgltf_size a = 0; a < primitive.attributes_count; a++)
				{
					const cgltf_attribute& attribute = primitive.attributes[a];
					if (attribute.type == cgltf_attribute_type_position) positionAccessor = attribute.data;
					else if (attribute.type == cgltf_attribute_type_normal) normalAccessor = attribute.data;
					else if (attribute.type == cgltf_attribute_type_texcoord && attribute.index == 0) uvAccessor = attribute.data;
				}

				if (!positionAccessor)
					continue;

				const cgltf_size vertexCount = positionAccessor->count;

				vertices.clear();
				vertices.resize(vertexCount);

				for (cgltf_size v = 0; v < vertexCount; v++)
				{
					f32 position[3] = { 0.0f, 0.0f, 0.0f };
					cgltf_accessor_read_float(positionAccessor, v, position, 3);

					// Baking the transform in means the draw loop needs no model matrix.
					const math::Vec3 worldPosition = math::transformPoint(world, { position[0], position[1], position[2] });
					vertices[v].position[0] = worldPosition.x;
					vertices[v].position[1] = worldPosition.y;
					vertices[v].position[2] = worldPosition.z;

					accumulateBounds(worldPosition, outModel.boundsMin, outModel.boundsMax);

					if (normalAccessor)
					{
						f32 normal[3] = { 0.0f, 1.0f, 0.0f };
						cgltf_accessor_read_float(normalAccessor, v, normal, 3);

						const math::Vec3 worldNormal = math::normalize(
							math::transformDirection(normalTransform, { normal[0], normal[1], normal[2] }));

						vertices[v].normal[0] = worldNormal.x;
						vertices[v].normal[1] = worldNormal.y;
						vertices[v].normal[2] = worldNormal.z;
					}
					else
					{
						vertices[v].normal[1] = 1.0f;
					}

					if (uvAccessor)
					{
						cgltf_accessor_read_float(uvAccessor, v, vertices[v].uv, 2);
					}
				}

				const cgltf_size indexCount = primitive.indices->count;
				indices.clear();
				indices.resize(indexCount);
				cgltf_accessor_unpack_indices(primitive.indices, indices.data(), sizeof(u32), indexCount);

				ModelPrimitive out{};
				out.firstIndex = (u32)mergedIndices.size();
				out.indexCount = (u32)indexCount;
				out.material = primitive.material
					? materials[cgltf_material_index(data, primitive.material)]
					: materials[defaultMaterialIndex];

				// Folding the vertex offset into the indices keeps the shading pass a plain
				// lookup into one global array, with no per-draw base to apply.
				const u32 vertexOffset = (u32)mergedVertices.size();
				for (u32& index : indices)
				{
					index += vertexOffset;
				}

				mergedVertices.insert(mergedVertices.end(), vertices.begin(), vertices.end());
				mergedIndices.insert(mergedIndices.end(), indices.begin(), indices.end());

				outModel.primitives.push_back(out);
			}
		}

		cgltf_free(data);

		if (mergedIndices.empty())
			return false;

		outModel.vertexCount = (u32)mergedVertices.size();
		outModel.indexCount = (u32)mergedIndices.size();

		// eStorage as well as eVertex/eIndex: the shading pass reads the same buffers as
		// structured buffers rather than through the input assembler.
		rhi::BufferDesc vertexDesc{};
		vertexDesc.size = mergedVertices.size() * sizeof(ModelVertex);
		vertexDesc.usage = rhi::EBufferUsage::eVertex | rhi::EBufferUsage::eStorage | rhi::EBufferUsage::eTransferDst;
		vertexDesc.memoryType = rhi::EMemoryType::eDeviceLocalBuffer;

		outModel.vertexBuffer = rhi->createBuffer(vertexDesc);
		rhi->uploadBuffer(outModel.vertexBuffer, mergedVertices.data(), vertexDesc.size);

		// Always 32-bit: cgltf can unpack any of glTF's index widths into it, so neither the
		// draw path nor the shading pass has to care which the file used.
		rhi::BufferDesc indexDesc{};
		indexDesc.size = mergedIndices.size() * sizeof(u32);
		indexDesc.usage = rhi::EBufferUsage::eIndex | rhi::EBufferUsage::eStorage | rhi::EBufferUsage::eTransferDst;
		indexDesc.memoryType = rhi::EMemoryType::eDeviceLocalBuffer;

		outModel.indexBuffer = rhi->createBuffer(indexDesc);
		rhi->uploadBuffer(outModel.indexBuffer, mergedIndices.data(), indexDesc.size);

		if (retainGeometry_)
			outModel.cpuVertices = std::move(mergedVertices);

		// Blended geometry has to come after everything it can show through. Sorting the
		// rest by material as well keeps pipeline and bind group changes down.
		std::stable_sort(
			outModel.primitives.begin(),
			outModel.primitives.end(),
			[&](const ModelPrimitive& a, const ModelPrimitive& b)
			{
				const auto modeA = materialSystem.material(a.material).renderState.alphaMode;
				const auto modeB = materialSystem.material(b.material).renderState.alphaMode;

				const bool blendA = (modeA == material::EAlphaMode::eBlend);
				const bool blendB = (modeB == material::EAlphaMode::eBlend);

				if (blendA != blendB)
					return blendB;

				return a.material < b.material;
			});

		return !outModel.primitives.empty();
	}

	bool GltfLoader::loadSkinned(
		const std::shared_ptr<rhi::IRHI>& rhi,
		material::MaterialSystem& materialSystem,
		const std::string& path,
		SkinnedModel& outModel)
	{
		cgltf_options options{};
		cgltf_data* data = nullptr;

		if (cgltf_parse_file(&options, path.c_str(), &data) != cgltf_result_success)
		{
			OutputDebugStringA("loadSkinned: parse failed\n");
			return false;
		}

		if (cgltf_load_buffers(&options, data, path.c_str()) != cgltf_result_success)
		{
			OutputDebugStringA("loadSkinned: buffers failed\n");
			cgltf_free(data);
			return false;
		}

		if (data->skins_count == 0)
		{
			OutputDebugStringA("loadSkinned: no skins\n");
			cgltf_free(data);
			return false;
		}

		std::string baseDirectory = path;
		const size_t slash = baseDirectory.find_last_of("/\\");
		baseDirectory = (slash == std::string::npos) ? std::string() : baseDirectory.substr(0, slash + 1);

		std::vector<material::MaterialHandle> materials;
		u32 textureCount = 0;

		const u32 defaultMaterialIndex =
			registerMaterials(rhi, materialSystem, data, baseDirectory, materials, textureCount);

		// --- skeleton --------------------------------------------------------------

		// One skin per model is all this supports, which is all the assets have.
		const cgltf_skin& skin = data->skins[0];

		std::unordered_map<const cgltf_node*, u32> jointIndexOf;

		for (cgltf_size j = 0; j < skin.joints_count; j++)
			jointIndexOf.emplace(skin.joints[j], (u32)j);

		outModel.joints.resize(skin.joints_count);

		for (cgltf_size j = 0; j < skin.joints_count; j++)
		{
			const cgltf_node* node = skin.joints[j];
			SkinnedJoint& joint = outModel.joints[j];

			const auto parent = node->parent ? jointIndexOf.find(node->parent) : jointIndexOf.end();
			joint.parent = parent != jointIndexOf.end() ? (s32)parent->second : -1;

			// glTF's column-major column-vector matrix is byte-for-byte this codebase's
			// row-major row-vector one -- the same transpose-of-a-transpose that makes
			// Bullet's getOpenGLMatrix a straight copy.
			if (skin.inverse_bind_matrices)
			{
				f32 raw[16];
				cgltf_accessor_read_float(skin.inverse_bind_matrices, j, raw, 16);
				std::memcpy(joint.inverseBind.m, raw, sizeof(raw));
			}

			if (node->has_translation)
				joint.baseTranslation = { node->translation[0], node->translation[1], node->translation[2] };

			if (node->has_rotation)
				joint.baseRotation = { node->rotation[0], node->rotation[1], node->rotation[2], node->rotation[3] };

			if (node->has_scale)
				joint.baseScale = { node->scale[0], node->scale[1], node->scale[2] };
		}

		// Parents before children: glTF's joint array promises nothing, and global
		// transforms are a single linear pass once this order exists.
		{
			std::vector<bool> placed(outModel.joints.size(), false);

			outModel.jointOrder.reserve(outModel.joints.size());

			while (outModel.jointOrder.size() < outModel.joints.size())
			{
				const size_t before = outModel.jointOrder.size();

				for (u32 j = 0; j < (u32)outModel.joints.size(); j++)
				{
					const s32 parent = outModel.joints[j].parent;

					if (!placed[j] && (parent < 0 || placed[parent]))
					{
						outModel.jointOrder.push_back(j);
						placed[j] = true;
					}
				}

				// A cycle would spin forever; a skeleton with one is not worth loading.
				if (outModel.jointOrder.size() == before)
				{
					OutputDebugStringA("loadSkinned: joint cycle\n");
					cgltf_free(data);
					return false;
				}
			}
		}

		// --- animations ------------------------------------------------------------

		for (cgltf_size a = 0; a < data->animations_count; a++)
		{
			const cgltf_animation& animation = data->animations[a];

			AnimClip clip{};
			clip.name = animation.name ? animation.name : ("clip " + std::to_string(a));

			for (cgltf_size c = 0; c < animation.channels_count; c++)
			{
				const cgltf_animation_channel& source = animation.channels[c];

				const auto joint = jointIndexOf.find(source.target_node);

				if (joint == jointIndexOf.end() || source.sampler == nullptr)
					continue;

				EAnimPath channelPath;
				u32 stride;

				switch (source.target_path)
				{
				case cgltf_animation_path_type_translation: channelPath = EAnimPath::eTranslation; stride = 3; break;
				case cgltf_animation_path_type_rotation:    channelPath = EAnimPath::eRotation; stride = 4; break;
				case cgltf_animation_path_type_scale:       channelPath = EAnimPath::eScale; stride = 3; break;
				default: continue;
				}

				AnimChannel channel{};
				channel.joint = joint->second;
				channel.path = channelPath;

				const cgltf_accessor* input = source.sampler->input;
				const cgltf_accessor* output = source.sampler->output;

				channel.times.resize(input->count);
				channel.values.resize(output->count * stride);

				for (cgltf_size k = 0; k < input->count; k++)
					cgltf_accessor_read_float(input, k, &channel.times[k], 1);

				// Cubic samplers store in/value/out triplets; reading only the value
				// keys and lerping them is the cheap approximation this settles for.
				for (cgltf_size k = 0; k < output->count; k++)
					cgltf_accessor_read_float(output, k, &channel.values[k * stride], stride);

				if (!channel.times.empty())
					clip.duration = (std::max)(clip.duration, channel.times.back());

				clip.channels.push_back(std::move(channel));
			}

			if (!clip.channels.empty())
				outModel.clips.push_back(std::move(clip));
		}

		// --- geometry --------------------------------------------------------------

		// Mesh space, raw: the spec says a skinned mesh ignores its node's transform
		// -- the joints alone place every vertex.
		outModel.boundsMin = { FLT_MAX, FLT_MAX, FLT_MAX };
		outModel.boundsMax = { -FLT_MAX, -FLT_MAX, -FLT_MAX };

		std::vector<SkinnedVertex> mergedVertices;
		std::vector<u32> mergedIndices;
		std::vector<u32> indices;

		for (cgltf_size n = 0; n < data->nodes_count; n++)
		{
			const cgltf_node& node = data->nodes[n];

			if (!node.mesh || !node.skin)
				continue;

			for (cgltf_size p = 0; p < node.mesh->primitives_count; p++)
			{
				const cgltf_primitive& primitive = node.mesh->primitives[p];

				// No index requirement here, unlike load(): the Khronos Fox -- the one
				// skinned sample everyone reaches for first -- ships non-indexed, and
				// sequential indices are one line to synthesise.
				if (primitive.type != cgltf_primitive_type_triangles)
					continue;

				const cgltf_accessor* positionAccessor = nullptr;
				const cgltf_accessor* normalAccessor = nullptr;
				const cgltf_accessor* uvAccessor = nullptr;
				const cgltf_accessor* jointsAccessor = nullptr;
				const cgltf_accessor* weightsAccessor = nullptr;

				for (cgltf_size at = 0; at < primitive.attributes_count; at++)
				{
					const cgltf_attribute& attribute = primitive.attributes[at];

					if (attribute.type == cgltf_attribute_type_position) positionAccessor = attribute.data;
					else if (attribute.type == cgltf_attribute_type_normal) normalAccessor = attribute.data;
					else if (attribute.type == cgltf_attribute_type_texcoord && attribute.index == 0) uvAccessor = attribute.data;
					else if (attribute.type == cgltf_attribute_type_joints && attribute.index == 0) jointsAccessor = attribute.data;
					else if (attribute.type == cgltf_attribute_type_weights && attribute.index == 0) weightsAccessor = attribute.data;
				}

				if (!positionAccessor || !jointsAccessor || !weightsAccessor)
					continue;

				const u32 vertexOffset = (u32)mergedVertices.size();
				const cgltf_size vertexCount = positionAccessor->count;

				for (cgltf_size v = 0; v < vertexCount; v++)
				{
					SkinnedVertex vertex{};

					cgltf_accessor_read_float(positionAccessor, v, vertex.position, 3);

					accumulateBounds(
						{ vertex.position[0], vertex.position[1], vertex.position[2] },
						outModel.boundsMin, outModel.boundsMax);

					vertex.normal[1] = 1.0f;

					if (normalAccessor)
						cgltf_accessor_read_float(normalAccessor, v, vertex.normal, 3);

					if (uvAccessor)
						cgltf_accessor_read_float(uvAccessor, v, vertex.uv, 2);

					// Indices as floats: exact for any joint count a vertex fetch meets,
					// and eFloat4 exists on every backend where an int format may not.
					cgltf_accessor_read_float(jointsAccessor, v, vertex.joints, 4);
					cgltf_accessor_read_float(weightsAccessor, v, vertex.weights, 4);

					mergedVertices.push_back(vertex);
				}

				const cgltf_size indexCount = primitive.indices ? primitive.indices->count : vertexCount;
				indices.clear();
				indices.resize(indexCount);

				if (primitive.indices)
				{
					cgltf_accessor_unpack_indices(primitive.indices, indices.data(), sizeof(u32), indexCount);
				}
				else
				{
					for (cgltf_size i = 0; i < indexCount; i++)
						indices[i] = (u32)i;
				}

				ModelPrimitive out{};
				out.firstIndex = (u32)mergedIndices.size();
				out.indexCount = (u32)indexCount;
				out.material = primitive.material
					? materials[cgltf_material_index(data, primitive.material)]
					: materials[defaultMaterialIndex];

				for (u32& index : indices)
					index += vertexOffset;

				mergedIndices.insert(mergedIndices.end(), indices.begin(), indices.end());

				outModel.primitives.push_back(out);
			}
		}

		cgltf_free(data);

		if (mergedIndices.empty())
		{
			OutputDebugStringA("loadSkinned: no skinned geometry\n");
			return false;
		}

		outModel.vertexCount = (u32)mergedVertices.size();
		outModel.indexCount = (u32)mergedIndices.size();

		rhi::BufferDesc vertexDesc{};
		vertexDesc.size = mergedVertices.size() * sizeof(SkinnedVertex);
		vertexDesc.usage = rhi::EBufferUsage::eVertex | rhi::EBufferUsage::eTransferDst;
		vertexDesc.memoryType = rhi::EMemoryType::eDeviceLocalBuffer;

		outModel.vertexBuffer = rhi->createBuffer(vertexDesc);
		rhi->uploadBuffer(outModel.vertexBuffer, mergedVertices.data(), vertexDesc.size);

		rhi::BufferDesc indexDesc{};
		indexDesc.size = mergedIndices.size() * sizeof(u32);
		indexDesc.usage = rhi::EBufferUsage::eIndex | rhi::EBufferUsage::eTransferDst;
		indexDesc.memoryType = rhi::EMemoryType::eDeviceLocalBuffer;

		outModel.indexBuffer = rhi->createBuffer(indexDesc);
		rhi->uploadBuffer(outModel.indexBuffer, mergedIndices.data(), indexDesc.size);

		return true;
	}
}

