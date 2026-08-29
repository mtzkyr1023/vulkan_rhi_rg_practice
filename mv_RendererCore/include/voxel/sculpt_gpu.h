#ifndef _MV_SCULPT_GPU_H_
#define _MV_SCULPT_GPU_H_

#include <memory>
#include <vector>

#include "material/material_system.h"
#include "rhi/rhi.h"

#include "util/math.h"
#include "util/types.h"

namespace mv
{
	namespace voxel
	{
		using namespace types;

		// The sculptable world's GPU side, chunked: one set of pipelines and tables,
		// and per chunk its own density, vertex buffer and indirect arguments. A
		// brush stroke touches only the chunks it overlaps; only those re-march.
		//
		// Chunks are independent grids whose boundary corner samples are duplicated
		// between neighbours. Seams stay crack-free because every writer -- the
		// initial fill and each brush -- applies by world position, so the shared
		// corners always hold identical values and marching cubes emits identical
		// edge vertices on both sides.
		//
		// Division of labour with SculptVolume stands: the CPU keeps a per-chunk
		// mirror for Bullet, refreshed lazily after strokes; this class owns pixels.
		class SculptGpu
		{
		public:
			// Per chunk. A chunk is mostly a ground sheet, so the worst case of the
			// whole grid never happens; the cap covers a heavily carved one.
			static constexpr u32 kChunkMaxVertices = 45000;

			struct Shaders
			{
				const u32* mc = nullptr; u32 mcSize = 0;
				const u32* reset = nullptr; u32 resetSize = 0;
				const u32* deform = nullptr; u32 deformSize = 0;
				const u32* brush = nullptr; u32 brushSize = 0;
				const u32* vs = nullptr; u32 vsSize = 0;
				const u32* ps = nullptr; u32 psSize = 0;
			};

			// Pipelines, layouts and tables only; chunks are added afterwards.
			bool initialize(
				const std::shared_ptr<rhi::IRHI>& rhi,
				const Shaders& shaders,
				const std::vector<rhi::ETextureFormat>& colorFormats,
				rhi::ETextureFormat depthFormat,
				rhi::BindGroupLayoutHandle sceneLayout,
				rhi::BindGroupLayoutHandle bindlessLayout,
				const u16* edgeTable,
				const s8* triTable,
				u32 maxCorners);

			void deinitialize();

			bool isReady() const { return ready_; }

			void setMaterial(material::MaterialHandle material) { material_ = material; }

			// Creates one chunk's GPU residence and returns its index.
			u32 addChunk();

			u32 chunkCount() const { return (u32)chunks_.size(); }

			// Full density upload for one chunk (placement and resets).
			void updateDensity(
				u32 chunk,
				const f32* density, u32 cornerCount,
				const math::Vec3& origin, f32 cellSize, u32 cells);

			// A stroke against one chunk, applied by a dispatch this frame.
			void queueBrush(u32 chunk, const math::Vec3& worldPosition, f32 radius, f32 strength);

			void queueRemesh(u32 chunk);
			void queueRemeshAll();

			bool anyPending() const;

			// Flushes strokes and re-marches every pending chunk.
			void recordRemesh(rhi::CommandBufferHandle cmd);

			// The continuous path, for every chunk, every frame.
			void recordAnimate(
				rhi::CommandBufferHandle cmd,
				f32 time, f32 amplitude, f32 wavelength, f32 speed);

			// One indirect draw per meshed chunk, inside the scene render pass.
			void recordDraw(
				rhi::CommandBufferHandle cmd,
				rhi::BindGroupHandle sceneGroup,
				rhi::BindGroupHandle bindlessGroup);

		private:
			struct BrushOp
			{
				math::Vec3 gridCenter{};
				f32 gridRadius = 0.0f;
				f32 strength = 0.0f;
			};

			struct Chunk
			{
				rhi::BufferHandle density = INVALID_HANDLE;
				rhi::BufferHandle animated = INVALID_HANDLE;
				rhi::BufferHandle vertices = INVALID_HANDLE;
				rhi::BufferHandle drawArgs = INVALID_HANDLE;

				rhi::BindGroupHandle computeGroup = INVALID_HANDLE;
				rhi::BindGroupHandle computeGroupAnimated = INVALID_HANDLE;
				rhi::BindGroupHandle deformGroup = INVALID_HANDLE;
				rhi::BindGroupHandle brushGroup = INVALID_HANDLE;
				rhi::BindGroupHandle drawGroup = INVALID_HANDLE;

				std::vector<BrushOp> brushOps;

				math::Vec3 origin{};
				f32 cellSize = 1.0f;
				u32 cells = 0;

				bool remeshPending = false;
				bool firstFrame = true;
				bool firstAnimate = true;
				bool meshed = false;
			};

			void recordBrushes(rhi::CommandBufferHandle cmd, Chunk& chunk);
			void recordMarch(rhi::CommandBufferHandle cmd, Chunk& chunk, rhi::BindGroupHandle group);
			void recordDeform(
				rhi::CommandBufferHandle cmd, Chunk& chunk,
				f32 time, f32 amplitude, f32 wavelength, f32 speed);

			std::shared_ptr<rhi::IRHI> rhi_;

			std::vector<Chunk> chunks_;

			rhi::BufferHandle tableBuffer_ = INVALID_HANDLE;

			rhi::BindGroupLayoutHandle computeLayout_ = INVALID_HANDLE;
			rhi::PipelineLayoutHandle computePipelineLayout_ = INVALID_HANDLE;
			rhi::PipelineHandle mcPipeline_ = INVALID_HANDLE;
			rhi::PipelineHandle resetPipeline_ = INVALID_HANDLE;

			rhi::BindGroupLayoutHandle deformLayout_ = INVALID_HANDLE;
			rhi::PipelineLayoutHandle deformPipelineLayout_ = INVALID_HANDLE;
			rhi::PipelineHandle deformPipeline_ = INVALID_HANDLE;

			rhi::BindGroupLayoutHandle brushLayout_ = INVALID_HANDLE;
			rhi::PipelineLayoutHandle brushPipelineLayout_ = INVALID_HANDLE;
			rhi::PipelineHandle brushPipeline_ = INVALID_HANDLE;

			rhi::BindGroupLayoutHandle drawLayout_ = INVALID_HANDLE;
			rhi::PipelineLayoutHandle drawPipelineLayout_ = INVALID_HANDLE;
			rhi::PipelineHandle drawPipeline_ = INVALID_HANDLE;

			material::MaterialHandle material_ = INVALID_HANDLE;

			u32 maxCorners_ = 0;

			bool ready_ = false;
		};
	}
}

#endif
