
#include "rhi/dx12_0/dx_pipeline.h"
#include "rhi/dx12_0/dx_device.h"

namespace mv::backend::dx12_0
{
	DXGI_FORMAT toDxgiFormat(rhi::ETextureFormat format);

	static DXGI_FORMAT toDxgiVertexFormat(rhi::EVertexFormat format)
	{
		switch (format)
		{
		case rhi::EVertexFormat::eFloat:            return DXGI_FORMAT_R32_FLOAT;
		case rhi::EVertexFormat::eFloat2:           return DXGI_FORMAT_R32G32_FLOAT;
		case rhi::EVertexFormat::eFloat3:           return DXGI_FORMAT_R32G32B32_FLOAT;
		case rhi::EVertexFormat::eUint:             return DXGI_FORMAT_R32_UINT;
		case rhi::EVertexFormat::eR8G8B8A8_UNORM:   return DXGI_FORMAT_R8G8B8A8_UNORM;
		case rhi::EVertexFormat::eFloat4:
		default:                                    return DXGI_FORMAT_R32G32B32A32_FLOAT;
		}
	}

	static D3D12_PRIMITIVE_TOPOLOGY_TYPE toDxTopologyType(rhi::EPrimitiveTopology topology)
	{
		switch (topology)
		{
		case rhi::EPrimitiveTopology::ePointList:  return D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
		case rhi::EPrimitiveTopology::eLineList:   return D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
		case rhi::EPrimitiveTopology::eTriangleList:
		case rhi::EPrimitiveTopology::eTriangleStrip:
		default:                                   return D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		}
	}

	static D3D12_PRIMITIVE_TOPOLOGY toDxTopology(rhi::EPrimitiveTopology topology)
	{
		switch (topology)
		{
		case rhi::EPrimitiveTopology::ePointList:      return D3D_PRIMITIVE_TOPOLOGY_POINTLIST;
		case rhi::EPrimitiveTopology::eLineList:       return D3D_PRIMITIVE_TOPOLOGY_LINELIST;
		case rhi::EPrimitiveTopology::eTriangleStrip:  return D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
		case rhi::EPrimitiveTopology::eTriangleList:
		default:                                       return D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		}
	}

	static D3D12_COMPARISON_FUNC toDxCompareOp(rhi::ECompareOp op)
	{
		switch (op)
		{
		case rhi::ECompareOp::eNever:         return D3D12_COMPARISON_FUNC_NEVER;
		case rhi::ECompareOp::eLess:          return D3D12_COMPARISON_FUNC_LESS;
		case rhi::ECompareOp::eEqual:         return D3D12_COMPARISON_FUNC_EQUAL;
		case rhi::ECompareOp::eLessEqual:     return D3D12_COMPARISON_FUNC_LESS_EQUAL;
		case rhi::ECompareOp::eGreater:       return D3D12_COMPARISON_FUNC_GREATER;
		case rhi::ECompareOp::eNotEqual:      return D3D12_COMPARISON_FUNC_NOT_EQUAL;
		case rhi::ECompareOp::eGreaterEqual:  return D3D12_COMPARISON_FUNC_GREATER_EQUAL;
		case rhi::ECompareOp::eAlways:
		default:                              return D3D12_COMPARISON_FUNC_ALWAYS;
		}
	}

	static D3D12_BLEND toDxBlendFactor(rhi::EBlendFactor factor)
	{
		switch (factor)
		{
		case rhi::EBlendFactor::eZero:               return D3D12_BLEND_ZERO;
		case rhi::EBlendFactor::eSrcColor:           return D3D12_BLEND_SRC_COLOR;
		case rhi::EBlendFactor::eOneMinusSrcColor:   return D3D12_BLEND_INV_SRC_COLOR;
		case rhi::EBlendFactor::eDstColor:           return D3D12_BLEND_DEST_COLOR;
		case rhi::EBlendFactor::eOneMinusDstColor:   return D3D12_BLEND_INV_DEST_COLOR;
		case rhi::EBlendFactor::eSrcAlpha:           return D3D12_BLEND_SRC_ALPHA;
		case rhi::EBlendFactor::eOneMinusSrcAlpha:   return D3D12_BLEND_INV_SRC_ALPHA;
		case rhi::EBlendFactor::eDstAlpha:           return D3D12_BLEND_DEST_ALPHA;
		case rhi::EBlendFactor::eOneMinusDstAlpha:   return D3D12_BLEND_INV_DEST_ALPHA;
		case rhi::EBlendFactor::eOne:
		default:                                     return D3D12_BLEND_ONE;
		}
	}

