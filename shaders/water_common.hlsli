#ifndef _MV_WATER_COMMON_HLSLI_
#define _MV_WATER_COMMON_HLSLI_

// What the water pass and the water SSR pass share: the constants layout, the wave field
// and the view-ray reconstruction. Two shaders now evaluate the same surface -- the pixel
// shader to draw it and the reflection march to bounce off it -- and a reflection computed
// from waves even slightly different from the ones being drawn slides across the surface.
//
// Every function takes its inputs explicitly rather than reading a named global: both
// includers keep their own push-constant block, and this header does not care what they
// called it.

// Must match WaterGpuConstants in water_surface.cpp.
struct WaterConstants
{
	float3 cameraPosition;
	float  level;

	// Rebuilt from the basis rather than an inverse view-projection, as the clouds are.
	float3 cameraForward;
	float  tanHalfFov;

	// The direction light travels, and what colour.
	float3 lightDirection;
	float  time;

	// The sun as an intensity rather than a colour -- it was only ever fed a grey -- which
	// is what freed the slot next to it: how strongly the image-based terms apply. The
	// reflection off the cube and the light scattered back out of the body are both sky
	// light, and a surface lit by an IBL the rest of the scene has turned down cannot be
	// the one thing still shining.
	float  sunIntensity;
	float  iblIntensity;
	float  waveScale;
	float  _waterPad0;

	// What the water absorbs per metre of path, per channel. Red first, which is why deep
	// water is blue.
	float3 extinction;
	float  waveHeight;

	// The colour scattered back out of the body of the water.
	float3 scatterColor;
	float  roughness;

	float2 viewportSize;
	float  depthLinearA;
	float  depthLinearB;

	// Metres of path over which the shallow edge fades in, and how far the reflection is
	// allowed to be trusted.
	float  shoreFade;
	float  reflectionStrength;
	float  specularStrength;

	// How much of the screen-space reflection to lay over the cube one. Zero turns the
	// march off entirely.
	float  ssrStrength;
};

// The camera frame the ray reconstruction and the projection share. The same construction
// the skybox and the cloud march use, so all of them agree about what a pixel looks at.
void mvWaterBasis(float3 forward, out float3 right, out float3 up)
{
	right = normalize(cross(forward, float3(0.0f, 1.0f, 0.0f)));
	up = cross(right, forward);
}

// The view ray through a screen UV.
float3 mvWaterRay(float2 uv, float3 forward, float3 right, float3 up, float tanHalfFov, float aspect)
{
	const float2 ndc = float2(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f);

	return normalize(
		forward +
		right * (ndc.x * tanHalfFov * aspect) +
		up * (ndc.y * tanHalfFov));
}

// The other direction: where a world position lands on screen, and how deep it is along
// the camera axis. Returns uv in xy and the view depth in z; a negative depth means the
// point is behind the camera and the uv is meaningless.
float3 mvWaterProject(
	float3 position, float3 cameraPosition,
	float3 forward, float3 right, float3 up,
	float tanHalfFov, float aspect)
{
	const float3 d = position - cameraPosition;

	const float viewZ = dot(d, forward);

	const float2 ndc = float2(
		dot(d, right) / max(viewZ * tanHalfFov * aspect, 1e-6f),
		dot(d, up) / max(viewZ * tanHalfFov, 1e-6f));

	return float3(ndc.x * 0.5f + 0.5f, 0.5f - ndc.y * 0.5f, viewZ);
}

// Value noise on a lattice, and its analytic gradient. The waves are a sum of these rather
// than of sines: a sum of sines lines its crests up into a plaid whichever directions are
// picked, and the gradient is what the surface normal is made of, so it has to come out of
// the same function rather than be differenced from it.
float2 mvWaveHash(float2 cell)
{
	float3 p = float3(cell.xy, cell.x * 0.37f + cell.y * 0.71f);
	p = frac(p * float3(0.1031f, 0.1030f, 0.0973f));
	p += dot(p, p.yzx + 33.33f);

	return frac((p.xx + p.yz) * p.zy) * 2.0f - 1.0f;
}

