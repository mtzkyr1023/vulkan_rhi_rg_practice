#ifndef _MV_GRASS_FIELD_H_
#define _MV_GRASS_FIELD_H_

#include <memory>
#include <vector>

#include "rhi/rhi.h"

#include "util/math.h"
#include "util/types.h"

namespace mv
{
	namespace grass
	{
		using namespace types;

		// The hard ceiling on the camera-following grid, which sizes the instance buffer
		// once and for all: capacity for every cell means the append can never overflow.
		constexpr u32 kMaxBladesPerSide = 600;

		struct GrassParams
		{
			// Metres from the camera the field reaches. Blades thin out over the last
			// third of it rather than stopping at a line.
			f32 radius = 150.0f;

			// Cells along one side of the camera-following grid: the candidate count is
			// this squared. The cull decides which of them exist and are on screen.
			u32 bladesPerSide = 400;

			// Metres. Height varies about half around this per blade.
			f32 bladeHeight = 0.7f;
			f32 bladeWidth = 0.06f;

			// How far the gusts push the lean.
			f32 windStrength = 0.35f;
			f32 windSpeed = 1.0f;

			// The odds a cell that could grow a blade does. The knob that reads as "how
			// thick the meadow is".
			f32 density = 0.9f;

			math::Vec3 rootColor{ 0.05f, 0.14f, 0.03f };
			math::Vec3 tipColor{ 0.24f, 0.42f, 0.10f };

			bool enabled = true;
		};

		// A field of procedural blades, GPU-driven end to end.
		//
		// A compute pass runs the growth rules and the frustum test once per grid cell and
		// appends one compact record per surviving blade, bumping the indirect draw's
		// instanceCount with the same atomic. The draw then reads its arguments from that
		// buffer, so the CPU issues one dispatch and one drawIndirect and never learns how
		// many blades there are -- nothing to read back, nothing to stall on.
		class GrassField
		{
		public:
			struct Shaders
			{
				const u32* vs = nullptr; u32 vsSize = 0;
				const u32* ps = nullptr; u32 psSize = 0;

				// The culling dispatch and the tiny reset that precedes it. Both required:
				// the draw's arguments come from them.
				const u32* cull = nullptr; u32 cullSize = 0;
				const u32* reset = nullptr; u32 resetSize = 0;
			};

			// The scene and bindless layouts come along for the shadows, the SH ambient
			// and the sampler presets, exactly as the fog pass borrows them.
			bool initialize(
				const std::shared_ptr<rhi::IRHI>& rhi,
				const Shaders& shaders,
				const std::vector<rhi::ETextureFormat>& colorFormats,
				rhi::ETextureFormat depthFormat,
				rhi::BindGroupLayoutHandle sceneLayout,
				rhi::BindGroupLayoutHandle bindlessLayout);

			void deinitialize();

			bool isReady() const { return ready_; }

			// Points the cull at the heightmap copy and remembers its shape. Called under
			// the engine's rebuild wait, so re-pointing the descriptor is safe.
			void setHeightField(rhi::BufferHandle buffer, u32 resolution, f32 worldSize, f32 heightScale);

			// Advances the wind. Separate from record so a paused frame does not sway.
			void advance(f32 deltaSeconds, const GrassParams& params)
			{
				time_ += deltaSeconds * params.windSpeed;
			}

			// The cull dispatch. Outside a render pass, before recordDraw: it resets the
			// draw arguments, appends the visible blades and hands the buffers over to the
			// draw with barriers of its own. The four planes are the frustum's sides,
			// (nx, ny, nz, d) with the inside positive.
			void recordCull(
				rhi::CommandBufferHandle cmd,
				const GrassParams& params,
				const math::Vec3& cameraPosition,
				f32 rockHeight,
				f32 rockSlope,
				f32 waterLevel,
				const f32 planes[4][4]);

			// The indirect draw, into whatever targets are bound: colour, velocity and the
			// scene depth, the same three the geometry passes write.
			void recordDraw(
				rhi::CommandBufferHandle cmd,
				const GrassParams& params,
				rhi::BindGroupHandle sceneGroup,
				rhi::BindGroupHandle bindlessGroup);

		private:
			std::shared_ptr<rhi::IRHI> rhi_;

			// --- draw ----------------------------------------------------------------

			rhi::BindGroupLayoutHandle layout_ = INVALID_HANDLE;
			rhi::PipelineLayoutHandle pipelineLayout_ = INVALID_HANDLE;
			rhi::PipelineHandle pipeline_ = INVALID_HANDLE;
			rhi::BindGroupHandle group_ = INVALID_HANDLE;

			// --- cull ----------------------------------------------------------------

			rhi::BindGroupLayoutHandle cullLayout_ = INVALID_HANDLE;
			rhi::PipelineLayoutHandle cullPipelineLayout_ = INVALID_HANDLE;
			rhi::PipelineHandle cullPipeline_ = INVALID_HANDLE;
			rhi::PipelineHandle resetPipeline_ = INVALID_HANDLE;
			rhi::BindGroupHandle cullGroup_ = INVALID_HANDLE;

			// Three float4s per surviving blade, capacity for every cell of the largest
			// grid so the append can never overflow.
			rhi::BufferHandle instances_ = INVALID_HANDLE;

			// The indirect draw arguments: reset by a one-thread dispatch, raised by the
			// cull's atomic, read by the draw.
			rhi::BufferHandle drawArgs_ = INVALID_HANDLE;

			rhi::BufferHandle heightField_ = INVALID_HANDLE;
			u32 fieldResolution_ = 0;
			f32 worldSize_ = 0.0f;
			f32 heightScale_ = 0.0f;

			// The buffers cycle through states each frame; the first frame has no previous
			// state to name, which eUndefined covers.
			bool firstFrame_ = true;

			f32 time_ = 0.0f;

			bool ready_ = false;
		};
	}
}

#endif
