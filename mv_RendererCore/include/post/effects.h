
#ifndef _MV_POST_EFFECTS_H_
#define _MV_POST_EFFECTS_H_

#include "post/post_process.h"

#include "util/math.h"

namespace mv
{
	namespace post
	{
		// Temporal anti-aliasing.
		//
		// The camera is jittered by a sub-pixel offset every frame, so successive frames
		// sample the same surfaces at slightly different points. Each frame is blended into
		// an accumulated history, which converges on the supersampled result while the view
		// is still and follows it while it moves.
		//
		// Following it is the hard part. Every pass that produces pixels writes out where
		// that pixel was on screen in the previous frame, and this reads that velocity to
		// find the matching history sample. Deriving it from depth instead works only while
		// the camera is the only thing that moves: a world position that a moving mesh
		// occupies this frame belonged to something else in the last one.
		//
		// Where reprojection lands on a surface that was hidden last frame, the history is
		// wrong, and blending it in is what produces ghosting. The neighbourhood clamp is
		// the defence: the history is pulled into the range of colours actually present
		// around the current pixel, which is where a disoccluded sample gets rejected.
		class TemporalAntiAliasing : public IPostEffect
		{
		public:
			const char* name() const override { return "TAA"; }

			bool initialize(const std::shared_ptr<rhi::IRHI>& rhi, PostProcessStack& stack) override;
			void deinitialize() override;

			void record(const EffectContext& context) override;
			void ui() override;
			void reset() override { historyValid_ = false; }
			void onResize(PostProcessStack& stack) override;

			// The sub-pixel offset for this frame, in pixels. The engine folds it into the
			// projection, so the stack has to be asked for it before the frame is drawn.
			math::Vec3 jitter(u32 frameCounter, u32 width, u32 height) const;

			// The velocity buffer the geometry passes wrote. Must be set before the effect is
			// added to the stack, because the bind groups are built once and never rewritten.
			void setVelocityTexture(rhi::TextureHandle velocity) { velocity_ = velocity; }

			bool isHistoryValid() const { return historyValid_; }

		private:
			struct Constants
			{
				f32 blendFactor = 0.05f;

				// How far the clamp lets the history stray from the neighbourhood, in
				// standard deviations. Tighter rejects more ghosting and keeps less of the
				// accumulated detail.
				f32 clampScale = 1.25f;

				f32 historyValid = 0.0f;
				f32 _pad = 0.0f;
			} constants_;

			std::shared_ptr<rhi::IRHI> rhi_;
			PostProcessStack* stack_ = nullptr;

			rhi::PipelineHandle pipeline_ = INVALID_HANDLE;

			// Two, because the frame being written cannot be the frame being read.
			rhi::TextureHandle history_[2]{ INVALID_HANDLE, INVALID_HANDLE };

			// Flattened as slot * 2 + parity, because a nested HandleArray cannot fill
			// itself with the invalid handle.
			HandleArray<rhi::BindGroupHandle, eInputSlotCount * 2> resolveGroups_;

			// The resolved frame has to reach both the rest of the chain and the next
			// frame's history, and it is written to the history so the chain reads that.
			HandleArray<rhi::BindGroupHandle, 2> copyGroups_;
			rhi::PipelineHandle copyChainPipeline_ = INVALID_HANDLE;
			rhi::PipelineHandle copyOutputPipeline_ = INVALID_HANDLE;

			void createBindGroups(PostProcessStack& stack);

			rhi::TextureHandle velocity_ = INVALID_HANDLE;

			u32 parity_ = 0;
			bool historyValid_ = false;
		};

		// Bloom.
		//
		// Bright parts of the image are extracted, blurred at several resolutions, and
		// added back. Doing the blur by successive halving is what makes a wide, soft
		// falloff affordable: a kernel that reached as far at full resolution would cost
		// hundreds of taps.
		//
		// This is the effect that decides whether a post-process framework is really a
		// chain of full-screen passes or just one pass with a longer shader. It needs its
		// own targets at its own sizes, and the interface has to let it have them without
		// the stack knowing.
		class Bloom : public IPostEffect
		{
		public:
			static constexpr u32 kMipCount = 5;

			const char* name() const override { return "Bloom"; }

			bool initialize(const std::shared_ptr<rhi::IRHI>& rhi, PostProcessStack& stack) override;
			void deinitialize() override;

			void record(const EffectContext& context) override;
			void ui() override;
			void onResize(PostProcessStack& stack) override;

		private:
			struct Constants
			{
				f32 threshold = 1.1f;
				f32 knee = 0.6f;
				f32 intensity = 0.06f;
				f32 texelSizeX = 0.0f;

				f32 texelSizeY = 0.0f;
				f32 _pad[3]{};
			} constants_;

			// Builds the pyramid and its bind groups. Shared by initialize and onResize,
			// which differ only in whether the old textures had to be released first.
			void createResources(PostProcessStack& stack);

			void drawInto(const EffectContext& context, rhi::TextureHandle target, u32 width, u32 height,
				rhi::PipelineHandle pipeline, rhi::BindGroupHandle resources);

			// The dispatch equivalent of drawInto. Inserts the barrier that orders this
			// level's writes against the next level reading them.
			void dispatchInto(const EffectContext& context, rhi::TextureHandle target, u32 width, u32 height,
				rhi::PipelineHandle pipeline, rhi::BindGroupHandle resources);

			// Compiles the three pyramid passes as compute. Returns false if any shader is
			// missing, in which case the pixel-shader path stays.
			bool createComputePipelines(PostProcessStack& stack);