// Gradient noise, returning the value in x and the two derivatives in yz.
float3 mvWaveNoise(float2 position)
{
	const float2 cell = floor(position);
	const float2 f = position - cell;

	// Quintic, so the second derivative is continuous too and the normals do not crease
	// along the lattice lines.
	const float2 u = f * f * f * (f * (f * 6.0f - 15.0f) + 10.0f);
	const float2 du = 30.0f * f * f * (f * (f - 2.0f) + 1.0f);

	const float2 g00 = mvWaveHash(cell + float2(0.0f, 0.0f));
	const float2 g10 = mvWaveHash(cell + float2(1.0f, 0.0f));
	const float2 g01 = mvWaveHash(cell + float2(0.0f, 1.0f));
	const float2 g11 = mvWaveHash(cell + float2(1.0f, 1.0f));

	const float v00 = dot(g00, f - float2(0.0f, 0.0f));
	const float v10 = dot(g10, f - float2(1.0f, 0.0f));
	const float v01 = dot(g01, f - float2(0.0f, 1.0f));
	const float v11 = dot(g11, f - float2(1.0f, 1.0f));

	const float value = v00 + u.x * (v10 - v00) + u.y * (v01 - v00) + u.x * u.y * (v00 - v10 - v01 + v11);

	const float2 gradient =
		g00 + u.x * (g10 - g00) + u.y * (g01 - g00) + u.x * u.y * (g00 - g10 - g01 + g11)
		+ du * float2(
			(v10 - v00) + u.y * (v00 - v10 - v01 + v11),
			(v01 - v00) + u.x * (v00 - v10 - v01 + v11));

	return float3(value, gradient);
}

// The surface normal at a point, from four octaves of drifting gradient noise.
//
// Each octave moves in its own direction, which is what stops the whole field from sliding
// as one sheet, and the amplitude falls faster than the frequency rises so the slope stays
// bounded as octaves are added.
float3 mvWaterNormal(float2 worldXZ, float distanceToCamera, float waveScale, float waveHeight, float time)
{
	const float2 base = worldXZ / max(waveScale, 0.01f);

	// The far surface is sampled at a rate no screen can resolve, and every octave past
	// that is noise that aliases and crawls. Dropping them with distance is the cheap
	// stand-in for the mip chain a texture-based version would have had.
	const float fade = saturate(1.0f - distanceToCamera / 4000.0f);

	float2 slope = 0.0f.xx;

	float amplitude = 1.0f;
	float frequency = 1.0f;

	const float2 drift[4] =
	{
		float2( 1.00f,  0.15f),
		float2(-0.60f,  0.80f),
		float2( 0.30f, -0.95f),
		float2(-0.85f, -0.45f),
	};

	[unroll]
	for (int i = 0; i < 4; i++)
	{
		const float octaveFade = saturate(fade * 4.0f - float(i));

		if (octaveFade > 0.0f)
		{
			const float3 noise = mvWaveNoise(base * frequency + drift[i] * time * (0.35f + 0.2f * float(i)));

			slope += noise.yz * amplitude * frequency * octaveFade;
		}

		amplitude *= 0.5f;
		frequency *= 2.1f;
	}

	slope *= waveHeight / max(waveScale, 0.01f);

	return normalize(float3(-slope.x, 1.0f, -slope.y));
}

float mvWaterFresnel(float cosTheta)
{
	// Water at normal incidence reflects about two per cent, and almost all of it at a
	// grazing angle. That range is the entire reason a lake is a mirror at the far shore
	// and a window at your feet.
	const float f0 = 0.02f;

	return f0 + (1.0f - f0) * pow(saturate(1.0f - cosTheta), 5.0f);
}

#endif
