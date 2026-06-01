#ifndef _MV_VULKAN_DESCRIPTOR_H_
#define _MV_VULKAN_DESCRIPTOR_H_

#include "vector"

#include "vulkan/vulkan.h"

#include "util/types.h"

namespace mv
{
	namespace backend
	{
		using namespace types;

		class VulkanDescriptorAllocator
		{
		private:
			struct DescriptorPoolWrapper
			{
				VkDescriptorPool pool = VK_NULL_HANDLE;
				u32 usedSets = 0;
				u32 maxSets = 0;
			};

			struct DescriptorFrameContext
			{
				std::vector<DescriptorPoolWrapper> pools;

				u32 currentPool = 0;
			};

		public:
			VulkanDescriptorAllocator();
			~VulkanDescriptorAllocator();

			void initialize(VkDevice device, u32 framesInFlight);
			void deinitialize(VkDevice device);

			void beginFrame(VkDevice device, u32 frameIndex);

			VkDescriptorSet allocate(VkDevice device, VkDescriptorSetLayout layout);

		private:
			DescriptorPoolWrapper createPool(VkDevice device, u32 maxSets);

		private:
			std::vector<DescriptorFrameContext> frames_;


			u32 currentFrame_ = 0;
		};
	}
}

#endif