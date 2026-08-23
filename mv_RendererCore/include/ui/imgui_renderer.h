
#ifndef _MV_IMGUI_RENDERER_H_
#define _MV_IMGUI_RENDERER_H_

#include <memory>
#include <vector>

#include "rhi/rhi.h"

namespace mv
{
	namespace ui
	{
		using namespace types;

		// Draws Dear ImGui through the RHI rather than through imgui's own Vulkan/D3D12
		// backends, so there is a single rendering path shared by both backends.
		//
		// Platform input still comes from imgui_impl_win32; that touches only Win32.
		class ImGuiRenderer
		{
		public:
			struct ShaderCode
			{
				const u32* bytecode = nullptr;
				u32 size = 0;
			};

			// depthFormat must match the render pass this will be recorded into: Vulkan
			// requires a pipeline's depth attachment format to equal the attachment's own,
			// even when the pipeline does no depth testing.
			bool initialize(
				const std::shared_ptr<rhi::IRHI>& rhi,
				const ShaderCode& vs,
				const ShaderCode& ps,
				rhi::ETextureFormat depthFormat = rhi::ETextureFormat::eUndefined);
			void deinitialize();

			// Records the current ImGui draw data. Must be called inside a render pass.
			void render(rhi::CommandBufferHandle cmd, u32 frameIndex);

		private:
			// ImGui rebuilds its geometry every frame, so each frame in flight needs its own
			// buffers: overwriting one the GPU may still be reading would corrupt that frame.
			struct FrameBuffers
			{
				rhi::BufferHandle vertexBuffer = INVALID_HANDLE;
				rhi::BufferHandle indexBuffer = INVALID_HANDLE;
				rhi::BufferHandle constantBuffer = INVALID_HANDLE;

				u32 vertexCapacity = 0;
				u32 indexCapacity = 0;
			};

			bool createFontTexture();
			void ensureCapacity(FrameBuffers& frame, u32 vertexCount, u32 indexCount);

		private:
			std::shared_ptr<rhi::IRHI> rhi_;

			std::vector<FrameBuffers> frames_;

			rhi::PipelineHandle pipeline_ = INVALID_HANDLE;
			rhi::PipelineLayoutHandle pipelineLayout_ = INVALID_HANDLE;
			rhi::BindGroupLayoutHandle bindGroupLayout_ = INVALID_HANDLE;

			// One bind group per frame: the constant buffer it points at differs per frame.
			std::vector<rhi::BindGroupHandle> bindGroups_;

			rhi::TextureHandle fontTexture_ = INVALID_HANDLE;
		};
	}
}

#endif