	static D3D12_BLEND_OP toDxBlendOp(rhi::EBlendOp op)
	{
		switch (op)
		{
		case rhi::EBlendOp::eSubtract:         return D3D12_BLEND_OP_SUBTRACT;
		case rhi::EBlendOp::eReverseSubtract:  return D3D12_BLEND_OP_REV_SUBTRACT;
		case rhi::EBlendOp::eMin:              return D3D12_BLEND_OP_MIN;
		case rhi::EBlendOp::eMax:              return D3D12_BLEND_OP_MAX;
		case rhi::EBlendOp::eAdd:
		default:                               return D3D12_BLEND_OP_ADD;
		}
	}

	static UINT8 toDxColorComponents(rhi::EColorComponent mask)
	{
		UINT8 flags = 0;
		if ((mask & rhi::EColorComponent::eR) == rhi::EColorComponent::eR) flags |= D3D12_COLOR_WRITE_ENABLE_RED;
		if ((mask & rhi::EColorComponent::eG) == rhi::EColorComponent::eG) flags |= D3D12_COLOR_WRITE_ENABLE_GREEN;
		if ((mask & rhi::EColorComponent::eB) == rhi::EColorComponent::eB) flags |= D3D12_COLOR_WRITE_ENABLE_BLUE;
		if ((mask & rhi::EColorComponent::eA) == rhi::EColorComponent::eA) flags |= D3D12_COLOR_WRITE_ENABLE_ALPHA;
		return flags;
	}

	static D3D12_SHADER_VISIBILITY toDxShaderVisibility(rhi::EShaderStage stages)
	{
		// A root parameter names one visible stage or ALL, so anything read from more than
		// one stage has to fall back to ALL.
		switch (stages)
		{
		case rhi::EShaderStage::eVertex:    return D3D12_SHADER_VISIBILITY_VERTEX;
		case rhi::EShaderStage::eFragment:  return D3D12_SHADER_VISIBILITY_PIXEL;
		case rhi::EShaderStage::eTask:      return D3D12_SHADER_VISIBILITY_AMPLIFICATION;
		case rhi::EShaderStage::eMesh:      return D3D12_SHADER_VISIBILITY_MESH;
		default:                            return D3D12_SHADER_VISIBILITY_ALL;
		}
	}

	static D3D12_DESCRIPTOR_RANGE_TYPE toDxRangeType(rhi::EDescriptorType type)
	{
		switch (type)
		{
		case rhi::EDescriptorType::eUniformBuffer:  return D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
		// Read-only structured buffer, so an SRV rather than a UAV.
		case rhi::EDescriptorType::eStorageBuffer:           return D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		case rhi::EDescriptorType::eStorageBufferReadWrite:  return D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
		case rhi::EDescriptorType::eSampler:        return D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
		case rhi::EDescriptorType::eSampledImage:
		default:                                    return D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		}
	}



	void DxShader::initialize(const rhi::ShaderDesc& desc)
	{
		bytecode_.assign(
			reinterpret_cast<const u8*>(desc.bytecode),
			reinterpret_cast<const u8*>(desc.bytecode) + desc.bytecodeSize);

		type_ = desc.stage;
	}

	void DxShader::deinitialize()
	{
		bytecode_.clear();
	}



	void DxShaderManager::initialize(DxDevice* device)
	{
		device_ = device;
	}

	void DxShaderManager::deinitialize()
	{
		for (auto& shader : shaders_)
		{
			shader.deinitialize();
		}
		shaders_.clear();
	}

	rhi::ShaderHandle DxShaderManager::createShader(const rhi::ShaderDesc& desc)
	{
		DxShader shader;
		shader.initialize(desc);

		rhi::ShaderHandle handle = (rhi::ShaderHandle)shaders_.size();
		shaders_.push_back(std::move(shader));

		return handle;
	}



