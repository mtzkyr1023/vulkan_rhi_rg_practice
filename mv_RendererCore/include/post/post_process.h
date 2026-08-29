
#ifndef _MV_POST_PROCESS_H_
#define _MV_POST_PROCESS_H_

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "rhi/rhi.h"

#include "util/types.h"

namespace mv
{
	namespace post
	{
		using namespace types;

		// Everything in the chain is full screen and full resolution unless an effect says
		// otherwise, and half-float because the chain runs before tone mapping for most of
		// its length.
		constexpr rhi::ETextureFormat kChainFormat = rhi::ETextureFormat::eR16G16B16A16_SFLOAT;

		// The three textures an effect can be handed as its chain input: the scene target
		// the geometry passes wrote, and the two the chain ping-pongs between. Fixed and
		// small, so every effect can build one bind group per slot up front instead of
		// rewriting a descriptor that a frame in flight might still be reading.
		enum EInputSlot
		{
			eInputScene = 0,
			eInputPingA,
			eInputPingB,

			eInputSlotCount,
		};

		class PostProcessStack;

		// Zero is a perfectly good handle, so an array of them left brace-initialised looks
		// exactly like an array of bind group 0. Everything below is filled with the invalid
		// value instead, so a slot that was never assigned cannot silently bind something
		// that belongs to another pass.
		template<typename T, size_t N>
		struct HandleArray
		{
			T values[N];

			HandleArray()
			{
				for (size_t i = 0; i < N; i++)
				{
					values[i] = INVALID_HANDLE;
				}
			}

			T& operator[](size_t index) { return values[index]; }
			const T& operator[](size_t index) const { return values[index]; }
		};

		// What an effect is given when it records.
		struct EffectContext
		{
			rhi::IRHI* rhi = nullptr;
			rhi::CommandBufferHandle cmd = INVALID_HANDLE;

			// Which of the stack's textures the chain input currently is. The effect picks
			// the bind group it prepared for that slot.
			EInputSlot inputSlot = eInputScene;

			// Where to render. INVALID_HANDLE never happens; the last effect is handed the
			// backbuffer rather than a chain texture.
			rhi::TextureHandle output = INVALID_HANDLE;

			// Set 0 and set 1, already built by the engine, so an effect can read the scene
			// constants without the stack having to mirror them.
			rhi::BindGroupHandle sceneBindGroup = INVALID_HANDLE;
			rhi::BindGroupHandle bindlessBindGroup = INVALID_HANDLE;

			u32 width = 0;
			u32 height = 0;

			u32 frameIndex = 0;
		};

		// One step of the chain.
		//
		// An effect is a class plus a shader. It owns whatever extra targets it needs, so
		// nothing in the stack has to know that bloom wants a pyramid or that temporal
		// anti-aliasing wants a history buffer: the chain still sees one texture in and one
		// texture out.
		class IPostEffect
		{
		public:
			virtual ~IPostEffect() {}

			virtual const char* name() const = 0;

			virtual bool initialize(const std::shared_ptr<rhi::IRHI>& rhi, PostProcessStack& stack) = 0;
			virtual void deinitialize() = 0;

			virtual void record(const EffectContext& context) = 0;

			// Drawn inside the stack's UI section.
			virtual void ui() {}

			// An effect that reads the frame before this one cannot be skipped and then
			// resumed without its history being stale. The stack calls this when the effect
			// comes back after sitting out a frame.
			virtual void reset() {}

			// The chain textures have been rebuilt at a new size. Anything the effect owns
			// that was sized to the window, and every bind group naming a texture that has
			// been replaced, has to be rebuilt here. Pipelines survive: only the sizes
			// changed, not the formats.
			virtual void onResize(PostProcessStack& stack) {}

			bool enabled = true;

			// Owned by the stack, which uses it to notice that `enabled` was turned back on.
			bool ranLastFrame = false;
		};

