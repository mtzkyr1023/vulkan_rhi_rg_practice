#ifndef _MV_HUD_RENDERER_H_
#define _MV_HUD_RENDERER_H_

#include <memory>
#include <vector>

#include "rhi/rhi.h"

#include "util/types.h"

namespace mv
{
	namespace hud
	{
		using namespace types;

		// The game's own screen: rectangles and pixel text, drawn over the finished
		// image, under the debug UI.
		//
		// ImGui stays what it is -- a developer's panel -- and this is the player's
		// layer: crosshair, titles, prompts. Immediate mode on the CPU (begin, some
		// rects and text, end), one alpha-blended draw on the GPU.
		//
		// The font follows the sound_synth philosophy: an embedded 8x8 pixel font
		// baked into a small atlas at initialize, no file, no licence file to carry.
		// A texture slot in the atlas is solid white, which is how the rectangles ride
		// the same pipeline as the glyphs.
		class HudRenderer
		{
		public:
			struct Shaders
			{
				const u32* vs = nullptr; u32 vsSize = 0;
				const u32* ps = nullptr; u32 psSize = 0;
			};

			// 8 px per glyph cell at scale 1.
			static constexpr f32 kGlyphSize = 8.0f;

			static constexpr u32 kMaxVertices = 24 * 1024;

			bool initialize(
				const std::shared_ptr<rhi::IRHI>& rhi,
				const Shaders& shaders,
				rhi::ETextureFormat colorFormat,
				u32 framesInFlight);

			void deinitialize();

			bool isReady() const { return ready_; }

			// One frame's HUD: begin clears the queue, rect/text append, end uploads
			// into this frame's slot, record draws it inside a render pass on the
			// target begin was told the size of.
			void begin(u32 width, u32 height);

			// Coordinates in pixels from the top left; colour is 0xAABBGGRR (the same
			// byte order ImGui uses: R first in memory).
			void rect(f32 x, f32 y, f32 w, f32 h, u32 color);

			void text(f32 x, f32 y, f32 scale, u32 color, const char* string);

			// Width in pixels the string will cover at this scale, for centring.
			static f32 textWidth(f32 scale, const char* string);

			void end(u32 frameIndex);

			void record(rhi::CommandBufferHandle cmd, u32 frameIndex);

		private:
			struct Vertex
			{
				f32 position[2];
				f32 uv[2];
				u32 color;
			};

			void quad(f32 x, f32 y, f32 w, f32 h, f32 u0, f32 v0, f32 u1, f32 v1, u32 color);

			std::shared_ptr<rhi::IRHI> rhi_;

			rhi::PipelineLayoutHandle pipelineLayout_ = INVALID_HANDLE;
			rhi::PipelineHandle pipeline_ = INVALID_HANDLE;
			rhi::BindGroupLayoutHandle bindGroupLayout_ = INVALID_HANDLE;
			rhi::BindGroupHandle bindGroup_ = INVALID_HANDLE;
			rhi::TextureHandle fontTexture_ = INVALID_HANDLE;

			std::vector<rhi::BufferHandle> buffers_;
			std::vector<u32> counts_;

			std::vector<Vertex> queue_;

			f32 width_ = 1.0f;
			f32 height_ = 1.0f;

			bool ready_ = false;
		};
	}
}

#endif
