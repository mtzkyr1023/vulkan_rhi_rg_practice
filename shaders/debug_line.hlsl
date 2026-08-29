// Debug lines: Bullet's collision wireframes, drawn as a line list straight from a
// CPU-written vertex buffer. Depth-tested against the scene so the shapes sit in the
// world rather than floating over it, but not depth-writing -- debug ink should never
// occlude anything real.

#include "common.hlsli"

struct VSInput
{
	float3 position : POSITION;
	float3 color    : COLOR0;
};

struct VSOutput
{
	float4 position : SV_POSITION;
	float3 color    : COLOR0;
};

VSOutput VSMain(VSInput input)
{
	VSOutput output;

	output.position = mul(float4(input.position, 1.0f), viewProj);
	output.color = input.color;

	return output;
}

float4 PSMain(VSOutput input) : SV_Target0
{
	return float4(input.color, 1.0f);
}