	void DxBindGroupLayout::initialize(const rhi::BindGroupLayoutDesc& desc)
	{
		bool firstStage = true;

		for (const auto& item : desc.bindings)
		{
			D3D12_DESCRIPTOR_RANGE range{};
			range.RangeType = toDxRangeType(item.type);
			// -1 means unbounded: the shader may index as far as the heap allows, and the
			// range must be the last one in its table.
			range.NumDescriptors = item.bindless ? UINT_MAX : item.count;
			range.BaseShaderRegister = item.binding;
			// RegisterSpace stays 0 here; the pipeline layout rewrites it to the group index.
			range.RegisterSpace = 0;
			range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

			if (range.RangeType == D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER)
			{
				samplerRanges_.push_back(range);
				samplerBindings_.push_back(item);
			}
			else
			{
				viewRanges_.push_back(range);
				viewBindings_.push_back(item);
			}

			const D3D12_SHADER_VISIBILITY stageVisibility = toDxShaderVisibility(item.stages);
			if (firstStage)
			{
				visibility_ = stageVisibility;
				firstStage = false;
			}
			else if (visibility_ != stageVisibility)
			{
				// Bindings in this group are read from more than one stage.
				visibility_ = D3D12_SHADER_VISIBILITY_ALL;
			}
		}
	}

	namespace
	{
		u32 slotOffsetIn(const std::vector<rhi::BindingDesc>& bindings, u32 binding)
		{
			u32 offset = 0;
			for (const auto& item : bindings)
			{
				if (item.binding == binding)
					return offset;

				offset += item.count;
			}

			return offset;
		}

		u32 slotCountIn(const std::vector<rhi::BindingDesc>& bindings)
		{
			u32 total = 0;
			for (const auto& item : bindings)
			{
				total += item.count;
			}

			return total;
		}
	}

	u32 DxBindGroupLayout::viewSlotOffset(u32 binding) const { return slotOffsetIn(viewBindings_, binding); }
	u32 DxBindGroupLayout::samplerSlotOffset(u32 binding) const { return slotOffsetIn(samplerBindings_, binding); }

	u32 DxBindGroupLayout::viewSlotCount() const { return slotCountIn(viewBindings_); }
	u32 DxBindGroupLayout::samplerSlotCount() const { return slotCountIn(samplerBindings_); }

	void DxBindGroupLayout::deinitialize()
	{
		viewRanges_.clear();
		samplerRanges_.clear();
		viewBindings_.clear();
		samplerBindings_.clear();
	}



	void DxBindGroupLayoutManager::initialize(DxDevice* device)
	{
		device_ = device;
	}

	void DxBindGroupLayoutManager::deinitialize()
	{
		for (auto& layout : layouts_)
		{
			layout.deinitialize();
		}
		layouts_.clear();
	}

	rhi::BindGroupLayoutHandle DxBindGroupLayoutManager::createBindGroupLayout(const rhi::BindGroupLayoutDesc& desc)
	{
		DxBindGroupLayout layout;
		layout.initialize(desc);

		rhi::BindGroupLayoutHandle handle = (rhi::BindGroupLayoutHandle)layouts_.size();
		layouts_.push_back(std::move(layout));

		return handle;
	}



	void DxPipelineLayout::initialize(
		DxDevice* device,
		const std::vector<D3D12_ROOT_PARAMETER>& parameters,
		const std::vector<GroupRootSlots>& groupSlots,
		u32 pushConstantSlot)
	{
		groupSlots_ = groupSlots;
		pushConstantSlot_ = pushConstantSlot;

		D3D12_ROOT_SIGNATURE_DESC desc{};
		desc.NumParameters = (UINT)parameters.size();
		desc.pParameters = parameters.data();
		desc.NumStaticSamplers = 0;
		desc.pStaticSamplers = nullptr;
		desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

		wrl::ComPtr<ID3DBlob> blob;
		wrl::ComPtr<ID3DBlob> error;
		if (FAILED(D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1_0, &blob, &error)))
		{
			if (error)
			{
				OutputDebugStringA((const char*)error->GetBufferPointer());
				OutputDebugStringA("\n");
			}
			throw std::exception("Failed to serialize root signature");
		}

