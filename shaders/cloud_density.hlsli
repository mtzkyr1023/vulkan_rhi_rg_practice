#ifndef _MV_CLOUD_DENSITY_HLSLI_
#define _MV_CLOUD_DENSITY_HLSLI_

// The density field, shared by every pass that samples the layer.
//
// Three of them do now -- the view march, the shadow map baked from the sun's side and the
// environment cube -- and a cloud that shades differently from the one it casts a shadow
// with is worse than no shadow at all. So the model lives here once.
//
// The resources are named globals rather than function parameters. HLSL can pass a texture
// to a function, but a header that does it has to be instantiated per call site anyway, and
// naming them by convention keeps the register assignments where a reader of the shader can
// see them. The includer declares, before including this:
//
//   Texture3D<float4> mvCloudShape;
//   Texture3D<float4> mvCloudDetail;
//   Texture2D<float4> mvCloudWeather;
//   SamplerState      mvCloudRepeat;
//
// Positions are planet-centred: the shell is two spheres about the origin and altitude is
// just a length. The horizontal coordinates are still world ones, so the weather map and
// the volumes line up with the scene.

#include "clouds.hlsli"

// Density at a world position, and the height fraction that produced it.
//
// useDetail is off for the light march and for the shadow bake, where the erosion costs as
// much as the sample it refines and contributes nothing either of them can show.
float mvCloudDensity(CloudFieldConstants field, float3 position, bool useDetail, out float heightFraction)
{
	const float radius = length(position);

	heightFraction = saturate((radius - (field.planetRadius + field.layerBottom))
		/ max(field.layerTop - field.layerBottom, 1e-3f));

	if (heightFraction <= 0.0f || heightFraction >= 1.0f)
		return 0.0f;

	// The weather map is indexed by horizontal position, wrapping. Wind slides the whole
	// field rather than animating the noise, which is both cheaper and what wind does.
	const float2 weatherUv = (position.xz + field.windOffset.xz) * field.coverageScale;
	const float4 weather = mvCloudWeather.SampleLevel(mvCloudRepeat, weatherUv, 0.0f);

	const float coverage = weather.r;
	const float cloudType = weather.g;

	if (coverage <= 0.0f)
		return 0.0f;

	// The base shape, tiled. The vertical axis is scaled the same as the horizontal ones so
	// billows are not stretched into columns.
	const float3 shapeUvw = (position + field.windOffset) / max(field.shapeScale, 1e-3f);
	const float4 shape = mvCloudShape.SampleLevel(mvCloudRepeat, shapeUvw, 0.0f);

	// Erode the Perlin-Worley base with its own Worley channels: the low-frequency billows
	// carve the silhouette, the higher ones roughen it.
	const float worleyFbm = shape.g * 0.625f + shape.b * 0.25f + shape.a * 0.125f;

	float density = mvRemap(shape.r, worleyFbm - 1.0f, 1.0f, 0.0f, 1.0f);

	// Altitude decides how much of that survives, and coverage decides how much of the sky
	// gets any at all. Subtracting coverage rather than multiplying is what keeps the edges
	// sharp as the sky clears instead of fading the whole layer out.
	density *= mvHeightGradient(heightFraction, cloudType);
	density = mvRemap(density, 1.0f - coverage, 1.0f, 0.0f, 1.0f);

	if (density <= 0.0f)
		return 0.0f;

	if (useDetail)
	{
		const float3 detailUvw = (position + field.windOffset * 1.7f) / max(field.detailScale, 1e-3f);
		const float3 detail = mvCloudDetail.SampleLevel(mvCloudRepeat, detailUvw, 0.0f).rgb;

		const float detailFbm = detail.r * 0.625f + detail.g * 0.25f + detail.b * 0.125f;

		// Wispy at the bottom, billowy at the top: inverting the erosion with altitude is a
		// cheap stand-in for the way a cumulus frays underneath and stays firm on top.
		const float modifier = lerp(1.0f - detailFbm, detailFbm, saturate(heightFraction * 5.0f));

		density = mvRemap(density, modifier * field.detailStrength, 1.0f, 0.0f, 1.0f);
	}

	return saturate(density) * field.densityScale;
}

// How much sunlight reaches a point inside the cloud. A short march towards the sun, with
// the steps growing so the far ones cover the bulk cheaply.
float mvCloudSunTransmittance(CloudFieldConstants field, float3 position, float3 toSun, uint steps)
{
	float opticalDepth = 0.0f;

	// Sized against the layer, so it does not need retuning when the layer does.
	float stepLength = (field.layerTop - field.layerBottom) / max(float(steps), 1.0f) * 0.5f;

	float3 samplePosition = position;

	for (uint i = 0; i < steps; i++)
	{
		samplePosition += toSun * stepLength;

		float unused;
		opticalDepth += mvCloudDensity(field, samplePosition, false, unused) * stepLength;

		stepLength *= 1.5f;
	}

	return exp(-opticalDepth * field.extinction);
}

// The whole layer's transmittance along a ray, with no shading: what a shadow needs and all
// it needs. Steps uniformly between the two shell crossings rather than growing, because
// unlike the light march this one is not anchored at a point already inside the cloud.
float mvCloudBeamTransmittance(CloudFieldConstants field, float3 origin, float3 direction, uint steps)
{
	const float innerRadius = field.planetRadius + field.layerBottom;
	const float outerRadius = field.planetRadius + field.layerTop;

	const float radius = length(origin);

	float start;
	float end;

	if (radius < innerRadius)
	{
		// Below the layer, which is where the ground is: enter at the bottom, leave at the
		// top. Both are far roots, since the origin is inside both spheres.
		start = mvRaySphereFar(origin, direction, innerRadius);
		end = mvRaySphereFar(origin, direction, outerRadius);
	}
	else if (radius > outerRadius)
	{
		start = mvRaySphereNear(origin, direction, outerRadius);
		end = mvRaySphereFar(origin, direction, innerRadius);

		if (end < 0.0f)
			end = mvRaySphereFar(origin, direction, outerRadius);
	}
	else
	{
		start = 0.0f;

		const float toInner = mvRaySphereFar(origin, direction, innerRadius);
		const float toOuter = mvRaySphereFar(origin, direction, outerRadius);

		end = (toInner > 0.0f) ? min(toInner, toOuter) : toOuter;
	}

	start = max(start, 0.0f);

	if (end <= start)
		return 1.0f;

	const float stepLength = (end - start) / float(max(steps, 1u));

	float opticalDepth = 0.0f;
	float travelled = start + stepLength * 0.5f;

	for (uint i = 0; i < steps; i++)
	{
		float unused;
		opticalDepth += mvCloudDensity(field, origin + direction * travelled, false, unused) * stepLength;

		travelled += stepLength;
	}

	return exp(-opticalDepth * field.extinction);
}

#endif
