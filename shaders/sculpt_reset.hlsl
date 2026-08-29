// Resets the sculpt mesh's indirect draw arguments before the marching cubes
// dispatch appends into them: zero vertices, one instance.

RWStructuredBuffer<float4> vertices : register(u2, space0);
RWStructuredBuffer<uint> drawArgs : register(u3, space0);

[numthreads(1, 1, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
	drawArgs[0] = 0;
	drawArgs[1] = 1;
	drawArgs[2] = 0;
	drawArgs[3] = 0;
}
