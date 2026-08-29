#ifndef _MV_VULKAN_COMMAND_H_
#define _MV_VULKAN_COMMAND_H_

#include <vulkan/vulkan.h>

#include "util/types.h"
#include "rhi/commandbuffer.h"

namespace mv
{
	namespace backend
	{
		namespace vk1_4
		{
			using namespace types;

			class VulkanDevice;
			class VulkanCommandPool;
			class VulkanPipeline;
			struct VulkanBuffer;
			struct VulkanImage;

			struct VulkanStateInfo
			{
				VkPipelineStageFlags stage;

				VkAccessFlags access;

				VkImageLayout layout;
			};

			VkPipelineStageFlags getPipelineStageFlags(rhi::EResourceState state);
			VkAccessFlags getAccessFlags(rhi::EResourceState state);
			VkImageLayout getImageLayout(rhi::EResourceState state);

			class VulkanCommandBuffer
			{
			public:
				void initialize(VkDevice device, VkCommandPool pool);
				void deinitialize(VkDevice device, VkCommandPool pool);

				void begin();
				void end();

				void bindGraphicsPipeline(const VulkanPipeline& pipeline);

				void bindVertexBuffer(const VulkanBuffer& buffer);
				void bindIndexBuffer(const VulkanBuffer& buffer);

				void draw(u32 vertexCount, u32 instanceCount, u32 firstVertex, u32 firstInstance);
				void drawIndexed(u32 indexCount, u32 instanceCount, u32 firstIndex, s32 vertexOffset, u32 firstInstance);

				void pipelineBarrier(const VulkanStateInfo& before, const VulkanStateInfo& after, const VulkanImage& image);

				VkCommandBuffer commandBuffer() const { return commandBuffer_; }

				// Which bind point the following descriptor set binds belong to. Vulkan
				// names it in every call rather than keeping it as state, so this only
				// exists so the RHI can present one bindBindGroup to both.
				bool isComputeBindPoint() const { return computeBindPoint_; }
				void setComputeBindPoint(bool compute) { computeBindPoint_ = compute; }

			private:
				VkCommandBuffer commandBuffer_ = VK_NULL_HANDLE;

				bool computeBindPoint_ = false;

				VulkanDevice* device_ = nullptr;
				VulkanCommandPool* commandPool_ = nullptr;
			};

			class VulkanCommandPool : public rhi::ICommandPool
			{
			public:
				void initialize(VulkanDevice* device, u32 queueFamilyIndex);
				void deinitialize();

				VulkanCommandBuffer& getCommandBuffer(rhi::CommandBufferHandle handle) const;

				VkCommandPool commandPool() const { return commandPool_; }

			private:
				virtual rhi::CommandBufferHandle createCommandBuffer() override;

			private:
				VkCommandPool commandPool_ = VK_NULL_HANDLE;

				std::vector<std::shared_ptr<VulkanCommandBuffer>> releaseArray_;

				VulkanDevice* device_ = nullptr;
			};
		}
	}
}

#endif