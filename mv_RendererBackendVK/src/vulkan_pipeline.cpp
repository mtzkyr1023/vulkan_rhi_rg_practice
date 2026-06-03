#include "vulkan_pipeline.h"

namespace mv::backend
{
	void VulkanShader::initialize(VkDevice device, const rhi::ShaderDesc& desc)
	{
		VkShaderModuleCreateInfo ci{};
		ci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
		ci.flags = 0;
		ci.pCode = desc.bytecode;
		ci.codeSize = desc.bytecodeSize;
		vkCreateShaderModule(device, &ci, nullptr, &module_);

		type_ = desc.stage;
	}

	void VulkanShader::deinitialize(VkDevice device)
	{
		vkDestroyShaderModule(device, module_, nullptr);
	}

	void VulkanShaderManager::initialize(VkDevice device)
	{
		device_ = device;
	}

	void VulkanShaderManager::deinitialize()
	{
		for (auto& shader : shaders_)
		{
			shader.deinitialize(device_);
		}
	}

	rhi::ShaderHandle VulkanShaderManager::createShader(const rhi::ShaderDesc& desc)
	{
		VulkanShader shader;

		shader.initialize(device_, desc);

		shaders_.push_back(shader);
		u32 handle = nextHandleIndex_;
		nextHandleIndex_++;

		return handle;
	}

	void VulkanPipelineLayout::initialize(VkDevice device, const rhi::PipelineLayoutDesc& desc)
	{

	}

	void VulkanPipelineLayout::deinitialize(VkDevice device)
	{
		vkDestroyPipelineLayout(device, layout_, nullptr);
	}

	void VulkanPipeline::initialize(VkDevice device, const rhi::GraphicsPipelineDesc& desc)
	{

	}

	void VulkanPipeline::deinitialize(VkDevice device)
	{
		vkDestroyPipeline(device, pipeline_, nullptr);
	}


	void VulkanPipelineManager::initialize(VkDevice device)
	{
		device_ = device;
	}

	void VulkanPipelineManager::deinitialize()
	{

	}
}