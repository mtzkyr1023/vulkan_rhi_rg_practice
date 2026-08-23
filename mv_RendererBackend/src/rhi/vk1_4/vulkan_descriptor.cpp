
#include <cassert>

#include "rhi/vk1_4/vulkan_descriptor.h"
#include "rhi/vk1_4/vulkan_device.h"
#include "rhi/vk1_4/vulkan_pipeline.h"



namespace mv::backend::vk1_4
{
	void VulkanDescriptorAllocator::initialize(VulkanDevice* device, u32 framesInFlight)
	{
		device_ = device;

		frames_.resize(framesInFlight);

		// allocate() indexes frame.pools[currentPool] before checking anything, so every
		// frame has to start with one pool already present.
		for (auto& frame : frames_)
		{
			frame.pools.push_back(createPool(1000));
			frame.currentPool = 0;
		}
	}

	void VulkanDescriptorAllocator::deinitialize()
	{
		for (auto& frame : frames_)
		{
			for (auto& pool : frame.pools)
			{
				vkDestroyDescriptorPool(device_->device(), pool.pool, nullptr);
			}
			frame.pools.clear();
			frame.currentPool = 0;
		}
	}
	void VulkanDescriptorAllocator::beginFrame(u32 frameIndex)
	{
		currentFrame_ = frameIndex;
		auto& frame = frames_[currentFrame_];

		frame.currentPool = 0;
		for (auto& pool : frame.pools)
		{
			vkResetDescriptorPool(device_->device(), pool.pool, 0);

			pool.usedSets = 0;
		}
	}
	VkDescriptorSet VulkanDescriptorAllocator::allocate(const VulkanBindGroupLayout* layout)
	{
		auto& frame = frames_[currentFrame_];

		while (true)
		{
			auto& pool = frame.pools[frame.currentPool];

			std::vector<VkDescriptorSetLayout> layouts =
			{
				layout->layout(),
			};

			VkDescriptorSetAllocateInfo alloc{};
			alloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
			alloc.descriptorPool = pool.pool;
			alloc.descriptorSetCount = 1;
			alloc.pSetLayouts =  layouts.data();

			VkDescriptorSet set;

			VkResult result = vkAllocateDescriptorSets(device_->device(), &alloc, &set);

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
					frame.pools.push_back(createPool(1000));
				}
				continue;
			}

			assert(false);
			return VK_NULL_HANDLE;
		}
	}
	VulkanDescriptorAllocator::DescriptorPoolWrapper VulkanDescriptorAllocator::createPool(u32 maxSets)
	{
		DescriptorPoolWrapper wrapper;

		wrapper.maxSets = maxSets;

		std::vector<VkDescriptorPoolSize> sizes =
		{
			{ VK_DESCRIPTOR_TYPE_SAMPLER, maxSets },
			// A single bindless set declares thousands of sampled image descriptors on its
			// own, so this pool cannot be sized per set like the others.
			{ VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, maxSets + 8192 },
			{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, maxSets },
			{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, maxSets },
			{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, maxSets },
			{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, maxSets },
			{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, maxSets },
			{ VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, maxSets },
		};
		
		VkDescriptorPoolCreateInfo info{};
		info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		info.maxSets = maxSets;
		info.poolSizeCount = static_cast<uint32_t>(sizes.size());
		info.pPoolSizes = sizes.data();
		// A set whose layout allows update-after-bind can only be allocated from a pool
		// that allows it too, and the flag is harmless on the sets that do not use it.
		info.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;

		vkCreateDescriptorPool(device_->device(), &info, nullptr, &wrapper.pool);

		return wrapper;
	}
}