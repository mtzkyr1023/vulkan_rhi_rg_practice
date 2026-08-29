#ifndef _MV_ENV_HLSLI_
#define _MV_ENV_HLSLI_

// The GPU twin of env/environment.cpp's sky model and cube geometry.
//
// The CPU implementation stays: the nine spherical-harmonic coefficients are a reduction
// over the whole cube and they have to reach the scene constant buffer, so they are still
// projected on the CPU from a radiance buffer this pass writes. What moves here is the part
// that dominated the bake -- a hundred thousand atmosphere integrations, each stepping the
// view ray sixteen times and the light ray eight times per step.

#define MV_PI 3.14159265359f

// Must match SkyGpuConstants in compute/environment_baker.cpp.
struct SkyConstants
{
	// The direction light travels, matching the scene's light direction.
	float3 lightDirection;
	float turbidity;

	float3 groundAlbedo;
	float sunIntensity;

	// Which cube face size this dispatch is writing, and which mip.
	uint faceSize;
	uint mipLevel;

	// Roughness for the prefilter pass; ignored by the sky pass.
	float roughness;
	uint sourceSize;
};

// Face order is +X, -X, +Y, -Y, +Z, -Z, and within a face u runs right and v runs down.
// Both APIs agree on this, which is why one table serves both -- and why this matches
// faceDirection in environment.cpp line for line.
float3 mvFaceDirection(uint face, float u, float v)
{
	switch (face)
	{
	case 0:  return float3( 1.0f,   -v,   -u);
	case 1:  return float3(-1.0f,   -v,    u);
	case 2:  return float3(    u, 1.0f,    v);
	case 3:  return float3(    u,-1.0f,   -v);
	case 4:  return float3(    u,   -v, 1.0f);
	default: return float3(   -u,   -v,-1.0f);
	}
}

// The inverse: which face a direction lands on, and where.
void mvDirectionToFace(float3 d, out uint face, out float u, out float v)
{
	const float ax = abs(d.x);
	const float ay = abs(d.y);
	const float az = abs(d.z);

	if (ax >= ay && ax >= az)
	{
		const float inv = 1.0f / ax;
		if (d.x > 0.0f) { face = 0; u = -d.z * inv; v = -d.y * inv; }
		else            { face = 1; u =  d.z * inv; v = -d.y * inv; }
	}
	else if (ay >= az)
	{
		const float inv = 1.0f / ay;
		if (d.y > 0.0f) { face = 2; u = d.x * inv; v =  d.z * inv; }
		else            { face = 3; u = d.x * inv; v = -d.z * inv; }
	}
	else
	{
		const float inv = 1.0f / az;
		if (d.z > 0.0f) { face = 4; u =  d.x * inv; v = -d.y * inv; }
		else            { face = 5; u = -d.x * inv; v = -d.y * inv; }
	}
}

// --- atmosphere --------------------------------------------------------------

#define MV_EARTH_RADIUS      6360e3f
#define MV_ATMOSPHERE_RADIUS 6420e3f
#define MV_RAYLEIGH_SCALE_H  7994.0f
#define MV_MIE_SCALE_H       1200.0f

float mvAtmosphereDistance(float3 o, float3 d)
{
	const float b = 2.0f * dot(o, d);
	const float c = dot(o, o) - MV_ATMOSPHERE_RADIUS * MV_ATMOSPHERE_RADIUS;
	const float disc = b * b - 4.0f * c;

	if (disc < 0.0f)
		return 0.0f;

	return (-b + sqrt(disc)) * 0.5f;
}

// The ground is the same colour in every downward direction, so it is worth computing once
// per dispatch rather than per texel -- but a shader has nowhere to cache it, and the
// zenith integration it needs is one ray out of a hundred thousand. Recomputed here.
float3 mvGroundRadiance(SkyConstants sky, float3 zenithRadiance)
{
	const float3 sun = normalize(-sky.lightDirection);

	const float sunOnGround = max(0.0f, sun.y);
	const float3 lit = zenithRadiance * 0.5f + float3(1.0f, 0.95f, 0.85f) * (sky.sunIntensity * 0.02f * sunOnGround);

	return lit * sky.groundAlbedo;
}

