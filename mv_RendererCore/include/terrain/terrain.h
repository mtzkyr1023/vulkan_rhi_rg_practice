#ifndef _MV_TERRAIN_H_
#define _MV_TERRAIN_H_

#include <memory>
#include <vector>

#include "asset/gltf_loader.h"
#include "material/material_system.h"

#include "rhi/rhi.h"

#include "compute/mip_generator.h"
#include "compute/terrain_builder.h"
#include "util/noise.h"
#include "util/types.h"

namespace mv
{
	namespace terrain
	{
		using namespace types;

		// A grid of heights in [0, 1], row-major.
		//
		// Deliberately not a texture: the mesh builder, the normal bake and the camera's
		// ground query all read it on the CPU, and only the derived maps ever reach the GPU.
		struct Heightmap
		{
			u32 width = 0;
			u32 height = 0;

			std::vector<f32> heights;

			f32 at(u32 x, u32 y) const
			{
				const u32 cx = (x < width) ? x : width - 1;
				const u32 cy = (y < height) ? y : height - 1;

				return heights[(size_t)cy * width + cx];
			}

			// Bilinear, with uv clamped to the edge. Used both to place vertices between
			// samples and to answer where the ground is under an arbitrary world position.
			f32 sample(f32 u, f32 v) const;
		};

		struct TerrainDesc
		{
			// Vertices per side. The mesh is (resolution - 1) squares across, so a power of
			// two plus one divides evenly.
			u32 resolution = 513;

			// Metres across, centred on the origin.
			f32 worldSize = 2000.0f;

			// Metres between the lowest and highest point.
			f32 heightScale = 280.0f;

			// Side of the baked base colour, normal and roughness maps.
			//
			// Independent of the mesh resolution on purpose: the shading detail that makes
			// a slope read as rock costs a texel, not a triangle, and this is where the
			// heightmap's fine octaves end up once the mesh has stopped resolving them.
			u32 textureSize = 1024;

			// Height at which grass gives way to rock, and rock to snow, as a fraction of
			// the full range.
			f32 rockHeight = 0.45f;
			f32 snowHeight = 0.78f;

			// Above this slope the surface is rock whatever its height, which is what stops
			// grass from clinging to cliff faces.
			f32 rockSlope = 0.55f;

			// Metres below which the surface is treated as shoreline sand.
			// Kept in step with the water surface's level by the engine, so the sand band and
			// the waterline are the same line rather than two that nearly agree.
			f32 waterHeight = 70.0f;
		};

		// A heightmap turned into a drawable model.
		//
		// The output is an asset::Model, which is what lets terrain go through the material
		// system, the shadow cascades and the visibility buffer without any of them
		// learning what terrain is. The visibility buffer in particular resolves triangles
		// out of one global vertex array, so the geometry has to be shaped exactly like a
		// loaded model's -- which it is, because it is one.
		class Terrain
		{
		public:
			// Both compute helpers are optional, and independently so. Without a builder the
			// heightmap, mesh and maps are produced on the CPU exactly as before; without a
			// mip generator the chains are resized on the CPU. The CPU implementations stay
			// either way, because the ground-height query reads the heightmap directly and
			// because a sample that only works on one path is not much of a sample.
			bool initialize(
				const std::shared_ptr<rhi::IRHI>& rhi,
				material::MaterialSystem* materials,
				compute::MipGenerator* mipGenerator = nullptr,
				compute::TerrainBuilder* builder = nullptr);

			void deinitialize();

			// Whether the last build ran as dispatches rather than on the CPU.
			bool builtOnGpu() const { return builtOnGpu_; }

			// Generates a heightmap from the noise description and builds everything from
			// it. Safe to call again: the previous buffers and textures are released, so
			// the caller only has to re-point whatever bind groups named them.
			void build(const noise::NoiseDesc& noiseDesc, const TerrainDesc& desc);

			const asset::Model& model() const { return model_; }
			const Heightmap& heightmap() const { return heightmap_; }

			// World-space ground height under a point, bilinearly interpolated. Outside the
			// terrain the edge value is returned rather than nothing, so a camera that
			// wanders off the map still has a floor.
			f32 heightAt(f32 worldX, f32 worldZ) const;

			const TerrainDesc& desc() const { return desc_; }

			u32 triangleCount() const { return model_.indexCount / 3; }
			f32 lastBuildMilliseconds() const { return lastBuildMs_; }

		private:
			void releaseResources();

			void buildMesh();

			// Bakes base colour, normal and metallic-roughness from the heightmap and a few
			// extra noise fields, then registers the material that uses them.
			void bakeMaterial(const noise::NoiseDesc& noiseDesc);

			// The whole build as four dispatches. Leaves heightmap_ filled from the readback,
			// because the ground query still runs on the CPU.
			void buildOnGpu(const noise::NoiseDesc& noiseDesc);

			// Creates the three maps empty, sized for the current desc, and claims or
			// re-points their bindless slots. Shared by both paths.
			void createMaps(bool storage);

			// Registers the material on the first build, updates the index count after.
			void finishModel();

		private:
			std::shared_ptr<rhi::IRHI> rhi_;
			material::MaterialSystem* materials_ = nullptr;
			compute::MipGenerator* mipGenerator_ = nullptr;
			compute::TerrainBuilder* builder_ = nullptr;

			Heightmap heightmap_;
			TerrainDesc desc_;

			asset::Model model_;

			rhi::TextureHandle baseColorTexture_ = INVALID_HANDLE;
			rhi::TextureHandle normalTexture_ = INVALID_HANDLE;
			rhi::TextureHandle roughnessTexture_ = INVALID_HANDLE;

			// The bindless slots those three occupy. Claimed on the first bake and held for
			// the lifetime of the terrain, so the material never has to be rebuilt.
			u32 baseColorSlot_ = 0;
			u32 normalSlot_ = 0;
			u32 roughnessSlot_ = 0;

			bool builtOnGpu_ = false;

			f32 lastBuildMs_ = 0.0f;
		};
	}
}

#endif
