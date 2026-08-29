#include "terrain/terrain.h"

#include "util/parallel.h"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace mv::terrain
{
	namespace
	{
		inline f32 lerp(f32 a, f32 b, f32 t)
		{
			return a + (b - a) * t;
		}

		inline f32 saturate(f32 v)
		{
			return std::clamp(v, 0.0f, 1.0f);
		}

		// Hermite fade between two thresholds, the CPU twin of HLSL's smoothstep. Used for
		// every material boundary so the blends have no visible seam.
		inline f32 smoothstep(f32 edge0, f32 edge1, f32 x)
		{
			if (edge1 <= edge0)
				return (x < edge0) ? 0.0f : 1.0f;

			const f32 t = saturate((x - edge0) / (edge1 - edge0));

			return t * t * (3.0f - 2.0f * t);
		}

		struct Rgb
		{
			f32 r, g, b;
		};

		inline Rgb mixColor(const Rgb& a, const Rgb& b, f32 t)
		{
			return { lerp(a.r, b.r, t), lerp(a.g, b.g, t), lerp(a.b, b.b, t) };
		}

		inline u8 toByte(f32 v)
		{
			return (u8)std::clamp((s32)std::lround(v * 255.0f), 0, 255);
		}
	}

	f32 Heightmap::sample(f32 u, f32 v) const
	{
		if (width == 0 || height == 0)
			return 0.0f;

		// Sample positions sit at texel centres, so the continuous coordinate is offset by
		// half a texel before the floor. Without it the whole field shifts by half a texel
		// against the mesh, and the baked normals stop matching the geometry.
		const f32 x = saturate(u) * (f32)width - 0.5f;
		const f32 y = saturate(v) * (f32)height - 0.5f;

		const f32 fx = std::floor(x);
		const f32 fy = std::floor(y);

		const s32 x0 = (s32)fx;
		const s32 y0 = (s32)fy;

		const f32 tx = x - fx;
		const f32 ty = y - fy;

		const u32 cx0 = (u32)std::clamp(x0, 0, (s32)width - 1);
		const u32 cy0 = (u32)std::clamp(y0, 0, (s32)height - 1);
		const u32 cx1 = (u32)std::clamp(x0 + 1, 0, (s32)width - 1);
		const u32 cy1 = (u32)std::clamp(y0 + 1, 0, (s32)height - 1);

		const f32 h00 = heights[(size_t)cy0 * width + cx0];
		const f32 h10 = heights[(size_t)cy0 * width + cx1];
		const f32 h01 = heights[(size_t)cy1 * width + cx0];
		const f32 h11 = heights[(size_t)cy1 * width + cx1];

		return lerp(lerp(h00, h10, tx), lerp(h01, h11, tx), ty);
	}

	bool Terrain::initialize(
		const std::shared_ptr<rhi::IRHI>& rhi,
		material::MaterialSystem* materials,
		compute::MipGenerator* mipGenerator,
		compute::TerrainBuilder* builder)
	{
		if (!rhi || materials == nullptr)
			return false;

		rhi_ = rhi;
		materials_ = materials;
		mipGenerator_ = mipGenerator;
		builder_ = builder;

		return true;
	}

	void Terrain::releaseResources()
	{
		// release, not free: these go straight back on the allocator's free list to be
		// handed out again, rather than through the deferred queue. That is what a rebuild
		// wants -- the replacement buffers are the same size and reuse the same memory --
		// but it means the caller must already have waited for the frames that drew the
		// previous terrain, because nothing here checks.
		if (model_.vertexBuffer != INVALID_HANDLE)
			rhi_->releaseBuffer(model_.vertexBuffer);

		if (model_.indexBuffer != INVALID_HANDLE)
			rhi_->releaseBuffer(model_.indexBuffer);

		model_.vertexBuffer = INVALID_HANDLE;
		model_.indexBuffer = INVALID_HANDLE;

		// The baked maps are not touched here. bakeMaterial swaps them itself, and has to
		// do it in the opposite order -- new ones built before the old ones are let go.
	}

	void Terrain::deinitialize()
	{
		if (!rhi_)
			return;

		releaseResources();

		if (baseColorTexture_ != INVALID_HANDLE)
			rhi_->releaseImage(baseColorTexture_);

		if (normalTexture_ != INVALID_HANDLE)
			rhi_->releaseImage(normalTexture_);

		if (roughnessTexture_ != INVALID_HANDLE)
			rhi_->releaseImage(roughnessTexture_);

		baseColorTexture_ = INVALID_HANDLE;
		normalTexture_ = INVALID_HANDLE;
		roughnessTexture_ = INVALID_HANDLE;

		rhi_.reset();
		materials_ = nullptr;
	}

	void Terrain::build(const noise::NoiseDesc& noiseDesc, const TerrainDesc& desc)
	{
		if (!rhi_)
			return;

		const auto start = std::chrono::high_resolution_clock::now();

		releaseResources();

		desc_ = desc;
		desc_.resolution = std::max(2u, desc_.resolution);
		desc_.textureSize = std::max(4u, desc_.textureSize);

		// The heightmap is sampled by the mesh at vertex spacing and by the material bake
		// at texel spacing, and the finer of the two decides how much of the noise survives.
		// Generating at the larger resolution means neither is the one throwing detail away.
		const u32 fieldSize = std::max(desc_.resolution, desc_.textureSize);

		heightmap_.width = fieldSize;
		heightmap_.height = fieldSize;

		builtOnGpu_ = (builder_ != nullptr) && builder_->isReady();

		if (builtOnGpu_)
		{
			buildOnGpu(noiseDesc);
		}
		else
		{
			heightmap_.heights.resize((size_t)fieldSize * fieldSize);

			noise::generate(noiseDesc, fieldSize, fieldSize, heightmap_.heights.data());

			buildMesh();
			bakeMaterial(noiseDesc);
		}

		const auto end = std::chrono::high_resolution_clock::now();

		lastBuildMs_ = std::chrono::duration<f32, std::milli>(end - start).count();
	}

	void Terrain::createMaps(bool storage)
	{
		const rhi::TextureHandle previousBaseColor = baseColorTexture_;
		const rhi::TextureHandle previousNormal = normalTexture_;
		const rhi::TextureHandle previousRoughness = roughnessTexture_;

		const bool firstBake = (previousBaseColor == INVALID_HANDLE);

		rhi::TextureDesc desc{};
		desc.width = desc_.textureSize;
		desc.height = desc_.textureSize;
		desc.depth = 1;

		// 0 asks for a full chain. A terrain map is viewed at every scale from underfoot to
		// the horizon, so this is not optional.
		desc.mipLevels = 0;
		desc.usage = rhi::ETextureUsage::eSampled | rhi::ETextureUsage::eTransferDst;

		if (storage)
			desc.usage = desc.usage | rhi::ETextureUsage::eStorage;

		desc.memoryType = rhi::EMemoryType::eDeviceLocalImage;

		// Created before the old ones are let go: a released handle goes straight back on
		// the free list, so the other order would hand these three the same three handles
		// back in some other order and permute the bindless slots behind our back.
		rhi::TextureDesc colorDesc = desc;
		colorDesc.format = rhi::ETextureFormat::eR8G8B8A8_SRGB;

		rhi::TextureDesc linearDesc = desc;
		linearDesc.format = rhi::ETextureFormat::eR8G8B8A8_UNORM;

		baseColorTexture_ = rhi_->createTexture(colorDesc);
		normalTexture_ = rhi_->createTexture(linearDesc);
		roughnessTexture_ = rhi_->createTexture(linearDesc);

		if (!firstBake)
		{
			rhi_->releaseImage(previousBaseColor);
			rhi_->releaseImage(previousNormal);
			rhi_->releaseImage(previousRoughness);

			// The slots stay put and the material keeps naming them, so nothing downstream
			// has to be told the maps were rebuilt.
			materials_->replaceTexture(baseColorSlot_, baseColorTexture_);
			materials_->replaceTexture(normalSlot_, normalTexture_);
			materials_->replaceTexture(roughnessSlot_, roughnessTexture_);
		}
		else
		{
			baseColorSlot_ = materials_->registerTexture(baseColorTexture_);
			normalSlot_ = materials_->registerTexture(normalTexture_);
			roughnessSlot_ = materials_->registerTexture(roughnessTexture_);
		}
	}

	void Terrain::finishModel()
	{
		if (!model_.primitives.empty())
		{
			// Only the index count can have changed, and only if the resolution did.
			model_.primitives[0].indexCount = model_.indexCount;
			return;
		}

		material::MaterialDesc materialDesc{};
		materialDesc.baseColorTexture = baseColorTexture_;
		materialDesc.normalTexture = normalTexture_;
		materialDesc.metallicRoughnessTexture = roughnessTexture_;

		// Double-sided: a heightfield is a single sheet, and a camera that ends up
		// under it -- diving through a slope in fly mode, or a near plane clipping a
		// ridge -- otherwise sees a hole in the world where the culled underside was.
		// The underside shades dark (its normals face away), which reads as being
		// under the ground, which is exactly what is happening.
		materialDesc.renderState.doubleSided = true;

		materialDesc.sampler.filter = rhi::EFilterMode::eLinear;
		materialDesc.sampler.address = rhi::EAddressMode::eClampToEdge;
		materialDesc.sampler.maxAnisotropy = 8;

		materialDesc.constants.metallicFactor = 0.0f;
		materialDesc.constants.roughnessFactor = 1.0f;
		materialDesc.constants.normalScale = 1.0f;

		const material::MaterialHandle handle = materials_->createMaterial(materialDesc);

		model_.primitives.push_back({ 0, model_.indexCount, handle });

		model_.materialCount = 1;
		model_.textureCount = 3;
	}

	void Terrain::buildOnGpu(const noise::NoiseDesc& noiseDesc)
	{
		const u32 resolution = desc_.resolution;
		const u32 quads = resolution - 1;

		model_.vertexCount = resolution * resolution;
		model_.indexCount = quads * quads * 6;

		// eStorageReadWrite as well as eVertex/eIndex, because the mesh pass writes these
		// through a UAV. eStorage on top of that is what the visibility buffer's resolve
		// reads them through, so all three usages have to be declared at once.
		rhi::BufferDesc vertexDesc{};
		vertexDesc.size = (u64)model_.vertexCount * sizeof(asset::ModelVertex);
		vertexDesc.usage = rhi::EBufferUsage::eVertex | rhi::EBufferUsage::eStorage | rhi::EBufferUsage::eStorageReadWrite;
		vertexDesc.memoryType = rhi::EMemoryType::eDeviceLocalBuffer;

		model_.vertexBuffer = rhi_->createBuffer(vertexDesc);

		rhi::BufferDesc indexDesc{};
		indexDesc.size = (u64)model_.indexCount * sizeof(u32);
		indexDesc.usage = rhi::EBufferUsage::eIndex | rhi::EBufferUsage::eStorage | rhi::EBufferUsage::eStorageReadWrite;
		indexDesc.memoryType = rhi::EMemoryType::eDeviceLocalBuffer;

		model_.indexBuffer = rhi_->createBuffer(indexDesc);

		createMaps(true);

		compute::TerrainBuilder::Output output{};
		output.vertexBuffer = model_.vertexBuffer;
		output.indexBuffer = model_.indexBuffer;
		output.baseColor = baseColorTexture_;
		output.normal = normalTexture_;
		output.roughness = roughnessTexture_;

		builder_->build(noiseDesc, desc_, heightmap_.width, output, heightmap_.heights);

		// Only level 0 was written; the rest of each chain is the mip generator's job.
		if (mipGenerator_ != nullptr && mipGenerator_->isReady())
		{
			const u32 mipLevels = rhi::mipLevelsFor(desc_.textureSize, desc_.textureSize);

			mipGenerator_->generate(baseColorTexture_, desc_.textureSize, desc_.textureSize, mipLevels, true);
			mipGenerator_->generate(normalTexture_, desc_.textureSize, desc_.textureSize, mipLevels, false);
			mipGenerator_->generate(roughnessTexture_, desc_.textureSize, desc_.textureSize, mipLevels, false);
		}

		const f32 half = desc_.worldSize * 0.5f;

		model_.boundsMin = { -half, 0.0f, -half };
		model_.boundsMax = { half, desc_.heightScale, half };

		finishModel();
	}

	void Terrain::buildMesh()
	{
		const u32 resolution = desc_.resolution;
		const u32 quads = resolution - 1;

		const f32 half = desc_.worldSize * 0.5f;
		const f32 step = desc_.worldSize / (f32)quads;

		std::vector<asset::ModelVertex> vertices((size_t)resolution * resolution);

		// Differenced at vertex spacing, so the four taps are the neighbouring vertices'
		// own heights and the result is the average of the slopes of the triangles that
		// meet here -- the definition of a smooth-shaded vertex normal on a height grid.
		//
		// Sampling the field at its own spacing instead, which is four times finer than the
		// vertices at the default settings, makes the normal an alias of detail the
		// triangles cannot represent. That detail is not lost; it belongs in the normal map,
		// and bakeMaterial puts it there.
		const f32 vertexStep = 1.0f / (f32)quads;

		util::parallelFor(resolution, [&](u32 z)
			{
				const f32 v = (f32)z / (f32)quads;

				for (u32 x = 0; x < resolution; x++)
				{
					const f32 u = (f32)x / (f32)quads;

					const f32 height = heightmap_.sample(u, v) * desc_.heightScale;

					// Central differences in world units. The horizontal span of one vertex
					// step is what converts the height difference into a slope.
					const f32 hL = heightmap_.sample(u - vertexStep, v) * desc_.heightScale;
					const f32 hR = heightmap_.sample(u + vertexStep, v) * desc_.heightScale;
					const f32 hD = heightmap_.sample(u, v - vertexStep) * desc_.heightScale;
					const f32 hU = heightmap_.sample(u, v + vertexStep) * desc_.heightScale;

					const f32 spacing = vertexStep * desc_.worldSize;

					// The cross product of the two tangents, written out: the tangent along
					// x is (2*spacing, hR - hL, 0) and along z is (0, hU - hD, 2*spacing).
					math::Vec3 normal{ -(hR - hL), 2.0f * spacing, -(hU - hD) };
					normal = math::normalize(normal);

					asset::ModelVertex& vertex = vertices[(size_t)z * resolution + x];

					vertex.position[0] = -half + (f32)x * step;
					vertex.position[1] = height;
					vertex.position[2] = -half + (f32)z * step;

					vertex.normal[0] = normal.x;
					vertex.normal[1] = normal.y;
					vertex.normal[2] = normal.z;

					vertex.uv[0] = u;
					vertex.uv[1] = v;
				}
			});

		std::vector<u32> indices;
		indices.resize((size_t)quads * quads * 6);

		util::parallelFor(quads, [&](u32 z)
			{
				for (u32 x = 0; x < quads; x++)
				{
					const u32 topLeft = z * resolution + x;
					const u32 topRight = topLeft + 1;
					const u32 bottomLeft = topLeft + resolution;
					const u32 bottomRight = bottomLeft + 1;

					u32* out = &indices[((size_t)z * quads + x) * 6];

					// Clockwise in a left-handed, y-up frame, matching the winding the
					// loaded models use.
					out[0] = topLeft;
					out[1] = bottomLeft;
					out[2] = topRight;

					out[3] = topRight;
					out[4] = bottomLeft;
					out[5] = bottomRight;
				}
			});

		model_.vertexCount = (u32)vertices.size();
		model_.indexCount = (u32)indices.size();

		rhi::BufferDesc vertexDesc{};
		vertexDesc.size = vertices.size() * sizeof(asset::ModelVertex);
		vertexDesc.usage = rhi::EBufferUsage::eVertex | rhi::EBufferUsage::eStorage | rhi::EBufferUsage::eTransferDst;
		vertexDesc.memoryType = rhi::EMemoryType::eDeviceLocalBuffer;

		model_.vertexBuffer = rhi_->createBuffer(vertexDesc);
		rhi_->uploadBuffer(model_.vertexBuffer, vertices.data(), vertexDesc.size);

		rhi::BufferDesc indexDesc{};
		indexDesc.size = indices.size() * sizeof(u32);
		indexDesc.usage = rhi::EBufferUsage::eIndex | rhi::EBufferUsage::eStorage | rhi::EBufferUsage::eTransferDst;
		indexDesc.memoryType = rhi::EMemoryType::eDeviceLocalBuffer;

		model_.indexBuffer = rhi_->createBuffer(indexDesc);
		rhi_->uploadBuffer(model_.indexBuffer, indices.data(), indexDesc.size);

		model_.boundsMin = { -half, 0.0f, -half };
		model_.boundsMax = { half, desc_.heightScale, half };
	}

	void Terrain::bakeMaterial(const noise::NoiseDesc& noiseDesc)
	{
		const u32 size = desc_.textureSize;
		const size_t texelCount = (size_t)size * size;

		// Two extra fields, both far above the terrain's own frequency. One breaks up the
		// flat colour a pure height-and-slope blend would give; the other roughens the
		// baked normal so the surface catches light at a scale the mesh has no vertices for.
		noise::NoiseDesc tintDesc{};
		tintDesc.basis = noise::EBasis::eSimplex;
		tintDesc.fractal = noise::EFractal::eFbm;
		tintDesc.frequency = 24.0f;
		tintDesc.octaves = 4;
		tintDesc.seed = noiseDesc.seed + 0x2545f491u;

		std::vector<f32> tint(texelCount);
		noise::generate(tintDesc, size, size, tint.data());

		noise::NoiseDesc detailDesc{};
		detailDesc.basis = noise::EBasis::eWorley;
		detailDesc.fractal = noise::EFractal::eBillow;
		detailDesc.frequency = 48.0f;
		detailDesc.octaves = 3;
		detailDesc.seed = noiseDesc.seed + 0x68e31da4u;

		std::vector<f32> detail(texelCount);
		noise::generate(detailDesc, size, size, detail.data());

		std::vector<u8> baseColor(texelCount * 4);
		std::vector<u8> normals(texelCount * 4);
		std::vector<u8> roughness(texelCount * 4);

		const f32 texelStep = 1.0f / (f32)size;
		const f32 spacing = texelStep * desc_.worldSize;

		// The scale the mesh already describes, which the normal map has to subtract off.
		const f32 vertexStep = 1.0f / (f32)std::max(1u, desc_.resolution - 1);
		const f32 vertexSpacing = vertexStep * desc_.worldSize;

		// The palette. Flat colours rather than tiled photographs, because everything here
		// has to come out of a function.
		constexpr Rgb kSand{ 0.76f, 0.70f, 0.50f };
		constexpr Rgb kGrass{ 0.24f, 0.38f, 0.16f };
		constexpr Rgb kRock{ 0.38f, 0.35f, 0.32f };
		constexpr Rgb kSnow{ 0.90f, 0.92f, 0.95f };

		const f32 waterFraction = (desc_.heightScale > 0.0f)
			? desc_.waterHeight / desc_.heightScale
			: 0.0f;

		util::parallelFor(size, [&](u32 y)
			{
				const f32 v = ((f32)y + 0.5f) * texelStep;

				for (u32 x = 0; x < size; x++)
				{
					const f32 u = ((f32)x + 0.5f) * texelStep;
					const size_t index = (size_t)y * size + x;

					const f32 height = heightmap_.sample(u, v);

					const f32 hL = heightmap_.sample(u - texelStep, v) * desc_.heightScale;
					const f32 hR = heightmap_.sample(u + texelStep, v) * desc_.heightScale;
					const f32 hD = heightmap_.sample(u, v - texelStep) * desc_.heightScale;
					const f32 hU = heightmap_.sample(u, v + texelStep) * desc_.heightScale;

					// The detail field displaces the surface a few centimetres before the
					// gradient is taken, which is where the fine grain in the normal map
					// comes from.
					const f32 detailScale = 0.35f;
					const f32 dL = detail[(size_t)y * size + ((x > 0) ? x - 1 : x)] * detailScale;
					const f32 dR = detail[(size_t)y * size + ((x + 1 < size) ? x + 1 : x)] * detailScale;
					const f32 dD = detail[(size_t)((y > 0) ? y - 1 : y) * size + x] * detailScale;
					const f32 dU = detail[(size_t)((y + 1 < size) ? y + 1 : y) * size + x] * detailScale;

					// The surface as the heightmap and the detail field describe it, at
					// texel scale.
					const f32 slopeFineX = ((hR + dR) - (hL + dL)) / (2.0f * spacing);
					const f32 slopeFineZ = ((hU + dU) - (hD + dD)) / (2.0f * spacing);

					const math::Vec3 fineNormal = math::normalize({ -slopeFineX, 1.0f, -slopeFineZ });

					// How far from flat, as 0 at horizontal and 1 at vertical. Taken from
					// the fine surface, because whether a spot is rock is a question about
					// the surface, not about how well the mesh happens to resolve it.
					const f32 slope = 1.0f - saturate(fineNormal.y);

					// --- base colour ---------------------------------------------------

					const f32 tintValue = tint[index];

					f32 grassAmount = smoothstep(waterFraction, waterFraction + 0.06f, height);
					Rgb color = mixColor(kSand, kGrass, grassAmount);

					// Height and slope both push towards rock, and the stronger of the two
					// wins. Taking the max rather than adding them stops a moderately steep
					// slope high up from counting twice.
					const f32 rockFromHeight = smoothstep(
						desc_.rockHeight - 0.10f,
						desc_.rockHeight + 0.10f,
						height + (tintValue - 0.5f) * 0.10f);

					const f32 rockFromSlope = smoothstep(
						desc_.rockSlope - 0.12f,
						desc_.rockSlope + 0.12f,
						slope);

					color = mixColor(color, kRock, std::max(rockFromHeight, rockFromSlope));

					// Snow settles on what is flat enough to hold it, so the slope term
					// takes it away again.
					const f32 snowAmount = smoothstep(desc_.snowHeight - 0.08f, desc_.snowHeight + 0.08f, height)
						* (1.0f - smoothstep(0.35f, 0.60f, slope));

					color = mixColor(color, kSnow, snowAmount);

					// A little of the tint field everywhere, so no region is ever a single
					// flat colour.
					const f32 variation = 0.86f + tintValue * 0.28f;

					baseColor[index * 4 + 0] = toByte(color.r * variation);
					baseColor[index * 4 + 1] = toByte(color.g * variation);
					baseColor[index * 4 + 2] = toByte(color.b * variation);
					baseColor[index * 4 + 3] = 255;

					// --- normal --------------------------------------------------------

					// A tangent-space map has to carry the difference between the real
					// surface and the one the mesh already has, not the surface itself.
					// Writing the world normal here and letting the shader rotate it by the
					// tangent frame applied every slope twice.
					const f32 cL = heightmap_.sample(u - vertexStep, v) * desc_.heightScale;
					const f32 cR = heightmap_.sample(u + vertexStep, v) * desc_.heightScale;
					const f32 cD = heightmap_.sample(u, v - vertexStep) * desc_.heightScale;
					const f32 cU = heightmap_.sample(u, v + vertexStep) * desc_.heightScale;

					const f32 slopeCoarseX = (cR - cL) / (2.0f * vertexSpacing);
					const f32 slopeCoarseZ = (cU - cD) / (2.0f * vertexSpacing);

					// x runs along world x and y along world z, because that is how the
					// terrain's uv is laid out; z is the mesh normal. A residual of zero
					// gives (0, 0, 1) and perturbs nothing.
					const math::Vec3 tangentNormal = math::normalize({
						-(slopeFineX - slopeCoarseX),
						-(slopeFineZ - slopeCoarseZ),
						1.0f });

					normals[index * 4 + 0] = toByte(tangentNormal.x * 0.5f + 0.5f);
					normals[index * 4 + 1] = toByte(tangentNormal.y * 0.5f + 0.5f);
					normals[index * 4 + 2] = toByte(tangentNormal.z * 0.5f + 0.5f);
					normals[index * 4 + 3] = 255;

					// --- metallic-roughness --------------------------------------------

					// glTF packs roughness in green and metallic in blue. None of this is
					// metal, so blue stays at zero.
					const f32 rockRoughness = lerp(0.95f, 0.62f, std::max(rockFromHeight, rockFromSlope));
					const f32 surfaceRoughness = lerp(rockRoughness, 0.32f, snowAmount);

					roughness[index * 4 + 0] = 255;
					roughness[index * 4 + 1] = toByte(saturate(surfaceRoughness + (tintValue - 0.5f) * 0.08f));
					roughness[index * 4 + 2] = 0;
					roughness[index * 4 + 3] = 255;
				}
			});

		// The maps are created empty and then uploaded, rather than created from the
		// pixels, so both paths claim their bindless slots the same way.
		//
		// Storage is still needed when the chains are filled by a dispatch, which is an
		// independent choice from where the pixels came from: the two compute helpers can be
		// available separately, and a texture the mip generator writes has to say so at
		// creation whether or not the bake that produced level 0 ran on the GPU.
		const bool computeMips = (mipGenerator_ != nullptr) && mipGenerator_->isReady();

		createMaps(computeMips);

		auto upload = [&](rhi::TextureHandle texture, const std::vector<u8>& pixels, bool srgb)
		{
			if (computeMips)
			{
				const rhi::TextureUpload top{ pixels.data(), (u64)size * size * 4 };
				rhi_->uploadTexture(texture, &top, 1);

				mipGenerator_->generate(texture, size, size, rhi::mipLevelsFor(size, size), srgb);
				return;
			}

			noise::uploadWithCpuMips(rhi_, texture, pixels.data(), size, size, srgb);
		};

		upload(baseColorTexture_, baseColor, true);
		upload(normalTexture_, normals, false);
		upload(roughnessTexture_, roughness, false);

		finishModel();
	}

	f32 Terrain::heightAt(f32 worldX, f32 worldZ) const
	{
		if (heightmap_.width == 0)
			return 0.0f;

		const f32 half = desc_.worldSize * 0.5f;

		const f32 u = (worldX + half) / desc_.worldSize;
		const f32 v = (worldZ + half) / desc_.worldSize;

		return heightmap_.sample(u, v) * desc_.heightScale;
	}
}