		// Base for the common case: one shader, one draw, chain input to chain output.
		//
		// Two pipelines rather than one because the last effect in the chain renders to the
		// backbuffer and the rest render to a half-float chain texture, and a pipeline is
		// built against a specific target format on both APIs.
		class FullscreenEffect : public IPostEffect
		{
		public:
			bool initialize(const std::shared_ptr<rhi::IRHI>& rhi, PostProcessStack& stack) override;
			void deinitialize() override;

			void record(const EffectContext& context) override;
			void onResize(PostProcessStack& stack) override;

		protected:
			// The base name of the pixel shader, without the extension.
			virtual const char* shaderName() const = 0;

			// The push constant block this effect wants, or nothing.
			virtual const void* constants(u32& size) const { size = 0; return nullptr; }

			// Anything the subclass needs beyond the shared pipelines.
			virtual bool onInitialize(PostProcessStack& stack) { return true; }

			void drawFullscreen(const EffectContext& context, rhi::BindGroupHandle resources);

		protected:
			std::shared_ptr<rhi::IRHI> rhi_;
			PostProcessStack* stack_ = nullptr;

			rhi::PipelineHandle chainPipeline_ = INVALID_HANDLE;
			rhi::PipelineHandle outputPipeline_ = INVALID_HANDLE;

			// One per possible chain input, built once so no descriptor is ever rewritten
			// while a frame in flight might still be reading it.
			HandleArray<rhi::BindGroupHandle, eInputSlotCount> inputGroups_;
		};

		// An ordered chain of full-screen effects between the scene target and the
		// backbuffer.
		//
		// The scene is rendered into a half-float target rather than straight to the
		// backbuffer, and tone mapping is one of the effects rather than the tail of the
		// material shader. That is what makes everything before it able to work in the
		// range the lighting actually produced, and it is the reason bloom can find values
		// above one at all.
		class PostProcessStack
		{
		public:
			struct Desc
			{
				u32 width = 0;
				u32 height = 0;

				rhi::ETextureFormat backbufferFormat = rhi::ETextureFormat::eUndefined;

				// Sets 0 and 1 of the shared pipeline layout, so an effect shader can read
				// the scene constants exactly as the material shaders do. Post resources go
				// in set 2, the way the visibility resolve puts its own there.
				rhi::BindGroupLayoutHandle sceneLayout = INVALID_HANDLE;
				rhi::BindGroupLayoutHandle bindlessLayout = INVALID_HANDLE;

				// Takes a base name such as "tonemap.ps" and returns its bytecode, so the
				// stack never has to know which backend is running or where the shaders sit.
				std::function<std::vector<u32>(const char*)> loadShader;
			};

			bool initialize(const std::shared_ptr<rhi::IRHI>& rhi, const Desc& desc);
			void deinitialize();

			// Takes ownership. Effects run in the order they are added, so tone mapping is
			// added where the chain should stop being half-float.
			void add(std::unique_ptr<IPostEffect> effect);

			// Rebuilds the scene target and the chain at a new size and tells every effect to
			// do the same. Pipelines are untouched: the formats have not changed.
			void resize(u32 width, u32 height);

			// Runs every enabled effect, reading the scene target and ending on the
			// backbuffer. Emits no barriers of its own: the caller declares the chain
			// textures to the render graph.
			void execute(const EffectContext& base);

			void ui();

			rhi::TextureHandle sceneColor() const { return sceneColor_; }
			rhi::TextureHandle chainTexture(EInputSlot slot) const;

			// The shared pieces every effect builds its pipeline from: one full-screen
			// vertex shader, one bind group layout, and one pipeline layout.
			rhi::ShaderHandle fullscreenVertexShader() const { return fullscreenVs_; }
			rhi::BindGroupLayoutHandle resourceLayout() const { return resourceLayout_; }
			rhi::PipelineLayoutHandle pipelineLayout() const { return pipelineLayout_; }

