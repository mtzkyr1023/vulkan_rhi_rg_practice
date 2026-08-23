

#include "rhi/vk1_4/vulkan_pipeline.h"
#include "rhi/vk1_4/vulkan_device.h"

namespace mv::backend::vk1_4
{
	VkFormat toVkFormat(rhi::ETextureFormat format)
	{
		switch (format)
		{
		case rhi::ETextureFormat::eR8G8B8A8_UNORM:     return VK_FORMAT_R8G8B8A8_UNORM;
		case rhi::ETextureFormat::eR8G8B8A8_SRGB:      return VK_FORMAT_R8G8B8A8_SRGB;
		case rhi::ETextureFormat::eB8G8R8A8_UNORM:     return VK_FORMAT_B8G8R8A8_UNORM;
		case rhi::ETextureFormat::eD32_SFLOAT:         return VK_FORMAT_D32_SFLOAT;
		case rhi::ETextureFormat::eD24_UNORM_S8_UINT:  return VK_FORMAT_D24_UNORM_S8_UINT;
		case rhi::ETextureFormat::eR32G32_UINT:        return VK_FORMAT_R32G32_UINT;
		case rhi::ETextureFormat::eUndefined:
		default:                                       return VK_FORMAT_UNDEFINED;
		}
	}

	rhi::ETextureFormat fromVkFormat(VkFormat format)
	{
		switch (format)
		{
		case VK_FORMAT_R8G8B8A8_UNORM:     return rhi::ETextureFormat::eR8G8B8A8_UNORM;
		case VK_FORMAT_R8G8B8A8_SRGB:      return rhi::ETextureFormat::eR8G8B8A8_SRGB;
		case VK_FORMAT_B8G8R8A8_UNORM:     return rhi::ETextureFormat::eB8G8R8A8_UNORM;
		case VK_FORMAT_D32_SFLOAT:         return rhi::ETextureFormat::eD32_SFLOAT;
		case VK_FORMAT_D24_UNORM_S8_UINT:  return rhi::ETextureFormat::eD24_UNORM_S8_UINT;
		case VK_FORMAT_R32G32_UINT:        return rhi::ETextureFormat::eR32G32_UINT;
		default:                           return rhi::ETextureFormat::eUndefined;
		}
	}

	static VkFormat toVkVertexFormat(rhi::EVertexFormat format)
	{
		switch (format)
		{
		case rhi::EVertexFormat::eFloat:            return VK_FORMAT_R32_SFLOAT;
		case rhi::EVertexFormat::eFloat2:           return VK_FORMAT_R32G32_SFLOAT;
		case rhi::EVertexFormat::eFloat3:           return VK_FORMAT_R32G32B32_SFLOAT;
		case rhi::EVertexFormat::eUint:             return VK_FORMAT_R32_UINT;
		case rhi::EVertexFormat::eR8G8B8A8_UNORM:   return VK_FORMAT_R8G8B8A8_UNORM;
		case rhi::EVertexFormat::eFloat4:
		default:                                    return VK_FORMAT_R32G32B32A32_SFLOAT;
		}
	}

	static VkPrimitiveTopology toVkTopology(rhi::EPrimitiveTopology topology)
	{
		switch (topology)
		{
		case rhi::EPrimitiveTopology::ePointList:      return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
		case rhi::EPrimitiveTopology::eLineList:       return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
		case rhi::EPrimitiveTopology::eTriangleStrip:  return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
		case rhi::EPrimitiveTopology::eTriangleList:
		default:                                       return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
		}
	}

	static VkCompareOp toVkCompareOp(rhi::ECompareOp op)
	{
		switch (op)
		{
		case rhi::ECompareOp::eNever:         return VK_COMPARE_OP_NEVER;
		case rhi::ECompareOp::eLess:          return VK_COMPARE_OP_LESS;
		case rhi::ECompareOp::eEqual:         return VK_COMPARE_OP_EQUAL;
		case rhi::ECompareOp::eLessEqual:     return VK_COMPARE_OP_LESS_OR_EQUAL;
		case rhi::ECompareOp::eGreater:       return VK_COMPARE_OP_GREATER;
		case rhi::ECompareOp::eNotEqual:      return VK_COMPARE_OP_NOT_EQUAL;
		case rhi::ECompareOp::eGreaterEqual:  return VK_COMPARE_OP_GREATER_OR_EQUAL;
		case rhi::ECompareOp::eAlways:
		default:                              return VK_COMPARE_OP_ALWAYS;
		}
	}

