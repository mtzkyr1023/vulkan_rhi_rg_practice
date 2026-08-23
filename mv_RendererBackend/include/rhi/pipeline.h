#ifndef _MV_PIPELINE_H_
#define _MV_PIPELINE_H_

#include <vector>

#include "util/types.h"

#include "rhi/resource.h"

namespace mv
{
	namespace rhi
	{
		using namespace types;

		// Vulkan has push constants as a distinct concept; D3D12 expresses the same thing as
		// root constants, which still occupy a b-register. They are put in a space of their
		// own so they cannot collide with a bind group's b0, and HLSL has to declare them
		// there for the DXIL build. Part of the shader contract, hence a shared constant.
		constexpr u32 kPushConstantRegisterSpace = 9;

		using ShaderHandle = u32;
		using BindGroupLayoutHandle = u32;
		using BindGroupHandle = u32;
		using PipelineHandle = u32;
		using PipelineLayoutHandle = u32;

		enum class EShaderType
		{
			eVertex = 0,
			eFragment,

			eCompute,

			eTask,
			eMesh,
		};

		// A binding can be read from more than one stage, so bindings use these flags rather
		// than EShaderType, which names the single stage a shader module is compiled for.
		enum class EShaderStage : u8
		{
			eVertex = (1 << 0),
			eFragment = (1 << 1),
			eCompute = (1 << 2),
			eTask = (1 << 3),
			eMesh = (1 << 4),

			eAll = eVertex | eFragment | eCompute | eTask | eMesh,
		};

		enum class EDescriptorType
		{
			eUniformBuffer = 0,
			eStorageBuffer,

			// Shader-writable structured buffer. Vulkan uses the same descriptor type as the
			// read-only one; D3D12 needs a UAV rather than an SRV.
			eStorageBufferReadWrite,

			eSampledImage,
			eSampler,
		};

		enum class EPrimitiveTopology
		{
			ePointList = 0,
			eLineList,
			eTriangleList,
			eTriangleStrip,
		};

		enum class EPolygonMode
		{
			eFill = 0,
			eWireframe,
		};

		enum class ECullMode
		{
			eNone = 0,
			eFront,
			eBack,
		};

		enum class EFrontFace
		{
			eCounterClockwise = 0,
			eClockwise,
		};

		enum class ECompareOp
		{
			eNever = 0,
			eLess,
			eEqual,
			eLessEqual,
			eGreater,
			eNotEqual,
			eGreaterEqual,
			eAlways,
		};

		enum class EBlendFactor
		{
			eZero = 0,
			eOne,
			eSrcColor,
			eOneMinusSrcColor,
			eDstColor,
			eOneMinusDstColor,
			eSrcAlpha,
			eOneMinusSrcAlpha,
			eDstAlpha,
			eOneMinusDstAlpha,
		};

		enum class EBlendOp
		{
			eAdd = 0,
			eSubtract,
			eReverseSubtract,
			eMin,
			eMax,
		};

		enum class EColorComponent : u8
		{
			eR = (1 << 0),
			eG = (1 << 1),
			eB = (1 << 2),
			eA = (1 << 3),

			eAll = eR | eG | eB | eA,
		};

		enum class EVertexFormat
		{
			eFloat = 0,
			eFloat2,
			eFloat3,
			eFloat4,

			eUint,
			eR8G8B8A8_UNORM,
		};

		struct BlendState
		{
			bool blendEnable = false;

			EBlendFactor srcColorFactor = EBlendFactor::eOne;
			EBlendFactor dstColorFactor = EBlendFactor::eZero;
			EBlendOp colorOp = EBlendOp::eAdd;

			EBlendFactor srcAlphaFactor = EBlendFactor::eOne;
			EBlendFactor dstAlphaFactor = EBlendFactor::eZero;
			EBlendOp alphaOp = EBlendOp::eAdd;

			EColorComponent writeMask = EColorComponent::eAll;
		};

		struct DepthStencilState
		{
			bool depthTestEnable = false;
			bool depthWriteEnable = false;

			ECompareOp depthCompareOp = ECompareOp::eAlways;
		};

		struct RasterizerState
		{
			EPolygonMode polygonMode = EPolygonMode::eFill;
			ECullMode cullMode = ECullMode::eBack;
			EFrontFace frontFace = EFrontFace::eCounterClockwise;

			bool depthClampEnable = false;
		};

		// Vulkan addresses vertex inputs by location, HLSL by semantic name + index,
		// so a cross-API attribute has to carry both.
		struct VertexAttribute
		{
			u32 location = 0;

			const char* semanticName = "TEXCOORD";
			u32 semanticIndex = 0;

			u32 binding = 0;
			EVertexFormat format = EVertexFormat::eFloat4;
			u32 offset = 0;
		};

		struct VertexBinding
		{
			u32 binding = 0;
			u32 stride = 0;

			bool perInstance = false;
		};

