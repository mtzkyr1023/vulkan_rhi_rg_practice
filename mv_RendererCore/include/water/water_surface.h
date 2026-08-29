#ifndef _MV_WATER_SURFACE_H_
#define _MV_WATER_SURFACE_H_

#include <memory>

#include "rhi/rhi.h"

#include "util/math.h"
#include "util/types.h"

namespace mv
{
	namespace water
	{
		using namespace types;

		struct WaterParams
		{
			// World height of the surface. Everything below it is under water, which for a
			// terrain generated in [0, heightScale] means this is what decides how much of
			// it is coast.
			f32 level = 70.0f;

			// Metres across one repeat of the coarsest wave octave, and metres of amplitude.
			// Only the normal is ever displaced -- the surface itself stays a plane, which at
			// these scales is a difference no one can see and a pass no one has to write.
			//
			// So what these two really set is an angle, and an angle is what has to be
			// plausible: about a tenth of the wavelength in amplitude, which is roughly where
			// real water sits. Much under that and the surface is a mirror, with a sun
			// highlight so narrow no pixel centre ever lands in it.
			f32 waveScale = 12.0f;
			f32 waveHeight = 0.9f;

			f32 waveSpeed = 0.6f;

			// What a metre of water takes out of each channel. Red first, by an order of
			// magnitude, which is the whole of why water is blue.
			math::Vec3 extinction{ 0.16f, 0.045f, 0.028f };

			// What comes back out of the body of it.
			math::Vec3 scatterColor{ 0.02f, 0.13f, 0.16f };

			// Low, because water is smooth. It is not zero because a perfect mirror picks
			// mip zero of the environment and aliases into a field of fireflies at the far
			// end of the surface.
			f32 roughness = 0.10f;

			// Metres of path over which the shallow edge fades in. This is the waterline:
			// too small and it is a hard cut across the sand, too large and there is no
			// shore at all.
			f32 shoreFade = 3.0f;

			f32 reflectionStrength = 1.0f;
			f32 specularStrength = 12.0f;

			// How much of the screen-space reflection to lay over the cube one. The two are
			// crossfaded by the march's own confidence, so this is a master fader rather
			// than a mix: zero skips the march outright.
			f32 ssrStrength = 1.0f;

			bool enabled = true;
		};

		// The water surface: one fullscreen pass that intersects a plane.
		//
		// No geometry and no depth attachment. The plane is solved per pixel from the view
		// ray, and the scene depth arrives as a texture -- which it has to anyway, because
		// how far the light travelled through the water is what decides its colour, and a
		// depth test would only have said whether there is any.
		class WaterSurface
		{
		public:
			struct Shaders
			{
				const u32* vs = nullptr; u32 vsSize = 0;
				const u32* ps = nullptr; u32 psSize = 0;

				// The SSR march. Required: the pixel shader statically binds the texture
				// the march writes, so there is no water without it.
				const u32* ssr = nullptr; u32 ssrSize = 0;
			};

			bool initialize(
				const std::shared_ptr<rhi::IRHI>& rhi,
				const Shaders& shaders,
				rhi::ETextureFormat sceneColorFormat);

			void deinitialize();

			bool isReady() const { return ready_; }

			// Advances the waves. Separate from record so a paused frame does not move
			// them, exactly as the cloud layer's wind is.
			void advance(f32 deltaSeconds, const WaterParams& params)
			{
				time_ += deltaSeconds * params.waveSpeed;
			}

			struct View
			{
				math::Vec3 position;
				math::Vec3 forward;

				f32 fovY = 0.0f;
				f32 nearZ = 0.0f;
				f32 farZ = 0.0f;

				u32 width = 0;
				u32 height = 0;
			};

			// Sizes the reflection target to the window. Full resolution, unlike the cloud
			// march: a reflected ridge line is not a low-frequency thing. Called under the
			// engine's resize wait, so re-pointing the descriptors here is safe.
			void resize(u32 width, u32 height);

			// The SSR march, as a dispatch. Has to run before the render pass the surface
			// draws inside, and after everything the reflection should contain is in the
			// scene colour -- which is also why it cannot be part of the water pixel
			// shader: during that draw the scene colour is the render target.
			void recordSSR(
				rhi::CommandBufferHandle cmd,
				const WaterParams& params,
				const View& view,
				rhi::TextureHandle sceneDepth,
				rhi::TextureHandle sceneColor);

			// Draws into whatever colour target is bound. The scene depth and the
			// environment cube are named in a descriptor, so they are re-pointed only when
			// they actually change -- rewriting a set a frame in flight is still reading is
			// a race on both APIs and a validation error on one.
			// sunIntensity scales the glint; iblIntensity scales the cube reflection and the
			// scattered body colour, so the water dims with the same knob as everything else
			// the environment lights. The SSR term is exempt on purpose: it is a copy of
			// scene colour that was already lit -- and already scaled -- once.
			void record(
				rhi::CommandBufferHandle cmd,
				const WaterParams& params,
				const View& view,
				const math::Vec3& lightDirection,
				f32 sunIntensity,
				f32 iblIntensity,
				rhi::TextureHandle sceneDepth,
				rhi::TextureHandle environmentCube);

		private:
			std::shared_ptr<rhi::IRHI> rhi_;

			rhi::BindGroupLayoutHandle layout_ = INVALID_HANDLE;
			rhi::PipelineLayoutHandle pipelineLayout_ = INVALID_HANDLE;
			rhi::PipelineHandle pipeline_ = INVALID_HANDLE;
			rhi::BindGroupHandle group_ = INVALID_HANDLE;

			rhi::TextureHandle boundDepth_ = INVALID_HANDLE;
			rhi::TextureHandle boundCube_ = INVALID_HANDLE;

			// --- screen-space reflection ---------------------------------------------

			rhi::BindGroupLayoutHandle ssrLayout_ = INVALID_HANDLE;
			rhi::PipelineLayoutHandle ssrPipelineLayout_ = INVALID_HANDLE;
			rhi::PipelineHandle ssrPipeline_ = INVALID_HANDLE;
			rhi::BindGroupHandle ssrGroup_ = INVALID_HANDLE;

			// Reflected colour in rgb, the march's confidence in a. Owned here and stable
			// across frames, so the surface's descriptor for it never has to move.
			rhi::TextureHandle reflectionTarget_ = INVALID_HANDLE;

			rhi::TextureHandle boundSsrDepth_ = INVALID_HANDLE;
			rhi::TextureHandle boundSsrColor_ = INVALID_HANDLE;

			f32 time_ = 0.0f;

			bool ready_ = false;
		};
	}
}

#endif
