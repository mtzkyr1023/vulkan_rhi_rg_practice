#ifndef _MV_VULKAN_DESCRIPTOR_H_
#define _MV_VULKAN_DESCRIPTOR_H_

#include <vector>

#include <vulkan/vulkan.h>

#include "util/types.h"

namespace mv
{
	namespace backend
	{
		namespace vk1_4
		{
			using namespace types;

			class VulkanDevice;
			class VulkanBindGroupLayout;

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
				void initialize(VulkanDevice* device, u32 framesInFlight);
				void deinitialize();

				void beginFrame(u32 frameIndex);

				VkDescriptorSet allocate(const VulkanBindGroupLayout* layout);

			private:
				DescriptorPoolWrapper createPool(u32 maxSets);
				
			private:
				std::vector<DescriptorFrameContext> frames_;


				u32 currentFrame_ = 0;

				VulkanDevice* device_ = nullptr;
			};
		}
	}
}

#endif