		struct VertexLayout
		{
			std::vector<VertexBinding> bindings;
			std::vector<VertexAttribute> attributes;
		};

		struct ShaderDesc
		{
			EShaderType stage = EShaderType::eVertex;

			const u32* bytecode = nullptr;
			u32 bytecodeSize = 0;

			// SPIR-V names its entry point, and DXC keeps the HLSL name rather than
			// renaming it to "main". D3D12 ignores this: DXIL binds its entry internally.
			const char* entryPoint = "main";
		};

		struct GraphicsPipelineDesc
		{
			ShaderHandle vs = INVALID_HANDLE;
			ShaderHandle ps = INVALID_HANDLE;

			PipelineLayoutHandle layoutHandle = INVALID_HANDLE;

			VertexLayout vertexLayout;

			EPrimitiveTopology topology = EPrimitiveTopology::eTriangleList;

			BlendState blend;
			DepthStencilState depth;
			RasterizerState rasterizer;

			// A D3D12 PSO cannot be created without its render target formats, and Vulkan
			// dynamic rendering needs the same information, so it belongs in the shared desc.
			std::vector<ETextureFormat> colorFormats;
			ETextureFormat depthFormat = ETextureFormat::eUndefined;
		};

		struct BindingDesc
		{
			u32 binding = 0;
			u32 count = 0;
			EDescriptorType type = EDescriptorType::eSampledImage;
			EShaderStage stages = EShaderStage::eAll;

			// An unbounded array indexed from the shader. D3D12 needs a high enough resource
			// binding tier for this; Vulkan needs descriptor indexing turned on. `count`
			// then means the largest index the array will hold, not how many are written.
			bool bindless = false;
		};

		struct BindGroupLayoutDesc
		{
			std::vector<BindingDesc> bindings;
		};

		struct PipelineLayoutDesc
		{
			std::vector<BindGroupLayoutHandle> bindGroups;

			// Vulkan push constants / D3D12 root constants: the only way to give a draw its
			// own small payload without a descriptor. Must be a multiple of 4.
			u32 pushConstantSize = 0;
		};

		enum class EFilterMode
		{
			eNearest = 0,
			eLinear,
		};

		enum class EAddressMode
		{
			eRepeat = 0,
			eClampToEdge,
		};

		struct SamplerDesc
		{
			EFilterMode filter = EFilterMode::eLinear;
			EAddressMode address = EAddressMode::eRepeat;

			// 1 disables anisotropic filtering. Only meaningful on a texture with mips.
			u32 maxAnisotropy = 1;
		};

		struct BufferBinding
		{
			u32 binding = 0;
			BufferHandle buffer = INVALID_HANDLE;

			u64 offset = 0;
			u64 range = 0;
		};

		// Read-only structured buffer: an SRV in D3D12, a storage buffer in Vulkan.
		struct StorageBufferBinding
		{
			u32 binding = 0;
			BufferHandle buffer = INVALID_HANDLE;

			u64 offset = 0;

			// D3D12 describes a structured buffer view by element, Vulkan by byte range.
			u32 stride = 0;
			u32 count = 0;
		};

		struct TextureBinding
		{
			u32 binding = 0;
			TextureHandle texture = INVALID_HANDLE;

			// Slot within an array binding.
			u32 arrayIndex = 0;

			// Restricts sampling to a subrange of the mip chain, which is how a partially
			// resident texture hides the levels that have not streamed in yet. mipCount 0
			// means "everything from baseMip down".
			u32 baseMip = 0;
			u32 mipCount = 0;
		};

		struct SamplerBinding
		{
			u32 binding = 0;
			SamplerDesc sampler;

			u32 arrayIndex = 0;
		};

		struct BindGroupDesc
		{
			BindGroupLayoutHandle layout = INVALID_HANDLE;

			std::vector<BufferBinding> uniformBuffers;
			std::vector<StorageBufferBinding> storageBuffers;
			std::vector<TextureBinding> sampledTextures;
			std::vector<SamplerBinding> samplers;
		};

		class IShaderManager
		{
		public:
			virtual ~IShaderManager() {}
			virtual ShaderHandle createShader(const ShaderDesc& desc) = 0;
		};

		class IBindGroupLayoutManager
		{
		public:
			virtual ~IBindGroupLayoutManager() {}
			virtual BindGroupLayoutHandle createBindGroupLayout(const BindGroupLayoutDesc& desc) = 0;
		};

		class IPipelineManager
		{
		public:
			virtual ~IPipelineManager() {}

			virtual PipelineLayoutHandle createPipelineLayout(const PipelineLayoutDesc& desc) = 0;
			virtual PipelineHandle createPipeline(const GraphicsPipelineDesc& desc) = 0;
		};
	}

	namespace enum_concept
	{
		template<> struct has_and_or_operators<rhi::EColorComponent> : std::true_type {};
		template<> struct has_and_or_operators<rhi::EShaderStage> : std::true_type {};
	}
}

#endif
