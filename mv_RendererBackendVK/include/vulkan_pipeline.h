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

		class VulkanShader
		{
		public:
			void initialize(VkDevice device, const rhi::ShaderDesc& desc);
			void deinitialize(VkDevice device);

			VkShaderModule module() { return module_; }
			rhi::EShaderType type() { return type_; }


		private:
			VkShaderModule module_;
			rhi::EShaderType type_;
		};

		class VulkanShaderManager : public rhi::IShaderManager
		{
		public:
			void initialize(VkDevice device);
			void deinitialize();

			rhi::ShaderHandle createShader(const rhi::ShaderDesc& desc) override;

			const VulkanShader& shader(rhi::ShaderHandle handle) { return shaders_[handle]; }

		private:
			std::vector<VulkanShader> shaders_;
			rhi::ShaderHandle nextHandleIndex_ = 0;

			VkDevice device_;
		};

		class VulkanBindGroupLayout
		{
		public:


			VkDescriptorSetLayout layout() { return layout_; }

		private:
			VkDescriptorSetLayout layout_;
		};

		class VulkanBindGroupLayoutManager
		{
		public:
			void initialize();
			void deinitialize();
		};

		class VulkanPipelineLayout
		{
		public:
			void initialize(VkDevice device, const rhi::PipelineLayoutDesc& desc);
			void deinitialize(VkDevice device);

			VkPipelineLayout layout() { return layout_; }

		private:
			VkPipelineLayout layout_;
		};

		class VulkanPipeline
		{
		public:
			void initialize(VkDevice device, const rhi::GraphicsPipelineDesc& desc);
			void deinitialize(VkDevice device);

			VkPipeline pipeline() { return pipeline_; }
			VkPipelineCache cache() { return cache_; }

		private:
			VkPipeline pipeline_ = VK_NULL_HANDLE;
			VkPipelineCache cache_ = VK_NULL_HANDLE;
		};

		class VulkanPipelineManager : public rhi::IPipelineManager
		{
		public:
			void initialize(VkDevice device);
			void deinitialize();

			rhi::PipelineLayoutHandle createPipelineLayout(const rhi::PipelineLayoutDesc& desc) override;
			rhi::PipelineHandle createPipeline(const rhi::GraphicsPipelineDesc& desc) override;

		private:
			std::vector<VulkanPipelineLayout> layouts_;
			std::vector<VulkanPipeline> pipelines_;

			VkDevice device_;
		};
	}
}

#endif