// Single-scattering through a spherical atmosphere: Rayleigh for the blue of the sky and
// Mie for the white haze that pools around the sun. Returns zero below the horizon; the
// caller substitutes the ground colour there.
float3 mvSkyRadianceAbove(SkyConstants sky, float3 direction)
{
	const float3 betaR = float3(3.8e-6f, 13.5e-6f, 33.1e-6f);
	const float betaM = 21e-6f * sky.turbidity;

	const float3 sun = normalize(-sky.lightDirection);
	const float3 dir = normalize(direction);

	const float3 origin = float3(0.0f, MV_EARTH_RADIUS + 1.0f, 0.0f);

	const float rayLength = mvAtmosphereDistance(origin, dir);
	if (rayLength <= 0.0f)
		return 0.0f.xxx;

	// Step counts kept low deliberately: the result is about to be blurred into nine
	// coefficients and a mip chain anyway.
	const uint kViewSamples = 16;
	const uint kLightSamples = 8;

	const float segment = rayLength / float(kViewSamples);

	float opticalDepthR = 0.0f;
	float opticalDepthM = 0.0f;

	float3 sumR = 0.0f.xxx;
	float3 sumM = 0.0f.xxx;

	const float mu = dot(dir, sun);

	// Rayleigh scatters almost evenly; Mie throws light strongly forward, which is what
	// puts the bright halo right around the sun.
	const float phaseR = 3.0f / (16.0f * MV_PI) * (1.0f + mu * mu);

	// A tighter lobe than the textbook 0.76: at that width the haze pools across
	// half the sky and washes the blue out of everything sunward. 0.85 confines it
	// to the sun's neighbourhood, and the cap keeps the core from climbing orders
	// of magnitude there -- the sun itself is the skybox's analytic disc, not this
	// halo's job.
	const float g = 0.85f;
	const float phaseM = min(
		3.0f / (8.0f * MV_PI) * ((1.0f - g * g) * (1.0f + mu * mu)) /
			((2.0f + g * g) * pow(1.0f + g * g - 2.0f * g * mu, 1.5f)),
		1.2f);

	for (uint i = 0; i < kViewSamples; i++)
	{
		const float3 samplePosition = origin + dir * (segment * (float(i) + 0.5f));
		const float height = sqrt(dot(samplePosition, samplePosition)) - MV_EARTH_RADIUS;

		const float hr = exp(-height / MV_RAYLEIGH_SCALE_H) * segment;
		const float hm = exp(-height / MV_MIE_SCALE_H) * segment;

		opticalDepthR += hr;
		opticalDepthM += hm;

		// How much of the sunlight survives the trip down to this sample.
		const float lightLength = mvAtmosphereDistance(samplePosition, sun);
		const float lightSegment = lightLength / float(kLightSamples);

		float lightDepthR = 0.0f;
		float lightDepthM = 0.0f;

		bool blocked = false;

		for (uint j = 0; j < kLightSamples; j++)
		{
			const float3 lightPosition = samplePosition + sun * (lightSegment * (float(j) + 0.5f));
			const float lightHeight = sqrt(dot(lightPosition, lightPosition)) - MV_EARTH_RADIUS;

			if (lightHeight < 0.0f)
			{
				blocked = true;
				break;
			}

			lightDepthR += exp(-lightHeight / MV_RAYLEIGH_SCALE_H) * lightSegment;
			lightDepthM += exp(-lightHeight / MV_MIE_SCALE_H) * lightSegment;
		}

		if (blocked)
			continue;

		const float3 tau = betaR * (opticalDepthR + lightDepthR) + (betaM * 1.1f) * (opticalDepthM + lightDepthM);
		const float3 attenuation = exp(-tau);

		sumR += attenuation * hr;
		sumM += attenuation * hm;
	}

	return (sumR * betaR * phaseR + sumM * betaM * phaseM) * sky.sunIntensity;
}

float3 mvSkyRadiance(SkyConstants sky, float3 direction)
{
	const float3 dir = normalize(direction);

	if (dir.y < 0.0f)
		return mvGroundRadiance(sky, mvSkyRadianceAbove(sky, float3(0.0f, 1.0f, 0.0f)));

	return mvSkyRadianceAbove(sky, dir);
}

#endif
