// Vertex data is generated from SV_VertexID so the sample needs no vertex buffer
// (there is no buffer upload path yet). Draw it with vertexCount = 3.

struct VSOutput
{
	float4 position : SV_POSITION;
	float3 color    : COLOR0;
};

VSOutput VSMain(uint vertexId : SV_VertexID)
{
	// Clip space here is D3D's (+Y up). The Vulkan backend flips its viewport so the same
	// coordinates land the same way up on both backends.
	const float2 positions[3] =
	{
		float2( 0.0f,  0.6f),   // apex,        red
		float2( 0.6f, -0.6f),   // bottom right, green
		float2(-0.6f, -0.6f),   // bottom left,  blue
	};

	const float3 colors[3] =
	{
		float3(1.0f, 0.0f, 0.0f),
		float3(0.0f, 1.0f, 0.0f),
		float3(0.0f, 0.0f, 1.0f),
	};

	VSOutput output;
	output.position = float4(positions[vertexId], 0.0f, 1.0f);
	output.color = colors[vertexId];

	return output;
}

float4 PSMain(VSOutput input) : SV_TARGET
{
	return float4(input.color, 1.0f);
}
