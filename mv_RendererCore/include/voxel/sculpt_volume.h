#ifndef _MV_SCULPT_VOLUME_H_
#define _MV_SCULPT_VOLUME_H_

#include <functional>
#include <memory>
#include <vector>

#include "asset/gltf_loader.h"
#include "material/material_system.h"
#include "rhi/rhi.h"

#include "util/math.h"
#include "util/types.h"

namespace mv
{
	namespace voxel
	{
		using namespace types;

		// The classic marching cubes tables, exposed so the GPU path can upload the
		// same data the CPU path indexes: 256 edge masks, and 256 rows of up to five
		// triangles' edge indices, -1 terminated.
		const u16* mcEdgeTable();
		const s8* mcTriTable();

		// A block of world the player can carve: a CPU density grid, marching cubes
		// over it whenever it changes, and the result served as an ordinary model the
		// prop pass draws.
		//
		// Everything stays on the CPU on purpose. The grid is small (a few tens of
		// metres), edits arrive at keypress rate, and a full remesh is a couple of
		// milliseconds -- a compute version buys nothing here but complexity. What
		// matters is that the same triangle soup feeds the renderer and the physics
		// mesh, so what you see carved is exactly what you stand on.
		//
		// The vertex buffers are doubled: the GPU may still be drawing last frame's
		// surface while an edit rewrites this one.
		class SculptVolume
		{
		public:
			// Grid resolution per side (cells), and metres per cell. Sized as one
			// chunk of the sculptable world: 24 metres across.
			static constexpr u32 kCells = 32;
			static constexpr f32 kCellSize = 0.75f;

			bool initialize(
				const std::shared_ptr<rhi::IRHI>& rhi,
				material::MaterialSystem& materialSystem);

			// CPU-only residence: the density and the triangle soup for physics, no
			// GPU objects at all -- what a chunk needs when the GPU path draws it.
			bool initializeData();

			void deinitialize();

			bool isReady() const { return ready_; }

			// Places the volume in the world (minimum corner derived from the centre)
			// and fills it with the starting shape: a dome half-sunk into the ground.
			// Calling again moves and resets it.
			void place(const math::Vec3& center, f32 groundY);

			// Places the volume at an explicit minimum corner and fills it from the
			// ground: solid below groundHeight(x, z) + lift. The density is linear
			// in y, so marching cubes reproduces the lifted ground surface exactly,
			// slope normals included -- and everything beneath it is diggable rock.
			void placeFromGround(
				const math::Vec3& origin,
				f32 lift,
				const std::function<f32(f32, f32)>& groundHeight);

			// Places the volume with a saved density -- a streamed-out chunk's edits
			// coming back exactly as they were left.
			void restoreDensity(const math::Vec3& origin, const std::vector<f32>& saved);

			bool isPlaced() const { return placed_; }

			// Carves (negative strength) or builds (positive) a smooth sphere of
			// influence at a world position, then remeshes. Returns true if anything
			// actually changed -- a miss far outside the grid is a no-op.
			bool edit(const math::Vec3& worldPosition, f32 radius, f32 strength);

			// The brush alone, no remesh: the mirror-keeping half for when the GPU
			// owns the visible surface and the CPU mesh only has to be right by the
			// time the physics asks for it.
			bool applyBrush(const math::Vec3& worldPosition, f32 radius, f32 strength);

			// Rebuilds the CPU triangle soup from the current density -- the deferred
			// other half of applyBrush.
			void rebuildMesh() { remesh(); }

			// The model the prop pass draws; world space is baked into the vertices,
			// so the transform to draw it with is identity.
			const asset::Model& model() const { return model_; }

			// The current surface as a triangle soup for the physics mesh: positions
			// are the first three floats of each vertex, three vertices per triangle.
			const f32* trianglePositions() const
			{
				return vertices_.empty() ? nullptr : vertices_[0].position;
			}

			u32 triangleVertexCount() const { return (u32)vertices_.size(); }

			u32 triangleCount() const { return (u32)vertices_.size() / 3; }

			static u32 positionStride() { return (u32)sizeof(asset::ModelVertex); }

			// The raw corner samples and placement, for the GPU mesh path: it uploads
			// this same grid and marches it on the GPU.
			const f32* densityData() const { return density_.data(); }

			u32 cornerCount() const { return (u32)density_.size(); }

			const math::Vec3& origin() const { return origin_; }

		private:
			f32 densityAt(s32 x, s32 y, s32 z) const;
			math::Vec3 gradientAt(const math::Vec3& gridPosition) const;
			f32 sampleGrid(const math::Vec3& gridPosition) const;

			void remesh();

			std::shared_ptr<rhi::IRHI> rhi_;

			// (kCells + 1)^3 corner samples; positive is solid.
			std::vector<f32> density_;

			std::vector<asset::ModelVertex> vertices_;

			asset::Model model_;

			// Two buffers, alternated per remesh.
			rhi::BufferHandle vertexBuffers_[2] = { INVALID_HANDLE, INVALID_HANDLE };
			rhi::BufferHandle indexBuffer_ = INVALID_HANDLE;
			u32 activeBuffer_ = 0;

			math::Vec3 origin_{};

			bool ready_ = false;
			bool placed_ = false;
		};
	}
}

#endif
