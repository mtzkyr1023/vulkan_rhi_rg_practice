#ifndef _MV_CLOUDS_HLSLI_
#define _MV_CLOUDS_HLSLI_

// Everything needed to evaluate the density field at a point, and nothing else.
//
// Split out of CloudConstants because three passes now sample the same layer for three
// different reasons: the view march, the shadow map baked from the sun's side, and the
// environment cube. Only the first of those has a camera, a screen or a phase function,
// and handing the whole of CloudConstants to the other two would spend most of a
// hundred-and-twenty-eight byte push constant budget on fields they never read.
struct CloudFieldConstants
{
	float3 windOffset;
	float  coverageScale;

	float  planetRadius;
	float  layerBottom;
	float  layerTop;
	float  shapeScale;

	float  detailScale;
	float  detailStrength;
	float  densityScale;
	float  extinction;
};

// Must match CloudGpuConstants in clouds/cloud_renderer.cpp.
struct CloudConstants
{
	float3 cameraPosition;
	float planetRadius;

	// The direction light travels, matching the scene's.
	float3 lightDirection;
	float layerBottom;

	float3 sunColor;
	float layerTop;

	float3 windOffset;
	float coverageScale;

	// The view ray is rebuilt from the camera basis and the field of view, the same way
	// the skybox does it, rather than from an inverse view-projection. The forward vector
	// is already in the scene constants, the inverse is not, and reconstructing a ray this
	// way needs no agreement about how a matrix is packed into a constant buffer.
	float3 cameraForward;
	float tanHalfFov;

	// Metres across one repeat of the shape volume, and of the detail volume.
	float shapeScale;
	float detailScale;
	float detailStrength;
	float densityScale;

	// Henyey-Greenstein asymmetry, and how much of the backward lobe to mix in. One lobe
	// cannot be both strongly forward for the silver lining and wide enough for the body.
	float forwardScattering;
	float backwardScattering;
	float scatterBlend;
	float extinction;

	uint2 targetSize;
	uint viewSteps;
	uint lightSteps;

	// Where the march gives up, in metres. Past this the layer is faded into the sky.
	float maxDistance;
	float ambientStrength;

	// The two projection coefficients that turn a depth-buffer value back into a view-space
	// distance: linear = depthLinearB / (depth + depthLinearA). Cheaper than an inverse
	// matrix and, unlike one, exact for the projection that actually produced the buffer.
	float depthLinearA;
	float depthLinearB;
};

// Distance along the view ray to whatever the depth buffer holds, in metres.
//
// cosToForward is the ray's angle off the camera axis: the depth buffer stores distance
// along the axis, and a ray at the edge of the frame travels further to reach the same
// plane.
float mvDepthToDistance(CloudConstants clouds, float rawDepth, float cosToForward)
{
	const float linearDepth = clouds.depthLinearB / (rawDepth + clouds.depthLinearA);

	return linearDepth / max(cosToForward, 1e-4f);
}

// Distance along a ray to a sphere centred on the planet, taking the far root. Negative
// when the ray misses, which for a shell only happens below the horizon.
float mvRaySphereFar(float3 origin, float3 direction, float radius)
{
	const float b = dot(origin, direction);
	const float c = dot(origin, origin) - radius * radius;

	const float disc = b * b - c;
	if (disc < 0.0f)
		return -1.0f;

	return -b + sqrt(disc);
}

float mvRaySphereNear(float3 origin, float3 direction, float radius)
{
	const float b = dot(origin, direction);
	const float c = dot(origin, origin) - radius * radius;

	const float disc = b * b - c;
	if (disc < 0.0f)
		return -1.0f;

	return -b - sqrt(disc);
}

// The Henyey-Greenstein phase function: how much light scattered once inside a cloud comes
// back towards the viewer, as a function of the angle to the sun. g near 1 throws light
// forward, which is what makes the edge of a cloud in front of the sun blaze.
float mvHenyeyGreenstein(float cosAngle, float g)
{
	const float gg = g * g;

	// Without the 1/4pi the function integrates to 4pi rather than 1, which is deliberate:
	// the sun colour here is an artist-facing intensity, not an irradiance, and dividing by
	// 4pi leaves the sun term five times smaller than the ambient one. The clouds then come
	// out uniformly white with no shading at all -- the phase function is present but
	// contributes nothing visible.
	return (1.0f - gg) / pow(max(1.0f + gg - 2.0f * g * cosAngle, 1e-4f), 1.5f);
}

// Remaps a value from one range to another, clamped. The cloud model is mostly remaps.
float mvRemap(float value, float lowIn, float highIn, float lowOut, float highOut)
{
	return lowOut + saturate((value - lowIn) / max(highIn - lowIn, 1e-5f)) * (highOut - lowOut);
}

// How dense the layer is allowed to be at this fraction of its height, for a given cloud
// type. Stratus is a flat sheet low down, cumulus a tower that bulges in the middle; the
// type field slides between them.
float mvHeightGradient(float heightFraction, float cloudType)
{
	const float stratus = mvRemap(heightFraction, 0.0f, 0.10f, 0.0f, 1.0f)
		* mvRemap(heightFraction, 0.15f, 0.30f, 1.0f, 0.0f);

	const float cumulus = mvRemap(heightFraction, 0.0f, 0.20f, 0.0f, 1.0f)
		* mvRemap(heightFraction, 0.60f, 1.00f, 1.0f, 0.0f);

	return lerp(stratus, cumulus, saturate(cloudType));
}

#endif