		if (FAILED(device->device()->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&rootSignature_))))
		{
			throw std::exception("Failed to create root signature");
		}
	}

	void DxPipelineLayout::deinitialize()
	{
		rootSignature_.Reset();
		groupSlots_.clear();
	}



	void DxPipeline::initialize(DxDevice* device, const D3D12_GRAPHICS_PIPELINE_STATE_DESC& desc)
	{
		if (FAILED(device->device()->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&pipelineState_))))
		{
			throw std::exception("Failed to create graphics pipeline state");
		}

		rootSignature_ = desc.pRootSignature;
	}

	void DxPipeline::deinitialize()
	{
		pipelineState_.Reset();
		rootSignature_.Reset();
	}



	void DxPipelineManager::initialize(DxDevice* device, DxShaderManager* shaderManager, DxBindGroupLayoutManager* layoutManager)
	{
		device_ = device;
		shaderManager_ = shaderManager;
		layoutManager_ = layoutManager;
	}

	void DxPipelineManager::deinitialize()
	{
		for (auto& layout : layouts_)
		{
			layout.deinitialize();
		}
		layouts_.clear();

		for (auto& pipeline : pipelines_)
		{
			pipeline.deinitialize();
		}
		pipelines_.clear();
	}

	rhi::PipelineLayoutHandle DxPipelineManager::createPipelineLayout(const rhi::PipelineLayoutDesc& desc)
	{
		std::vector<D3D12_ROOT_PARAMETER> parameters;

		// The root parameters point at these ranges, so the storage must outlive
		// serialization and must not reallocate. A bind group can contribute at most two
		// entries (a view table and a sampler table).
		std::vector<std::vector<D3D12_DESCRIPTOR_RANGE>> rangeStorage;
		rangeStorage.reserve(desc.bindGroups.size() * 2);

		std::vector<DxPipelineLayout::GroupRootSlots> groupSlots;

		// Root constants go first so their slot is stable; the descriptor tables below then
		// number themselves from wherever the parameter list has reached.
		u32 pushConstantSlot = DxPipelineLayout::kNoPushConstants;
		if (desc.pushConstantSize > 0)
		{
			D3D12_ROOT_PARAMETER parameter{};
			parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
			parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
			parameter.Constants.ShaderRegister = 0;
			// space0 is taken by the first bind group's b0, so root constants live in a
			// space of their own rather than colliding with it.
			parameter.Constants.RegisterSpace = rhi::kPushConstantRegisterSpace;
			parameter.Constants.Num32BitValues = desc.pushConstantSize / 4;

			pushConstantSlot = (u32)parameters.size();
			parameters.push_back(parameter);
		}

		for (u32 space = 0; space < (u32)desc.bindGroups.size(); space++)
		{
			const DxBindGroupLayout& layout = layoutManager_->layout(desc.bindGroups[space]);

			DxPipelineLayout::GroupRootSlots slots{};

			auto addTable = [&](const std::vector<D3D12_DESCRIPTOR_RANGE>& ranges) -> u32
			{
				if (ranges.empty()) return DxPipelineLayout::GroupRootSlots::kNone;

				// A Vulkan descriptor set index maps onto an HLSL register space.
				rangeStorage.push_back(ranges);
				for (auto& range : rangeStorage.back())
				{
					range.RegisterSpace = space;
				}

				D3D12_ROOT_PARAMETER parameter{};
				parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
				parameter.ShaderVisibility = layout.visibility();
				parameter.DescriptorTable.NumDescriptorRanges = (UINT)rangeStorage.back().size();
				parameter.DescriptorTable.pDescriptorRanges = rangeStorage.back().data();

				parameters.push_back(parameter);

				return (u32)parameters.size() - 1;
			};

			slots.viewTable = addTable(layout.viewRanges());
			slots.samplerTable = addTable(layout.samplerRanges());

			groupSlots.push_back(slots);
		}

		DxPipelineLayout layout;
		layout.initialize(device_, parameters, groupSlots, pushConstantSlot);

		rhi::PipelineLayoutHandle handle = (rhi::PipelineLayoutHandle)layouts_.size();
		layouts_.push_back(std::move(layout));

		return handle;
	}

	rhi::PipelineHandle DxPipelineManager::createPipeline(const rhi::GraphicsPipelineDesc& desc)
	{
		std::vector<D3D12_INPUT_ELEMENT_DESC> inputElements;
		for (const auto& item : desc.vertexLayout.attributes)
		{
			const rhi::VertexBinding* binding = nullptr;
			for (const auto& b : desc.vertexLayout.bindings)
			{
				if (b.binding == item.binding)
				{
					binding = &b;
					break;
				}
			}

			const bool perInstance = binding && binding->perInstance;

			D3D12_INPUT_ELEMENT_DESC element{};
			element.SemanticName = item.semanticName;
			element.SemanticIndex = item.semanticIndex;
			element.Format = toDxgiVertexFormat(item.format);
			element.InputSlot = item.binding;
			element.AlignedByteOffset = item.offset;
			element.InputSlotClass = perInstance ? D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA : D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
			element.InstanceDataStepRate = perInstance ? 1 : 0;

			inputElements.push_back(element);
		}

		D3D12_GRAPHICS_PIPELINE_STATE_DESC ci{};
		ci.pRootSignature = layouts_[desc.layoutHandle].rootSignature();

		if (desc.vs != INVALID_HANDLE) ci.VS = shaderManager_->shader(desc.vs).bytecode();
		if (desc.ps != INVALID_HANDLE) ci.PS = shaderManager_->shader(desc.ps).bytecode();

		ci.InputLayout.pInputElementDescs = inputElements.data();
		ci.InputLayout.NumElements = (UINT)inputElements.size();

		ci.RasterizerState.FillMode = (desc.rasterizer.polygonMode == rhi::EPolygonMode::eWireframe) ? D3D12_FILL_MODE_WIREFRAME : D3D12_FILL_MODE_SOLID;
		switch (desc.rasterizer.cullMode)
		{
		case rhi::ECullMode::eNone:   ci.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;  break;
		case rhi::ECullMode::eFront:  ci.RasterizerState.CullMode = D3D12_CULL_MODE_FRONT; break;
		default:                      ci.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;  break;
		}
		// Vulkan's counter-clockwise front face is D3D12's FrontCounterClockwise = TRUE.
		ci.RasterizerState.FrontCounterClockwise = (desc.rasterizer.frontFace == rhi::EFrontFace::eCounterClockwise) ? TRUE : FALSE;
		ci.RasterizerState.DepthClipEnable = desc.rasterizer.depthClampEnable ? FALSE : TRUE;
		ci.RasterizerState.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
		ci.RasterizerState.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
		ci.RasterizerState.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
		ci.RasterizerState.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;

		ci.DepthStencilState.DepthEnable = desc.depth.depthTestEnable ? TRUE : FALSE;
		ci.DepthStencilState.DepthWriteMask = desc.depth.depthWriteEnable ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
		ci.DepthStencilState.DepthFunc = toDxCompareOp(desc.depth.depthCompareOp);
		ci.DepthStencilState.StencilEnable = FALSE;

		D3D12_RENDER_TARGET_BLEND_DESC blend{};
		blend.BlendEnable = desc.blend.blendEnable ? TRUE : FALSE;
		blend.LogicOpEnable = FALSE;
		blend.SrcBlend = toDxBlendFactor(desc.blend.srcColorFactor);
		blend.DestBlend = toDxBlendFactor(desc.blend.dstColorFactor);
		blend.BlendOp = toDxBlendOp(desc.blend.colorOp);
		blend.SrcBlendAlpha = toDxBlendFactor(desc.blend.srcAlphaFactor);
		blend.DestBlendAlpha = toDxBlendFactor(desc.blend.dstAlphaFactor);
		blend.BlendOpAlpha = toDxBlendOp(desc.blend.alphaOp);
		blend.LogicOp = D3D12_LOGIC_OP_NOOP;
		blend.RenderTargetWriteMask = toDxColorComponents(desc.blend.writeMask);

		ci.BlendState.AlphaToCoverageEnable = FALSE;
		ci.BlendState.IndependentBlendEnable = FALSE;
		for (u32 i = 0; i < D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT; i++)
		{
			ci.BlendState.RenderTarget[i] = blend;
		}

		ci.SampleMask = UINT_MAX;
		ci.SampleDesc.Count = 1;
		ci.SampleDesc.Quality = 0;

		ci.PrimitiveTopologyType = toDxTopologyType(desc.topology);

		ci.NumRenderTargets = (UINT)desc.colorFormats.size();
		for (u32 i = 0; i < (u32)desc.colorFormats.size() && i < D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT; i++)
		{
			ci.RTVFormats[i] = toDxgiFormat(desc.colorFormats[i]);
		}
		ci.DSVFormat = toDxgiFormat(desc.depthFormat);

		DxPipeline pipeline;
		pipeline.initialize(device_, ci);
		pipeline.setTopology(toDxTopology(desc.topology));

		rhi::PipelineHandle handle = (rhi::PipelineHandle)pipelines_.size();
		pipelines_.push_back(std::move(pipeline));

		return handle;
	}
}