			// One group per pass: source, the level being added into, a sampler and the
			// writable target.
			rhi::BindGroupHandle createComputeGroup(
				rhi::TextureHandle source, rhi::TextureHandle accum, rhi::TextureHandle target);

		private:
			std::shared_ptr<rhi::IRHI> rhi_;
			PostProcessStack* stack_ = nullptr;

			rhi::PipelineHandle extractPipeline_ = INVALID_HANDLE;
			rhi::PipelineHandle downsamplePipeline_ = INVALID_HANDLE;
			rhi::PipelineHandle upsamplePipeline_ = INVALID_HANDLE;
			rhi::PipelineHandle compositeChainPipeline_ = INVALID_HANDLE;
			rhi::PipelineHandle compositeOutputPipeline_ = INVALID_HANDLE;

			// The chain down, and a second set for the chain back up. Two sets rather than
			// one because the upsample adds the blurred smaller level to the level above,
			// which means reading and writing the same texture in one draw if they share.
			HandleArray<rhi::TextureHandle, kMipCount> downMips_;
			HandleArray<rhi::TextureHandle, kMipCount> upMips_;

			u32 mipWidth_[kMipCount]{};
			u32 mipHeight_[kMipCount]{};

			HandleArray<rhi::BindGroupHandle, eInputSlotCount> extractGroups_;
			HandleArray<rhi::BindGroupHandle, kMipCount> downsampleGroups_;

			// Reads the smaller level being magnified and the level it is added to.
			HandleArray<rhi::BindGroupHandle, kMipCount> upsampleGroups_;

			// Chain input plus the finished bloom.
			HandleArray<rhi::BindGroupHandle, eInputSlotCount> compositeGroups_;

			// --- compute path ------------------------------------------------------

			// The pyramid as dispatches. Its own layout rather than the stack's, because
			// D3D12 rejects a compute root signature whose parameters name a graphics
			// stage, and the stack's names the fragment shader throughout.
			rhi::BindGroupLayoutHandle computeLayout_ = INVALID_HANDLE;
			rhi::PipelineLayoutHandle computePipelineLayout_ = INVALID_HANDLE;

			rhi::PipelineHandle extractCsPipeline_ = INVALID_HANDLE;
			rhi::PipelineHandle downsampleCsPipeline_ = INVALID_HANDLE;
			rhi::PipelineHandle upsampleCsPipeline_ = INVALID_HANDLE;

			HandleArray<rhi::BindGroupHandle, eInputSlotCount> extractCsGroups_;
			HandleArray<rhi::BindGroupHandle, kMipCount> downsampleCsGroups_;
			HandleArray<rhi::BindGroupHandle, kMipCount> upsampleCsGroups_;

			// False when the compute shaders are missing, which leaves the pixel-shader
			// pyramid exactly as it was.
			bool useCompute_ = false;
		};

		// Tone mapping and the sRGB encode: the point where the chain stops being
		// half-float radiance and becomes something a display can show.
		//
		// This used to be the last two lines of the material shader. Moving it here is what
		// lets everything before it work in the range the lighting actually produced, and
		// is the reason bloom can find values above one to bloom at all.
		class Tonemap : public FullscreenEffect
		{
		public:
			const char* name() const override { return "Tonemap + sRGB"; }

			void ui() override;

		protected:
			const char* shaderName() const override { return "tonemap.ps"; }

			const void* constants(u32& size) const override
			{
				size = sizeof(constants_);
				return &constants_;
			}

		private:
			struct Constants
			{
				// Stops in the photographic sense: each step doubles the exposure.
				f32 exposure = 0.0f;
				f32 _pad[3]{};
			} constants_;
		};

		// Fast approximate anti-aliasing, on the tone-mapped image.
		//
		// It belongs after tone mapping because it decides what is an edge from perceived
		// luminance, which is only meaningful once the range has been compressed. That
		// ordering is the reason the chain is a list rather than a set.
		class Fxaa : public FullscreenEffect
		{
		public:
			const char* name() const override { return "FXAA"; }

			void ui() override;

		protected:
			const char* shaderName() const override { return "fxaa.ps"; }

			const void* constants(u32& size) const override
			{
				size = sizeof(constants_);
				return &constants_;
			}

			bool onInitialize(PostProcessStack& stack) override;

			// The kernel is measured in texels, so it has to follow the window.
			void onResize(PostProcessStack& stack) override
			{
				FullscreenEffect::onResize(stack);
				onInitialize(stack);
			}

		private:
			struct Constants
			{
				f32 texelSizeX = 0.0f;
				f32 texelSizeY = 0.0f;

				// Below this contrast the pixel is left alone.
				f32 contrastThreshold = 0.0312f;
				f32 relativeThreshold = 0.125f;
			} constants_;
		};

		// Vignette and chromatic aberration: the shortest possible example of adding an
		// effect. One class, one shader, a few parameters.
		class LensDistortion : public FullscreenEffect
		{
		public:
			const char* name() const override { return "Vignette + chromatic aberration"; }

			void ui() override;

		protected:
			const char* shaderName() const override { return "lens.ps"; }

			const void* constants(u32& size) const override
			{
				size = sizeof(constants_);
				return &constants_;
			}

		private:
			struct Constants
			{
				f32 vignetteIntensity = 0.35f;
				f32 vignetteSmoothness = 0.45f;

				// How far apart the three channels are pulled at the edge of the frame,
				// as a fraction of the image.
				f32 aberration = 0.0025f;
				f32 _pad = 0.0f;
			} constants_;
		};
	}
}

#endif