	static VkBlendFactor toVkBlendFactor(rhi::EBlendFactor factor)
	{
		switch (factor)
		{
		case rhi::EBlendFactor::eZero:               return VK_BLEND_FACTOR_ZERO;
		case rhi::EBlendFactor::eSrcColor:           return VK_BLEND_FACTOR_SRC_COLOR;
		case rhi::EBlendFactor::eOneMinusSrcColor:   return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
		case rhi::EBlendFactor::eDstColor:           return VK_BLEND_FACTOR_DST_COLOR;
		case rhi::EBlendFactor::eOneMinusDstColor:   return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
		case rhi::EBlendFactor::eSrcAlpha:           return VK_BLEND_FACTOR_SRC_ALPHA;
		case rhi::EBlendFactor::eOneMinusSrcAlpha:   return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
		case rhi::EBlendFactor::eDstAlpha:           return VK_BLEND_FACTOR_DST_ALPHA;
		case rhi::EBlendFactor::eOneMinusDstAlpha:   return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
		case rhi::EBlendFactor::eOne:
		default:                                     return VK_BLEND_FACTOR_ONE;
		}
	}

	static VkBlendOp toVkBlendOp(rhi::EBlendOp op)
	{
		switch (op)
		{
		case rhi::EBlendOp::eSubtract:         return VK_BLEND_OP_SUBTRACT;
		case rhi::EBlendOp::eReverseSubtract:  return VK_BLEND_OP_REVERSE_SUBTRACT;
		case rhi::EBlendOp::eMin:              return VK_BLEND_OP_MIN;
		case rhi::EBlendOp::eMax:              return VK_BLEND_OP_MAX;
		case rhi::EBlendOp::eAdd:
		default:                               return VK_BLEND_OP_ADD;
		}
	}

	static VkColorComponentFlags toVkColorComponents(rhi::EColorComponent mask)
	{
		VkColorComponentFlags flags = 0;
		if ((mask & rhi::EColorComponent::eR) == rhi::EColorComponent::eR) flags |= VK_COLOR_COMPONENT_R_BIT;
		if ((mask & rhi::EColorComponent::eG) == rhi::EColorComponent::eG) flags |= VK_COLOR_COMPONENT_G_BIT;
		if ((mask & rhi::EColorComponent::eB) == rhi::EColorComponent::eB) flags |= VK_COLOR_COMPONENT_B_BIT;
		if ((mask & rhi::EColorComponent::eA) == rhi::EColorComponent::eA) flags |= VK_COLOR_COMPONENT_A_BIT;
		return flags;
	}

	static VkCullModeFlags toVkCullMode(rhi::ECullMode mode)
	{
		switch (mode)
		{
		case rhi::ECullMode::eNone:   return VK_CULL_MODE_NONE;
		case rhi::ECullMode::eFront:  return VK_CULL_MODE_FRONT_BIT;
		case rhi::ECullMode::eBack:
		default:                      return VK_CULL_MODE_BACK_BIT;
		}
	}