			rhi::ETextureFormat chainFormat() const { return kChainFormat; }
			rhi::ETextureFormat outputFormat() const { return backbufferFormat_; }

			u32 width() const { return width_; }
			u32 height() const { return height_; }

			// Convenience for effects: a bind group over this layout with up to three
			// textures and the shared sampler. Unused slots take the first texture, because
			// a descriptor left unwritten is not valid to bind.
			rhi::BindGroupHandle createResourceGroup(
				rhi::TextureHandle texture0,
				rhi::TextureHandle texture1 = INVALID_HANDLE,
				rhi::TextureHandle texture2 = INVALID_HANDLE);

			// Re-points an existing group at different textures. Neither backend can free a
			// bind group, so a resize that built new ones would consume descriptors it never
			// gets back; rewriting in place costs nothing. Safe only because a resize waits
			// for the GPU first -- otherwise this would be editing a descriptor a frame in
			// flight is still reading.
			void updateResourceGroup(
				rhi::BindGroupHandle group,
				rhi::TextureHandle texture0,
				rhi::TextureHandle texture1 = INVALID_HANDLE,
				rhi::TextureHandle texture2 = INVALID_HANDLE);

			// Creates the group the first time and re-points it on every resize after.
			void assignResourceGroup(
				rhi::BindGroupHandle& group,
				rhi::TextureHandle texture0,
				rhi::TextureHandle texture1 = INVALID_HANDLE,
				rhi::TextureHandle texture2 = INVALID_HANDLE);

			// Builds a pipeline for a full-screen effect from its pixel shader alone.
			rhi::PipelineHandle createEffectPipeline(rhi::ShaderHandle ps, rhi::ETextureFormat colorFormat);

			// True when the stack or one of its effects owns this texture, rather than it
			// being the backbuffer the render graph is tracking.
			bool ownsTexture(rhi::TextureHandle texture) const;

			// The chain ping-pongs inside a single render graph pass, so the graph cannot
			// see the transitions between one effect writing a texture and the next reading
			// it. Effects bracket every draw with these instead. Textures the graph does
			// track are left alone.
			void beginTarget(const EffectContext& context, rhi::TextureHandle target);
			void endTarget(const EffectContext& context, rhi::TextureHandle target);

			// Effects register the targets they create so the bracketing above knows them.
			void registerOwnedTexture(rhi::TextureHandle texture);

			// Loads shaders/<name>.ps.<spv|cso>, whichever the running backend needs.
			rhi::ShaderHandle loadPixelShader(const char* name);

			// The same, for an effect that runs as a dispatch rather than a fullscreen draw.
			rhi::ShaderHandle loadComputeShader(const char* name);

			const std::vector<std::unique_ptr<IPostEffect>>& effects() const { return effects_; }

		private:
			std::shared_ptr<rhi::IRHI> rhi_;

			// The geometry passes render here.
			rhi::TextureHandle sceneColor_ = INVALID_HANDLE;

			// Ping-pong pair. Two are enough for a linear chain of any length.
			rhi::TextureHandle chain_[2]{ INVALID_HANDLE, INVALID_HANDLE };

			rhi::ShaderHandle fullscreenVs_ = INVALID_HANDLE;
			rhi::BindGroupLayoutHandle resourceLayout_ = INVALID_HANDLE;
			rhi::PipelineLayoutHandle pipelineLayout_ = INVALID_HANDLE;

			rhi::ETextureFormat backbufferFormat_ = rhi::ETextureFormat::eUndefined;

			// Every texture the stack or its effects created, so a target can be told apart
			// from the backbuffer without asking the render graph.
			std::vector<rhi::TextureHandle> ownedTextures_;

			std::function<std::vector<u32>(const char*)> loadShader_;

			std::vector<std::unique_ptr<IPostEffect>> effects_;

			u32 width_ = 0;
			u32 height_ = 0;
		};
	}
}

#endif
