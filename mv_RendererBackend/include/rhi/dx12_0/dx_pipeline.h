
#ifndef _MV_DX_PIPELINE_H_
#define _MV_DX_PIPELINE_H_

#include <vector>

#include <d3d12.h>

#include <wrl/client.h>

#include "util/types.h"

#include "rhi/pipeline.h"

namespace mv
{
	namespace backend
	{
		namespace dx12_0
		{
			using namespace types;
			namespace wrl = Microsoft::WRL;

			class DxDevice;

			// D3D12 has no shader module object: the bytecode is handed to the PSO directly,
			// so the blob has to be owned here and kept alive for as long as it may be used.
			class DxShader
			{
			public:
				void initialize(const rhi::ShaderDesc& desc);
				void deinitialize();

				D3D12_SHADER_BYTECODE bytecode() const { return { bytecode_.data(), bytecode_.size() }; }
				rhi::EShaderType type() const { return type_; }

			private:
				std::vector<u8> bytecode_;
				rhi::EShaderType type_ = rhi::EShaderType::eVertex;
			};

			class DxShaderManager : public rhi::IShaderManager
			{
			public:
				void initialize(DxDevice* device);
				void deinitialize();

				rhi::ShaderHandle createShader(const rhi::ShaderDesc& desc) override;

				const DxShader& shader(rhi::ShaderHandle handle) const { return shaders_[handle]; }

			private:
				std::vector<DxShader> shaders_;

				DxDevice* device_ = nullptr;
			};

			// The closest D3D12 analogue of a descriptor set layout is a root parameter holding
			// a descriptor table. Samplers cannot share a table with CBV/SRV/UAV, so a bind group
			// may expand into two tables. The register space is not known until the group is
			// placed into a pipeline layout, so ranges are stored space-agnostic here.
			class DxBindGroupLayout
			{
			public:
				void initialize(const rhi::BindGroupLayoutDesc& desc);
				void deinitialize();

				const std::vector<D3D12_DESCRIPTOR_RANGE>& viewRanges() const { return viewRanges_; }
				const std::vector<D3D12_DESCRIPTOR_RANGE>& samplerRanges() const { return samplerRanges_; }

				// Ranges use OFFSET_APPEND, so a descriptor's slot in the table is decided by
				// declaration order, not by its register. Writing a bind group therefore needs
				// the ordered binding list, not just the ranges.
				const std::vector<rhi::BindingDesc>& viewBindings() const { return viewBindings_; }
				const std::vector<rhi::BindingDesc>& samplerBindings() const { return samplerBindings_; }

				// Descriptor offset of a binding within its table. Array and bindless
				// bindings occupy `count` slots, so this is not simply the binding index.
				u32 viewSlotOffset(u32 binding) const;
				u32 samplerSlotOffset(u32 binding) const;

				u32 viewSlotCount() const;
				u32 samplerSlotCount() const;

				D3D12_SHADER_VISIBILITY visibility() const { return visibility_; }

			private:
				std::vector<D3D12_DESCRIPTOR_RANGE> viewRanges_;
				std::vector<D3D12_DESCRIPTOR_RANGE> samplerRanges_;

				std::vector<rhi::BindingDesc> viewBindings_;
				std::vector<rhi::BindingDesc> samplerBindings_;

				D3D12_SHADER_VISIBILITY visibility_ = D3D12_SHADER_VISIBILITY_ALL;
			};

			class DxBindGroupLayoutManager : public rhi::IBindGroupLayoutManager
			{
			public:
				void initialize(DxDevice* device);
				void deinitialize();

				rhi::BindGroupLayoutHandle createBindGroupLayout(const rhi::BindGroupLayoutDesc& desc) override;

				const DxBindGroupLayout& layout(rhi::BindGroupLayoutHandle handle) const { return layouts_[handle]; }

			private:
				std::vector<DxBindGroupLayout> layouts_;

				DxDevice* device_ = nullptr;
			};

			class DxPipelineLayout
			{
			public:
				// Vulkan binds a descriptor set by its set index; D3D12 binds a table by its
				// root parameter index, and one set can expand into two tables. This records
				// that mapping so bindBindGroup can translate a set index back.
				struct GroupRootSlots
				{
					static constexpr u32 kNone = 0xFFFFFFFF;

					u32 viewTable = kNone;
					u32 samplerTable = kNone;
				};

				void initialize(
					DxDevice* device,
					const std::vector<D3D12_ROOT_PARAMETER>& parameters,
					const std::vector<GroupRootSlots>& groupSlots,
					u32 pushConstantSlot);

				void deinitialize();

				ID3D12RootSignature* rootSignature() const { return rootSignature_.Get(); }

				const GroupRootSlots& groupSlots(u32 setIndex) const { return groupSlots_[setIndex]; }

				// Root parameter index of the 32-bit constants, or kNoPushConstants.
				static constexpr u32 kNoPushConstants = 0xFFFFFFFF;
				u32 pushConstantSlot() const { return pushConstantSlot_; }

			private:
				wrl::ComPtr<ID3D12RootSignature> rootSignature_;

				std::vector<GroupRootSlots> groupSlots_;

				u32 pushConstantSlot_ = kNoPushConstants;
			};

			class DxPipeline
			{
			public:
				void initialize(DxDevice* device, const D3D12_GRAPHICS_PIPELINE_STATE_DESC& desc);
				void deinitialize();

				ID3D12PipelineState* pipelineState() const { return pipelineState_.Get(); }
				ID3D12RootSignature* rootSignature() const { return rootSignature_.Get(); }
				D3D12_PRIMITIVE_TOPOLOGY topology() const { return topology_; }

				void setTopology(D3D12_PRIMITIVE_TOPOLOGY topology) { topology_ = topology; }

			private:
				wrl::ComPtr<ID3D12PipelineState> pipelineState_;

				// A VkPipeline knows its own layout; a PSO does not hand its root signature
				// back, so it is kept here for binding.
				wrl::ComPtr<ID3D12RootSignature> rootSignature_;

				// The PSO only stores the topology *type* (triangle/line/point); the concrete
				// topology is command-list state, so it is carried alongside the pipeline.
				D3D12_PRIMITIVE_TOPOLOGY topology_ = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
			};

			class DxPipelineManager : public rhi::IPipelineManager
			{
			public:
				void initialize(DxDevice* device, DxShaderManager* shaderManager, DxBindGroupLayoutManager* layoutManager);
				void deinitialize();

				rhi::PipelineLayoutHandle createPipelineLayout(const rhi::PipelineLayoutDesc& desc) override;
				rhi::PipelineHandle createPipeline(const rhi::GraphicsPipelineDesc& desc) override;

				const DxPipelineLayout& layout(rhi::PipelineLayoutHandle handle) const { return layouts_[handle]; }
				const DxPipeline& pipeline(rhi::PipelineHandle handle) const { return pipelines_[handle]; }

			private:
				std::vector<DxPipelineLayout> layouts_;
				std::vector<DxPipeline> pipelines_;

				DxDevice* device_ = nullptr;
				DxShaderManager* shaderManager_ = nullptr;
				DxBindGroupLayoutManager* layoutManager_ = nullptr;
			};
		}
	}
}

#endif
