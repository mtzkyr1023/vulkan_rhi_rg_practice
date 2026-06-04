#include "vulkan_pipeline.h"
#include "vulkan_device.h"

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

	void VulkanShaderManager::initialize(VulkanDevice* device)
	{
		device_ = device;
	}

	void VulkanShaderManager::deinitialize()
	{
		for (auto& shader : shaders_)
		{
			shader.deinitialize(device_->device());
		}
	}

	rhi::ShaderHandle VulkanShaderManager::createShader(const rhi::ShaderDesc& desc)
	{
		VulkanShader shader;

		shader.initialize(device_->device(), desc);

		u32 handle = (u32)shaders_.size();
		shaders_.push_back(shader);

		return handle;
	}


	void VulkanBindGroupLayout::initialize(VkDevice device, const rhi::BindGroupLayoutDesc& desc)
	{
		std::vector<VkDescriptorSetLayoutBinding> bindings;

		// ToDo:ビットフラグ対応
		static const VkShaderStageFlags stageFlagTable[] =
		{
			VK_SHADER_STAGE_VERTEX_BIT,
			VK_SHADER_STAGE_FRAGMENT_BIT,

			VK_SHADER_STAGE_COMPUTE_BIT,

			VK_SHADER_STAGE_TASK_BIT_EXT,
			VK_SHADER_STAGE_MESH_BIT_EXT,
		};

		static const VkDescriptorType descTypeTable[] =
		{
			VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
			VK_DESCRIPTOR_TYPE_SAMPLER,
		};

		for (const auto& item : desc.bindings)
		{
			VkDescriptorSetLayoutBinding binding{};
			binding.binding = item.binding;
			binding.stageFlags = stageFlagTable[(u32)item.stageType];
			binding.descriptorType = descTypeTable[(u32)item.type];
			binding.descriptorCount = item.count;

			bindings.push_back(binding);
		}

		VkDescriptorSetLayoutCreateInfo ci{};
		ci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		ci.bindingCount = (u32)bindings.size();
		ci.pBindings = bindings.data();

		vkCreateDescriptorSetLayout(device, &ci, nullptr, &layout_);
	}

	void VulkanBindGroupLayout::deinitialize(VkDevice device)
	{
		vkDestroyDescriptorSetLayout(device, layout_, nullptr);
	}


	void VulkanBindGroupLayoutManager::initialize(VulkanDevice* device)
	{
		device_ = device;
	}

	void VulkanBindGroupLayoutManager::deinitialize()
	{
		for (auto& layout : layouts_)
		{
		}
	}

	rhi::BindGroupLayoutHandle VulkanBindGroupLayoutManager::createBindGroupLayout(const rhi::BindGroupLayoutDesc& desc)
	{
		VulkanBindGroupLayout layout;
		layout.initialize(device_->device(), desc);

		rhi::BindGroupLayoutHandle handle = (rhi::BindGroupLayoutHandle)layouts_.size();

		layouts_.push_back(layout);

		return handle;
	}

	void VulkanPipelineLayout::initialize(VkDevice device, const std::vector<VkDescriptorSetLayout>& layouts)
	{
		VkPipelineLayoutCreateInfo ci{};

		ci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		ci.setLayoutCount = (u32)layouts.size();
		ci.pSetLayouts = layouts.data();
		ci.pushConstantRangeCount = 0;

		vkCreatePipelineLayout(device, &ci, nullptr, &layout_);
	}

	void VulkanPipelineLayout::deinitialize(VkDevice device)
	{
		vkDestroyPipelineLayout(device, layout_, nullptr);
	}

	void VulkanPipeline::initialize(VkDevice device, const VkGraphicsPipelineCreateInfo& ci)
	{
		vkCreateGraphicsPipelines(device, cache_, 1, &ci, nullptr, &pipeline_);
	}

	void VulkanPipeline::deinitialize(VkDevice device)
	{
		vkDestroyPipeline(device, pipeline_, nullptr);
	}


	void VulkanPipelineManager::initialize(VulkanDevice* device, VulkanShaderManager* shaderManager, VulkanBindGroupLayoutManager* layoutManager)
	{
		device_ = device;
		shaderManager_ = shaderManager;
		layoutManager_ = layoutManager;
	}

	void VulkanPipelineManager::deinitialize()
	{
		for (auto& layout : layouts_)
		{
			layout.deinitialize(device_->device());
		}
		for (auto& pipeline : pipelines_)
		{
			pipeline.deinitialize(device_->device());
		}
	}

	rhi::PipelineLayoutHandle VulkanPipelineManager::createPipelineLayout(const rhi::PipelineLayoutDesc& desc)
	{
		VulkanPipelineLayout layout;
		std::vector<VkDescriptorSetLayout> layouts;

		for (const auto& item : desc.bindGroups)
		{
			layouts.push_back(layoutManager_->layout(item).layout());
		}

		layout.initialize(device_->device(), layouts);

		rhi::PipelineLayoutHandle handle = (rhi::PipelineLayoutHandle)layouts_.size();
		layouts_.push_back(layout);

		return handle;
	}

	rhi::PipelineHandle VulkanPipelineManager::createPipeline(const rhi::GraphicsPipelineDesc& desc)
	{
		std::vector<VkPipelineShaderStageCreateInfo> shaderStages;
		{
			VkPipelineShaderStageCreateInfo stageCI{};
			stageCI.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
			stageCI.stage = VK_SHADER_STAGE_VERTEX_BIT;
			stageCI.module = shaderManager_->shader(desc.vs).module();
			stageCI.pName = "main";
			shaderStages.push_back(stageCI);
		}
		{
			VkPipelineShaderStageCreateInfo stageCI{};
			stageCI.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
			stageCI.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
			stageCI.module = shaderManager_->shader(desc.ps).module();
			stageCI.pName = "main";
			shaderStages.push_back(stageCI);
		}

		VkPipelineVertexInputStateCreateInfo vertexInput
		{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
		};

		VkPipelineInputAssemblyStateCreateInfo inputAssembly
		{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
			.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
		};

		VkDynamicState dynamicStates[] =
		{
			VK_DYNAMIC_STATE_VIEWPORT,
			VK_DYNAMIC_STATE_SCISSOR,
		};

		VkPipelineDynamicStateCreateInfo dynamicState
		{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
			.dynamicStateCount = u32(std::size(dynamicStates)),
			.pDynamicStates = dynamicStates,
		};

		VkPipelineRasterizationStateCreateInfo rasterizer
		{
			.sType =VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
			.polygonMode = VK_POLYGON_MODE_FILL,
			.cullMode = VK_CULL_MODE_BACK_BIT,
			.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
			.lineWidth = 1.0f,
		};

		VkPipelineMultisampleStateCreateInfo multisample
		{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
			.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
		};

		VkPipelineDepthStencilStateCreateInfo depth
		{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
			.depthTestEnable = VK_FALSE,
			.depthWriteEnable = VK_FALSE,
			.depthCompareOp = VK_COMPARE_OP_ALWAYS,
		};

		VkPipelineColorBlendAttachmentState attachment
		{
			.blendEnable = VK_FALSE,
			.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
		};

		VkPipelineColorBlendStateCreateInfo blend
		{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
			.attachmentCount = 1,
			.pAttachments = &attachment,
		};

		VkFormat colorFormat = VK_FORMAT_R8G8B8A8_UNORM;
		
		VkPipelineRenderingCreateInfo renderingInfo
		{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
			.colorAttachmentCount = 1,
			.pColorAttachmentFormats = &colorFormat,
			.depthAttachmentFormat = VK_FORMAT_D32_SFLOAT,
		};

		VkGraphicsPipelineCreateInfo ci{};
		ci.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
		ci.pNext = &renderingInfo;
		ci.stageCount = (u32)shaderStages.size();
		ci.pStages = shaderStages.data();
		ci.pVertexInputState = &vertexInput;
		ci.pInputAssemblyState = &inputAssembly;
		ci.pRasterizationState = &rasterizer;
		ci.pMultisampleState = &multisample;
		ci.pDepthStencilState = &depth;
		ci.pColorBlendState = &blend;
		ci.pDynamicState = &dynamicState;
		ci.layout = layouts_[desc.layoutHandle].layout();

		VulkanPipeline pipeline;
		pipeline.initialize(device_->device(), ci);

		rhi::PipelineHandle handle = (u32)pipelines_.size();
		pipelines_.push_back(pipeline);

		return handle;
	}
}