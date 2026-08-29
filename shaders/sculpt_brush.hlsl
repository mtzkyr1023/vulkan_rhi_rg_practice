// The sculpt brush, on the GPU: one dispatch adds a smooth spherical bump to
// (or carves one out of) the density buffer in place. The CPU never touches the
// density on the hot path any more -- it keeps a mirror for Bullet, refreshed
// lazily after the stroke ends.

// Must match SculptBrushGpuConstants in sculpt_gpu.cpp.
struct SculptBrushConstants
{
	float3 center;
	float radius;

	float strength;
	uint corners;
	float2 _pad;
};

#ifdef MV_TARGET_VULKAN
[[vk::push_constant]] SculptBrushConstants brush;
#else
ConstantBuffer<SculptBrushConstants> brush : register(b0, space9);
#endif

RWStructuredBuffer<float> density : register(u0, space0);

[numthreads(4, 4, 4)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
	if (id.x >= brush.corners || id.y >= brush.corners || id.z >= brush.corners)
		return;

	// Everything in grid units; the CPU converted the world-space hit already.
	const float3 delta = float3(id) - brush.center;
	const float distance = length(delta);

	if (distance >= brush.radius)
		return;

	// The same squared falloff the CPU brush used: strokes sculpt, not punch.
	const float falloff = 1.0f - distance / brush.radius;

	density[(id.z * brush.corners + id.y) * brush.corners + id.x] +=
		brush.strength * falloff * falloff;
}
