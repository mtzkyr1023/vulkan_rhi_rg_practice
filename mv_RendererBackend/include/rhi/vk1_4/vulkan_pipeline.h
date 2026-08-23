#ifndef _MV_VULKAN_PIPELINE_H_
#define _MV_VULKAN_PIPELINE_H_

#include <string>
#include <vector>

#include <vulkan/vulkan.h>

#include "util/types.h"

#include "rhi/pipeline.h"

namespace mv
{
	namespace backend
	{
		namespace vk1_4
		{
			using namespace types;

			class VulkanDevice;

			VkFormat toVkFormat(rhi::ETextureFormat format);
			rhi::ETextureFormat fromVkFormat(VkFormat format);

			class VulkanShader
			{
			public:
				void initialize(VkDevice device, const rhi::ShaderDesc& desc);
				void deinitialize(VkDevice device);

				VkShaderModule module() const { return module_; }
				rhi::EShaderType type() const { return type_; }
				const char* entryPoint() const { return entryPoint_.c_str(); }

			private:
				VkShaderModule module_;
				rhi::EShaderType type_;

				std::string entryPoint_;
			};

			class VulkanShaderManager : public rhi::IShaderManager
			{
			public:
				void initialize(VulkanDevice* device);
				void deinitialize();

				rhi::ShaderHandle createShader(const rhi::ShaderDesc& desc) override;

				const VulkanShader& shader(rhi::ShaderHandle handle) const { return shaders_[handle]; }

			private:
				std::vector<VulkanShader> shaders_;

				VulkanDevice* device_ = nullptr;
			};

			class VulkanBindGroupLayout
			{
			public:
				void initialize(VkDevice device, const rhi::BindGroupLayoutDesc& desc);
				void deinitialize(VkDevice device);

				VkDescriptorSetLayout layout() const { return layout_; }

				// Sets allocated from this layout must come from an update-after-bind pool.
				bool updateAfterBind() const { return updateAfterBind_; }

			private:
				VkDescriptorSetLayout layout_;

				bool updateAfterBind_ = false;
			};

			class VulkanBindGroupLayoutManager : public rhi::IBindGroupLayoutManager
			{
			public:
				void initialize(VulkanDevice* device);
				void deinitialize();

				rhi::BindGroupLayoutHandle createBindGroupLayout(const rhi::BindGroupLayoutDesc& desc) override;

				const VulkanBindGroupLayout& layout(rhi::BindGroupLayoutHandle handle) const { return layouts_[handle]; }

			private:
				std::vector<VulkanBindGroupLayout> layouts_;

				VulkanDevice* device_ = nullptr;
			};

			class VulkanPipelineLayout
			{
			public:
				void initialize(VkDevice device, const std::vector<VkDescriptorSetLayout>& layouts, u32 pushConstantSize);
				void deinitialize(VkDevice device);

				VkPipelineLayout layout() const { return layout_; }

			private:
				VkPipelineLayout layout_;
			};

			class VulkanPipeline
			{
			public:
				void initialize(VkDevice device, const VkGraphicsPipelineCreateInfo& ci);
				void deinitialize(VkDevice device);

				VkPipeline pipeline() const { return pipeline_; }
				VkPipelineCache cache() const { return cache_; }

			private:
				VkPipeline pipeline_ = VK_NULL_HANDLE;
				VkPipelineCache cache_ = VK_NULL_HANDLE;
			};

			class VulkanPipelineManager : public rhi::IPipelineManager
			{
			public:
				void initialize(VulkanDevice* device, VulkanShaderManager* shaderManager, VulkanBindGroupLayoutManager* layoutManager);
				void deinitialize();

				rhi::PipelineLayoutHandle createPipelineLayout(const rhi::PipelineLayoutDesc& desc) override;
				rhi::PipelineHandle createPipeline(const rhi::GraphicsPipelineDesc& desc) override;

				const VulkanPipelineLayout& layout(rhi::PipelineLayoutHandle handle) const { return layouts_[handle]; }
				const VulkanPipeline& pipeline(rhi::PipelineHandle handle) const { return pipelines_[handle]; }

			private:
				std::vector<VulkanPipelineLayout> layouts_;
				std::vector<VulkanPipeline> pipelines_;

				VulkanDevice* device_ = nullptr;
				VulkanShaderManager* shaderManager_ = nullptr;
				VulkanBindGroupLayoutManager* layoutManager_ = nullptr;
			};
		}
	}
}

#endif