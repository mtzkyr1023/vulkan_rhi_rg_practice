#ifndef _MV_PIPELINE_H_
#define _MV_PIPELINE_H_

#include "vector"

#include "util/types.h"

namespace mv
{
	namespace rhi
	{
		using namespace types;

		using ShaderHandle = u32;
		using BindGroupLayoutHandle = u32;
		using PipelineHandle = u32;
		using PipelineLayoutHandle = u32;

		enum class EShaderType
		{
			eVertex = 0,
			eGeometory,
			eFragment,

			eCompute,

			eTask,
			eMesh,
		};

		enum class EDescriptorType
		{
			eUniformBuffer = 0,
			eStorageBuffer,
			eSampledImage,
			eSampler,
		};

		struct BlendState
		{

		};

		struct DepthStencilState
		{

		};

		struct RasterizerState
		{

		};

		struct VertexLayout
		{

		};

		struct ShaderDesc
		{
			EShaderType stage = EShaderType::eVertex;

			const u32* bytecode = nullptr;
			u32 bytecodeSize = 0;
		};

		struct GraphicsPipelineDesc
		{
			ShaderHandle vs = INVALID_HANDLE;
			ShaderHandle ps = INVALID_HANDLE;

			PipelineLayoutHandle layoutHandle = INVALID_HANDLE;

			VertexLayout vertexLayout;

			BlendState blend;
			DepthStencilState depth;
			RasterizerState rasterizer;
		};

		struct BindingDesc
		{
			u32 binding = 0;
			EDescriptorType type = EDescriptorType::eSampledImage;
			EShaderType stageType = EShaderType::eVertex;
		};

		struct BindGroupLayoutDesc
		{
			std::vector<BindingDesc> bindings;
		};

		struct PipelineLayoutDesc
		{
			std::vector<BindGroupLayoutDesc> bindGroups;
		};

		class IShaderManager
		{
		public:
			virtual ~IShaderManager() {}
			virtual ShaderHandle createShader(const ShaderDesc& desc) = 0;
		};

		class IPipelineManager
		{
		public:
			virtual ~IPipelineManager() {}

			virtual PipelineLayoutHandle createPipelineLayout(const PipelineLayoutDesc& desc) = 0;
			virtual PipelineHandle createPipeline(const GraphicsPipelineDesc& desc) = 0;
		};
	}
}

#endif