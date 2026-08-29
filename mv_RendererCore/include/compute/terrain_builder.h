#ifndef _MV_TERRAIN_BUILDER_H_
#define _MV_TERRAIN_BUILDER_H_

#include <memory>
#include <vector>

#include "compute/buffer_fill.h"

#include "rhi/rhi.h"

#include "util/noise.h"
#include "util/types.h"

namespace mv
{
	namespace terrain
	{
		struct TerrainDesc;
	}

	namespace compute
	{
		using namespace types;

		// The GPU half of terrain generation: heightmap, mesh and material maps.
		//
		// Four dispatches, in order, because each depends on the one before:
		//
		//   1. evaluate the noise field, reducing its range with two atomics as it goes
		//   2. rescale the field to [0, 1] now that the extremes are known
		//   3. write vertices and indices from it
		//   4. bake base colour, normal and roughness from it
		//
		// The heights are copied back afterwards. Not because anything on the GPU needs
		// them there, but because the camera has to know where the ground is, and a query
		// that stalls on a readback is not a query anyone can call per frame.
		class TerrainBuilder
		{
		public:
			struct Shaders
			{
				const u32* height = nullptr;      u32 heightSize = 0;
				const u32* normalise = nullptr;   u32 normaliseSize = 0;
				const u32* mesh = nullptr;        u32 meshSize = 0;
				const u32* bake = nullptr;        u32 bakeSize = 0;

				// The generic buffer fill, used to reset the two-slot range buffer before the
				// reduction. Without it the reduction accumulates into whatever the allocation
				// happened to hold and the normalise pass flattens the whole field.
				const u32* fill = nullptr;        u32 fillSize = 0;
			};

			// Targets the four passes write into. Owned by the caller, which rebuilds them
			// whenever the resolution changes.
			struct Output
			{
				rhi::BufferHandle vertexBuffer = INVALID_HANDLE;
				rhi::BufferHandle indexBuffer = INVALID_HANDLE;

				rhi::TextureHandle baseColor = INVALID_HANDLE;
				rhi::TextureHandle normal = INVALID_HANDLE;
				rhi::TextureHandle roughness = INVALID_HANDLE;
			};

			bool initialize(const std::shared_ptr<rhi::IRHI>& rhi, const Shaders& shaders);
			void deinitialize();

			bool isReady() const { return ready_; }

			// Runs all four passes on one immediate submit and copies the heights back into
			// outHeights, which is resized to fieldSize * fieldSize.
			void build(
				const noise::NoiseDesc& noiseDesc,
				const terrain::TerrainDesc& desc,
				u32 fieldSize,
				const Output& output,
				std::vector<f32>& outHeights);

		private:
			// Grows the height, range and readback buffers to fit a field of this size.
			void ensureFieldBuffers(u32 fieldSize);

			rhi::PipelineHandle createPipeline(rhi::BindGroupLayoutHandle layout, rhi::PipelineLayoutHandle& outPipelineLayout, const u32* bytecode, u32 size);

		private:
			std::shared_ptr<rhi::IRHI> rhi_;

			// One layout per pass: they read and write different things, and a union of all
			// of them would mean binding descriptors a pass never touches.
			rhi::BindGroupLayoutHandle heightLayout_ = INVALID_HANDLE;
			rhi::BindGroupLayoutHandle normaliseLayout_ = INVALID_HANDLE;
			rhi::BindGroupLayoutHandle meshLayout_ = INVALID_HANDLE;
			rhi::BindGroupLayoutHandle bakeLayout_ = INVALID_HANDLE;

			rhi::PipelineLayoutHandle heightPipelineLayout_ = INVALID_HANDLE;
			rhi::PipelineLayoutHandle normalisePipelineLayout_ = INVALID_HANDLE;
			rhi::PipelineLayoutHandle meshPipelineLayout_ = INVALID_HANDLE;
			rhi::PipelineLayoutHandle bakePipelineLayout_ = INVALID_HANDLE;

			rhi::PipelineHandle heightPipeline_ = INVALID_HANDLE;
			rhi::PipelineHandle normalisePipeline_ = INVALID_HANDLE;
			rhi::PipelineHandle meshPipeline_ = INVALID_HANDLE;
			rhi::PipelineHandle bakePipeline_ = INVALID_HANDLE;

			// Zeroes the two-slot range buffer before the reduction, which is the only
			// reason this class owns a buffer fill.
			BufferFill rangeClear_;

			rhi::BufferHandle heightBuffer_ = INVALID_HANDLE;
			rhi::BufferHandle rangeBuffer_ = INVALID_HANDLE;
			rhi::BufferHandle heightReadback_ = INVALID_HANDLE;

			u32 fieldCapacity_ = 0;

			bool ready_ = false;
		};
	}
}

#endif
