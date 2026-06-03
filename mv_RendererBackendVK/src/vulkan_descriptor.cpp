
#include "cassert"

#include "vulkan_descriptor.h"



namespace mv::backend
{
	void VulkanDescriptorAllocator::initialize(VkDevice device, u32 framesInFlight)
	{
		frames_.resize(framesInFlight);
	}

	void VulkanDescriptorAllocator::deinitialize(VkDevice device)
	{
		for (auto& frame : frames_)
		{
			for (auto& pool : frame.pools)
			{
				vkDestroyDescriptorPool(device, pool.pool, nullptr);
			}
			frame.pools.clear();
			frame.currentPool = 0;
		}
	}
	void VulkanDescriptorAllocator::beginFrame(VkDevice device, u32 frameIndex)
	{
		currentFrame_ = frameIndex;
		auto& frame = frames_[currentFrame_];

		frame.currentPool - 0;
		for (auto& pool : frame.pools)
		{
			vkResetDescriptorPool(device, pool.pool, 0);

			pool.usedSets = 0;
		}
	}
	VkDescriptorSet VulkanDescriptorAllocator::allocate(VkDevice device, VkDescriptorSetLayout layout)
	{
		auto& frame = frames_[currentFrame_];

		while (true)
		{
			auto& pool = frame.pools[frame.currentPool];
			VkDescriptorSetAllocateInfo alloc;
			alloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
			alloc.descriptorPool = pool.pool;
			alloc.descriptorSetCount = 1;
			alloc.pSetLayouts = &layout;

			VkDescriptorSet set;

			VkResult result = vkAllocateDescriptorSets(device, &alloc, &set);

			if (result == VK_SUCCESS)
			{
				pool.usedSets++;
				return set;

			}
			else if (result == VK_ERROR_FRAGMENTED_POOL || result == VK_ERROR_OUT_OF_POOL_MEMORY)
			{
				frame.currentPool++;
				if (frame.currentPool >= frame.pools.size())
				{
					frame.pools.push_back(createPool(device, 1000));
				}
				continue;
			}

			assert(false);
			return VK_NULL_HANDLE;
		}
	}
	VulkanDescriptorAllocator::DescriptorPoolWrapper VulkanDescriptorAllocator::createPool(VkDevice device, u32 maxSets)
	{
		DescriptorPoolWrapper wrapper;

		wrapper.maxSets = maxSets;

		std::vector<VkDescriptorPoolSize> sizes =
		{
			{ VK_DESCRIPTOR_TYPE_SAMPLER, maxSets },
			{ VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, maxSets },
			{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, maxSets },
			{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, maxSets },
			{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, maxSets },
			{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, maxSets },
			{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, maxSets },
			{ VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, maxSets },
		};
		
		VkDescriptorPoolCreateInfo info;
		info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		info.maxSets = maxSets;
		info.poolSizeCount = static_cast<uint32_t>(sizes.size());
		info.pPoolSizes = sizes.data();

		vkCreateDescriptorPool(device, &info, nullptr, &wrapper.pool);

		return wrapper;
	}
}