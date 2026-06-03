#ifndef _MV_VULKAN_COMMAND_H_
#define _MV_VULKAN_COMMAND_H_

#include "vulkan/vulkan.h"

#include "util/types.h"
#include "rhi/commandbuffer.h"

namespace mv
{
	namespace backend
	{
		using namespace types;

		class VulkanCommandBuffer
		{
		public:
			VulkanCommandBuffer();
			~VulkanCommandBuffer();

			void initialize(VkDevice device, VkCommandPool commandPool);
			void deinitialize(VkDevice device, VkCommandPool commandPool);

			void begin();
			void end();

			void bindVertexBuffer(rhi::BufferHandle buffer);
			void bindIndexBuffer(rhi::BufferHandle buffer);

			void draw(u32 vertexCount, u32 instanceCount, u32 firstVertex, u32 firstInstance);
			void drawIndexed(u32 indexCount, u32 instanceCount, u32 firstIndex, s32 vertexOffset, u32 firstInstance);
		
		private:
			VkCommandBuffer commandBuffer_ = VK_NULL_HANDLE;
		};

		class VulkanCommandPool : public rhi::ICommandPool
		{
		public:
			VulkanCommandPool();
			~VulkanCommandPool();

			void initialize(VkDevice device, u32 queueFamilyIndex);
			void deinitialize(VkDevice);

		private:
			virtual rhi::CommandBufferHandle createCommandBuffer() override;

		private:
			VkCommandPool commandPool_ = VK_NULL_HANDLE;

			std::vector<std::shared_ptr<VulkanCommandBuffer>> releaseArray_;

			VkDevice device_ = VK_NULL_HANDLE;
		};
	}
}

#endif