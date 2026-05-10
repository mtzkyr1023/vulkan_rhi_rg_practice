#ifndef _MV_COMMANDBUFFER_H_
#define _MV_COMMANDBUFFER_H_

#include "memory"

#include "rhi/resource.h"
#include "util/types.h"

namespace mv
{
	namespace rhi
	{
		using namespace types;

		class ICommandBuffer
		{
		public:
			virtual ~ICommandBuffer() {}
			virtual void begin() = 0;
			virtual void end() = 0;
			virtual void bindVertexBuffer(BufferHandle buffer) = 0;
			virtual void bindIndexBuffer(BufferHandle buffer) = 0;
			virtual void draw(u32 vertexCount, u32 instanceCount, u32 firstVertex, u32 firstInstance) = 0;
			virtual void drawIndexed(u32 indexCount, u32 instanceCount, u32 firstIndex, s32 vertexOffset, u32 firstInstance) = 0;
		};

		class VulkanCommandBuffer : public ICommandBuffer
		{
		public:
			VulkanCommandBuffer(const std::shared_ptr<class VulkanRHI>& rhi);
			~VulkanCommandBuffer();

			void begin() override;
			void end() override;
			void bindVertexBuffer(BufferHandle buffer) override;
			void bindIndexBuffer(BufferHandle buffer) override;
			void draw(u32 vertexCount, u32 instanceCount, u32 firstVertex, u32 firstInstance) override;
			void drawIndexed(u32 indexCount, u32 instanceCount, u32 firstIndex, s32 vertexOffset, u32 firstInstance) override;

		private:
			std::weak_ptr<class VulkanRHI> rhi_;
			u32 handle_;
		};
	}
}

#endif