	void VulkanShader::initialize(VkDevice device, const rhi::ShaderDesc& desc)
	{
		VkShaderModuleCreateInfo ci{};
		ci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
		ci.flags = 0;
		ci.pCode = desc.bytecode;
		ci.codeSize = desc.bytecodeSize;
		vkCreateShaderModule(device, &ci, nullptr, &module_);

		type_ = desc.stage;
		entryPoint_ = desc.entryPoint ? desc.entryPoint : "main";
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

		auto toVkStageFlags = [](rhi::EShaderStage stages)
		{
			VkShaderStageFlags flags = 0;
			if ((stages & rhi::EShaderStage::eVertex) == rhi::EShaderStage::eVertex) flags |= VK_SHADER_STAGE_VERTEX_BIT;
			if ((stages & rhi::EShaderStage::eFragment) == rhi::EShaderStage::eFragment) flags |= VK_SHADER_STAGE_FRAGMENT_BIT;
			if ((stages & rhi::EShaderStage::eCompute) == rhi::EShaderStage::eCompute) flags |= VK_SHADER_STAGE_COMPUTE_BIT;
			if ((stages & rhi::EShaderStage::eTask) == rhi::EShaderStage::eTask) flags |= VK_SHADER_STAGE_TASK_BIT_EXT;
			if ((stages & rhi::EShaderStage::eMesh) == rhi::EShaderStage::eMesh) flags |= VK_SHADER_STAGE_MESH_BIT_EXT;
			return flags;
		};

		static const VkDescriptorType descTypeTable[] =
		{
			VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			// Read-write storage buffers use the same descriptor type in Vulkan.
			VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
			VK_DESCRIPTOR_TYPE_SAMPLER,
		};

		std::vector<VkDescriptorBindingFlags> bindingFlags;
		bool anyBindless = false;

		for (const auto& item : desc.bindings)
		{
			VkDescriptorSetLayoutBinding binding{};
			binding.binding = item.binding;
			binding.stageFlags = toVkStageFlags(item.stages);
			binding.descriptorType = descTypeTable[(u32)item.type];
			binding.descriptorCount = item.count;

			bindings.push_back(binding);

			if (item.bindless)
			{
				// PARTIALLY_BOUND: only the slots actually written are read, so the array
				// does not have to be filled. UPDATE_AFTER_BIND: slots may be written while
				// the set is bound, which is what lets textures be registered as they load.
				bindingFlags.push_back(
					VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT |
					VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT);

				anyBindless = true;
			}
			else
			{
				bindingFlags.push_back(0);
			}
		}

		VkDescriptorSetLayoutBindingFlagsCreateInfo flagsInfo{};
		flagsInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
		flagsInfo.bindingCount = (u32)bindingFlags.size();
		flagsInfo.pBindingFlags = bindingFlags.data();

		VkDescriptorSetLayoutCreateInfo ci{};
		ci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		ci.bindingCount = (u32)bindings.size();
		ci.pBindings = bindings.data();

		if (anyBindless)
		{
			ci.pNext = &flagsInfo;
			ci.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
		}

		updateAfterBind_ = anyBindless;

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
			layout.deinitialize(device_->device());
		}
		layouts_.clear();
	}

	rhi::BindGroupLayoutHandle VulkanBindGroupLayoutManager::createBindGroupLayout(const rhi::BindGroupLayoutDesc& desc)
	{
		VulkanBindGroupLayout layout;
		layout.initialize(device_->device(), desc);

		rhi::BindGroupLayoutHandle handle = (rhi::BindGroupLayoutHandle)layouts_.size();

		layouts_.push_back(layout);

		return handle;
	}

	void VulkanPipelineLayout::initialize(VkDevice device, const std::vector<VkDescriptorSetLayout>& layouts, u32 pushConstantSize)
	{
		VkPushConstantRange pushConstantRange{};
		pushConstantRange.stageFlags = VK_SHADER_STAGE_ALL_GRAPHICS;
		pushConstantRange.offset = 0;
		pushConstantRange.size = pushConstantSize;

		VkPipelineLayoutCreateInfo ci{};

		ci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		ci.setLayoutCount = (u32)layouts.size();
		ci.pSetLayouts = layouts.data();
		ci.pushConstantRangeCount = (pushConstantSize > 0) ? 1 : 0;
		ci.pPushConstantRanges = (pushConstantSize > 0) ? &pushConstantRange : nullptr;

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

		layout.initialize(device_->device(), layouts, desc.pushConstantSize);

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
			stageCI.pName = shaderManager_->shader(desc.vs).entryPoint();
			shaderStages.push_back(stageCI);
		}
		{
			VkPipelineShaderStageCreateInfo stageCI{};
			stageCI.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
			stageCI.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
			stageCI.module = shaderManager_->shader(desc.ps).module();
			stageCI.pName = shaderManager_->shader(desc.ps).entryPoint();
			shaderStages.push_back(stageCI);
		}

		std::vector<VkVertexInputBindingDescription> vertexBindings;
		for (const auto& item : desc.vertexLayout.bindings)
		{
			VkVertexInputBindingDescription binding{};
			binding.binding = item.binding;
			binding.stride = item.stride;
			binding.inputRate = item.perInstance ? VK_VERTEX_INPUT_RATE_INSTANCE : VK_VERTEX_INPUT_RATE_VERTEX;
			vertexBindings.push_back(binding);
		}

		std::vector<VkVertexInputAttributeDescription> vertexAttributes;
		for (const auto& item : desc.vertexLayout.attributes)
		{
			VkVertexInputAttributeDescription attribute{};
			attribute.location = item.location;
			attribute.binding = item.binding;
			attribute.format = toVkVertexFormat(item.format);
			attribute.offset = item.offset;
			vertexAttributes.push_back(attribute);
		}

		VkPipelineVertexInputStateCreateInfo vertexInput
		{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
			.vertexBindingDescriptionCount = (u32)vertexBindings.size(),
			.pVertexBindingDescriptions = vertexBindings.data(),
			.vertexAttributeDescriptionCount = (u32)vertexAttributes.size(),
			.pVertexAttributeDescriptions = vertexAttributes.data(),
		};

		VkPipelineInputAssemblyStateCreateInfo inputAssembly
		{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
			.topology = toVkTopology(desc.topology),
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

		// Viewport/scissor are dynamic, but the state itself is still mandatory and its
		// counts must match the number of dynamic viewports/scissors bound later.
		VkPipelineViewportStateCreateInfo viewportState
		{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
			.viewportCount = 1,
			.scissorCount = 1,
		};

		VkPipelineRasterizationStateCreateInfo rasterizer
		{
			.sType =VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
			.depthClampEnable = desc.rasterizer.depthClampEnable ? VK_TRUE : VK_FALSE,
			.polygonMode = (desc.rasterizer.polygonMode == rhi::EPolygonMode::eWireframe) ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL,
			.cullMode = toVkCullMode(desc.rasterizer.cullMode),
			.frontFace = (desc.rasterizer.frontFace == rhi::EFrontFace::eClockwise) ? VK_FRONT_FACE_CLOCKWISE : VK_FRONT_FACE_COUNTER_CLOCKWISE,
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
			.depthTestEnable = desc.depth.depthTestEnable ? VK_TRUE : VK_FALSE,
			.depthWriteEnable = desc.depth.depthWriteEnable ? VK_TRUE : VK_FALSE,
			.depthCompareOp = toVkCompareOp(desc.depth.depthCompareOp),
		};

		VkPipelineColorBlendAttachmentState attachmentTemplate
		{
			.blendEnable = desc.blend.blendEnable ? VK_TRUE : VK_FALSE,
			.srcColorBlendFactor = toVkBlendFactor(desc.blend.srcColorFactor),
			.dstColorBlendFactor = toVkBlendFactor(desc.blend.dstColorFactor),
			.colorBlendOp = toVkBlendOp(desc.blend.colorOp),
			.srcAlphaBlendFactor = toVkBlendFactor(desc.blend.srcAlphaFactor),
			.dstAlphaBlendFactor = toVkBlendFactor(desc.blend.dstAlphaFactor),
			.alphaBlendOp = toVkBlendOp(desc.blend.alphaOp),
			.colorWriteMask = toVkColorComponents(desc.blend.writeMask),
		};

		std::vector<VkFormat> colorFormats;
		for (const auto& format : desc.colorFormats)
		{
			colorFormats.push_back(toVkFormat(format));
		}

		// One blend attachment per color attachment is required, so the shared blend state
		// is replicated across them.
		std::vector<VkPipelineColorBlendAttachmentState> attachments(colorFormats.size(), attachmentTemplate);

		VkPipelineColorBlendStateCreateInfo blend
		{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
			.attachmentCount = (u32)attachments.size(),
			.pAttachments = attachments.data(),
		};

		VkPipelineRenderingCreateInfo renderingInfo
		{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
			.colorAttachmentCount = (u32)colorFormats.size(),
			.pColorAttachmentFormats = colorFormats.data(),
			.depthAttachmentFormat = toVkFormat(desc.depthFormat),
		};

		VkGraphicsPipelineCreateInfo ci{};
		ci.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
		ci.pNext = &renderingInfo;
		ci.stageCount = (u32)shaderStages.size();
		ci.pStages = shaderStages.data();
		ci.pVertexInputState = &vertexInput;
		ci.pInputAssemblyState = &inputAssembly;
		ci.pViewportState = &viewportState;
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
