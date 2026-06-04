#ifndef _MV_VULKAN_PIPELINE_H_
#define _MV_VULKAN_PIPELINE_H_

#include "vulkan/vulkan.h"

#include "util/types.h"

#include "rhi/pipeline.h"

namespace mv
{
	namespace backend
	{
		using namespace types;

		class VulkanDevice;

		class VulkanShader
		{
		public:
			void initialize(VkDevice device, const rhi::ShaderDesc& desc);
			void deinitialize(VkDevice device);

			VkShaderModule module() const { return module_; }
			rhi::EShaderType type() const { return type_; }


		private:
			VkShaderModule module_;
			rhi::EShaderType type_;

			
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

		private:
			VkDescriptorSetLayout layout_;
		};

		class VulkanBindGroupLayoutManager
		{
		public:
			void initialize(VulkanDevice* device);
			void deinitialize();

			rhi::BindGroupLayoutHandle createBindGroupLayout(const rhi::BindGroupLayoutDesc& desc);

			const VulkanBindGroupLayout& layout(rhi::BindGroupLayoutHandle handle) const { return layouts_[handle]; }

		private:
			std::vector<VulkanBindGroupLayout> layouts_;

			VulkanDevice* device_ = nullptr;
		};

		class VulkanPipelineLayout
		{
		public:
			void initialize(VkDevice device, const std::vector<VkDescriptorSetLayout>& layouts);
			void deinitialize(VkDevice device);

			VkPipelineLayout layout() { return layout_; }

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

#endif