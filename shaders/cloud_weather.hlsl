// The weather map: one texel per few dozen metres of ground, tiling across the world.
//
//   r  coverage, how much of the sky this column has cloud in
//   g  cloud type, 0 stratus through 1 cumulus, which selects the altitude profile
//   b  the top of the layer this column reaches, as a fraction
//
// Two dimensional on purpose. Weather varies over kilometres horizontally and the vertical
// structure is a function of type and altitude, so spending a third dimension on it would
// be spending it on something the altitude gradients already describe.

#define MV_CUSTOM_PUSH_CONSTANTS
#include "common.hlsli"
#include "noise.hlsli"

// Remap onto [0, 1], clamped. The one operation the whole cloud model is built out of.
float mvRemap01(float value, float low, float high)
{
	return saturate((value - low) / max(high - low, 1e-5f));
}

struct WeatherConstants
{
	uint size;
	uint seed;

	// Raises or lowers coverage everywhere. The one knob that reads as "how cloudy".
	float coverage;

	float _weatherPad;
};

#ifdef MV_TARGET_VULKAN
[[vk::push_constant]] WeatherConstants weather;
#else
ConstantBuffer<WeatherConstants> weather : register(b0, space9);
#endif

#ifdef MV_TARGET_VULKAN
[[vk::image_format("rgba8")]] RWTexture2D<float4> target : register(u0, space0);
#else
RWTexture2D<float4> target : register(u0, space0);
#endif

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
	if (id.x >= weather.size || id.y >= weather.size)
		return;

	const float u = (float(id.x) + 0.5f) / float(weather.size);
	const float v = (float(id.y) + 0.5f) / float(weather.size);

	NoiseParams p;
	p.basis = MV_NOISE_BASIS_PERLIN;
	p.fractal = MV_NOISE_FRACTAL_FBM;
	p.frequency = 4.0f;
	p.octaves = 5;
	p.lacunarity = 2.0f;
	p.gain = 0.5f;
	p.seed = weather.seed;
	p.warpStrength = 0.0f;
	p.warpFrequency = 2.0f;

	// Tileable, and it has to be: the map repeats across the whole world and a seam would
	// be a straight edge of cloud running to the horizon.
	p.tileable = 1;
	p._noisePad = uint2(0, 0);

	const float base = mvNoiseSample(p, u, v) * 0.5f + 0.5f;

	p.seed = weather.seed + 0x27d4eb2du;
	p.frequency = 9.0f;
	const float variation = mvNoiseSample(p, u, v) * 0.5f + 0.5f;

	p.seed = weather.seed + 0x165667b1u;
	p.frequency = 2.0f;
	p.octaves = 3;
	const float typeField = mvNoiseSample(p, u, v) * 0.5f + 0.5f;

	// Coverage has to span [0, 1] across the map, because the march gates on it a second
	// time: density has to exceed 1 - coverage to survive. Two thresholds compounding is
	// what makes a naive subtraction here produce an empty sky -- fBm is bell-shaped around
	// 0.5 and almost never reaches the ends, so subtracting a constant leaves values that
	// the second gate then rejects entirely.
	//
	// Remapping instead stretches whatever range the noise actually occupies onto the full
	// one, and slides that window with the control: at 0 the sky is clear, at 1 overcast.
	const float field = base * 0.6f + variation * 0.4f;

	const float low = 1.0f - weather.coverage;
	const float high = min(1.0f, 1.4f - weather.coverage);

	const float coverage = mvRemap01(field, low, high);

	target[id.xy] = float4(coverage, typeField, saturate(0.4f + variation * 0.6f), 1.0f);
}
