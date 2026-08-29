// Per-frame deformation of the sculpt density: the base grid (the player's
// carved shape) is resampled through a vertical wave displacement into a second
// buffer, and the marching cubes dispatch marches that instead. The base is
// never touched -- the waves ride on top of whatever has been carved, and stop
// leaving it exactly as it was.

// Must match SculptDeformGpuConstants in sculpt_gpu.cpp.
struct SculptDeformConstants
{
	float3 origin;
	float cellSize;

	uint corners;
	float time;
	float amplitude;
	float wavelength;

	float speed;
	float3 _pad;
};

#ifdef MV_TARGET_VULKAN
[[vk::push_constant]] SculptDeformConstants deform;
#else
ConstantBuffer<SculptDeformConstants> deform : register(b0, space9);
#endif

StructuredBuffer<float> baseDensity : register(t0, space0);
RWStructuredBuffer<float> animatedDensity : register(u1, space0);

float baseAt(int x, int y, int z)
{
	const int corners = (int)deform.corners;

	x = clamp(x, 0, corners - 1);
	y = clamp(y, 0, corners - 1);
	z = clamp(z, 0, corners - 1);

	return baseDensity[(z * corners + y) * corners + x];
}

[numthreads(4, 4, 4)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
	if (id.x >= deform.corners || id.y >= deform.corners || id.z >= deform.corners)
		return;

	const float worldX = deform.origin.x + (float)id.x * deform.cellSize;
	const float worldZ = deform.origin.z + (float)id.z * deform.cellSize;

	// Two octaves at odd angles: one sine reads as a machine, two read as water.
	const float k = 6.2831853f / max(deform.wavelength, 0.5f);
	const float t = deform.time * deform.speed;

	const float wave = deform.amplitude * (
		0.7f * sin(k * (worldX + 0.7f * worldZ) + t) +
		0.3f * sin(k * 2.3f * (0.6f * worldX - worldZ) + 1.7f * t));

	// A vertical displacement is a vertical resample: the value that used to live
	// at y - wave now lives here, so the whole surface rises and falls by it.
	const float sampleY = (float)id.y - wave / deform.cellSize;

	const int lowY = (int)floor(sampleY);
	const float blend = sampleY - (float)lowY;

	const float value = lerp(
		baseAt((int)id.x, lowY, (int)id.z),
		baseAt((int)id.x, lowY + 1, (int)id.z),
		blend);

	animatedDensity[(id.z * deform.corners + id.y) * deform.corners + id.x] = value;
}
