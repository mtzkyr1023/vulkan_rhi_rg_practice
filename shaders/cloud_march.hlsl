// The cloud raymarch, at half resolution.
//
// Marches a spherical shell of atmosphere, accumulating scattered light and transmittance.
// The output is premultiplied: rgb is what the clouds added, a is what fraction of the
// scene behind them survives, so the composite is one blend with no division.
//
// Half resolution because the march is the whole cost of the effect and a cloud is a
// low-frequency thing -- the upsample gets the edges back by weighting on depth, and what
// it cannot get back was never resolvable at full rate either.

#define MV_CUSTOM_PUSH_CONSTANTS
#include "common.hlsli"
#include "clouds.hlsli"

#ifdef MV_TARGET_VULKAN
[[vk::push_constant]] CloudConstants clouds;
#else
ConstantBuffer<CloudConstants> clouds : register(b0, space9);
#endif

// The names cloud_density.hlsli expects. Declared before it is included, which is the whole
// of its contract.
Texture3D<float4> mvCloudShape   : register(t0, space0);
Texture3D<float4> mvCloudDetail  : register(t1, space0);
Texture2D<float4> mvCloudWeather : register(t2, space0);

// The scene's depth, so the march stops where the terrain already occluded it.
Texture2D<float>  sceneDepth     : register(t3, space0);

SamplerState mvCloudRepeat : register(s4, space0);
SamplerState linearClamp   : register(s5, space0);

#ifdef MV_TARGET_VULKAN
[[vk::image_format("rgba16f")]] RWTexture2D<float4> target : register(u6, space0);
#else
RWTexture2D<float4> target : register(u6, space0);
#endif

#include "cloud_density.hlsli"

// The field half of the push constant, which is what the shared density code takes.
CloudFieldConstants marchField()
{
	CloudFieldConstants field;

	field.windOffset = clouds.windOffset;
	field.coverageScale = clouds.coverageScale;

	field.planetRadius = clouds.planetRadius;
	field.layerBottom = clouds.layerBottom;
	field.layerTop = clouds.layerTop;
	field.shapeScale = clouds.shapeScale;

	field.detailScale = clouds.detailScale;
	field.detailStrength = clouds.detailStrength;
	field.densityScale = clouds.densityScale;
	field.extinction = clouds.extinction;

	return field;
}

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
	if (id.x >= clouds.targetSize.x || id.y >= clouds.targetSize.y)
		return;

	const float2 uv = (float2(id.xy) + 0.5f) / float2(clouds.targetSize);

	// The view ray, rebuilt from the camera basis and the field of view exactly as the
	// skybox does. Clip space y is flipped relative to uv, which is the convention the
	// fullscreen passes use.
	const float2 ndc = float2(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f);

	const float3 forward = normalize(clouds.cameraForward);
	const float3 right = normalize(cross(forward, float3(0.0f, 1.0f, 0.0f)));
	const float3 up = cross(right, forward);

	const float aspect = float(clouds.targetSize.x) / float(clouds.targetSize.y);

	const float3 rayOrigin = clouds.cameraPosition;
	const float3 rayDirection = normalize(
		forward +
		right * (ndc.x * clouds.tanHalfFov * aspect) +
		up * (ndc.y * clouds.tanHalfFov));

	// Planet-centred, so the shell is two spheres and altitude is just a length. The scene
	// sits on the surface, with world y measured from it.
	const float3 origin = float3(rayOrigin.x, rayOrigin.y + clouds.planetRadius, rayOrigin.z);

	const float innerRadius = clouds.planetRadius + clouds.layerBottom;
	const float outerRadius = clouds.planetRadius + clouds.layerTop;

	const float toInner = mvRaySphereFar(origin, rayDirection, innerRadius);
	const float toOuter = mvRaySphereFar(origin, rayDirection, outerRadius);

	if (toOuter <= 0.0f)
	{
		// Looking away from the layer entirely.
		target[id.xy] = float4(0.0f, 0.0f, 0.0f, 1.0f);
		return;
	}

	const float altitude = length(origin) - clouds.planetRadius;

	// Inside the shell, below it, or above it: the near end is whichever boundary is in
	// front, and the far end is where the ray leaves.
	//
	// From inside or above the layer, the end is the *near* crossing of the inner sphere:
	// that is where the ray drops out of the layer's bottom. The far root is its exit on
	// the other side of the world, and marching to it spreads the step budget across
	// hundreds of kilometres of empty planet interior -- the layer then falls between two
	// steps and the clouds fade out the moment the camera climbs into them.
	float marchStart;
	float marchEnd;

	if (altitude < clouds.layerBottom)
	{
		marchStart = max(toInner, 0.0f);
		marchEnd = toOuter;
	}
	else if (altitude > clouds.layerTop)
	{
		marchStart = max(mvRaySphereNear(origin, rayDirection, outerRadius), 0.0f);

		const float innerNear = mvRaySphereNear(origin, rayDirection, innerRadius);
		marchEnd = (innerNear > 0.0f) ? innerNear : toOuter;
	}
	else
	{
		marchStart = 0.0f;

		const float innerNear = mvRaySphereNear(origin, rayDirection, innerRadius);
		marchEnd = (innerNear > 0.0f) ? innerNear : toOuter;
	}

	// The planet occludes. A clamp on the far end rather than an early-out on any ray that
	// hits it: from above the layer, every downward ray hits the planet with the clouds in
	// front of it, which is exactly the view an early-out throws away. Below the layer the
	// clamp does the early-out's old job -- a below-horizon ray meets the ground long
	// before the shell's far side, the interval empties, and nothing is drawn through the
	// planet. The scene's own depth buffer cannot help there: past the terrain, every
	// downward ray is looking at empty sky.
	const float toPlanet = mvRaySphereNear(origin, rayDirection, clouds.planetRadius);
	if (toPlanet > 0.0f)
		marchEnd = min(marchEnd, toPlanet);

	marchEnd = min(marchEnd, clouds.maxDistance);

	// Anything the terrain already occludes is not worth marching to.
	{
		const float rawDepth = sceneDepth.SampleLevel(linearClamp, uv, 0.0f);

		if (rawDepth < 1.0f)
		{
			marchEnd = min(marchEnd, mvDepthToDistance(clouds, rawDepth, dot(rayDirection, forward)));
		}
	}

	if (marchEnd <= marchStart)
	{
		target[id.xy] = float4(0.0f, 0.0f, 0.0f, 1.0f);
		return;
	}

	const float stepLength = (marchEnd - marchStart) / float(clouds.viewSteps);

	// A per-pixel offset along the ray, so the step boundaries do not line up into rings.
	// The upsample spreads what is left of them.
	const float jitter = frac(sin(dot(float2(id.xy), float2(12.9898f, 78.233f))) * 43758.5453f);

	const float3 toSun = normalize(-clouds.lightDirection);
	const float cosAngle = dot(rayDirection, toSun);

	const CloudFieldConstants field = marchField();

	// Two lobes: a narrow forward one for the silver lining, a wide one for the body. One
	// lobe cannot be both.
	const float phase = lerp(
		mvHenyeyGreenstein(cosAngle, clouds.forwardScattering),
		mvHenyeyGreenstein(cosAngle, -clouds.backwardScattering),
		clouds.scatterBlend);

	float3 scattering = 0.0f.xxx;
	float transmittance = 1.0f;

	float travelled = marchStart + stepLength * jitter;

	for (uint step = 0; step < clouds.viewSteps; step++)
	{
		if (transmittance < 0.01f)
			break;

		const float3 position = origin + rayDirection * travelled;

		float heightFraction;
		const float density = mvCloudDensity(field, position, true, heightFraction);

		if (density > 0.0f)
		{
			const float sunTransmittance = mvCloudSunTransmittance(field, position, toSun, clouds.lightSteps);

			// Ambient from the sky, brighter at the top of the cloud where more of the
			// hemisphere is visible. Without it the shadowed side is black, which no cloud
			// ever is.
			const float3 ambient = clouds.sunColor * clouds.ambientStrength * lerp(0.35f, 1.0f, heightFraction);

			const float3 luminance = clouds.sunColor * sunTransmittance * phase + ambient;

			// The closed form of accumulating scattering under exponential extinction over
			// the step, rather than a Riemann sum that loses energy as the step grows.
			//
			// The scattering coefficient is the extinction coefficient: a water droplet
			// absorbs almost nothing, so a cloud is a single-scattering albedo of very
			// nearly one. Writing the integral with density as the scattering coefficient
			// and density * extinction as the extinction one -- which is what it looked
			// like before -- makes the albedo 1 / extinction, so at the default 0.08 every
			// cloud came out twelve times too bright. That is what flattened them: the
			// shading was there, but it was so far above white that all of it clipped.
			const float stepExtinction = max(density * clouds.extinction, 1e-6f);
			const float stepTransmittance = exp(-stepExtinction * stepLength);

			scattering += transmittance * luminance * (1.0f - stepTransmittance);
			transmittance *= stepTransmittance;
		}

		travelled += stepLength;
	}

	// Aerial perspective, cheaply: the further away the column of cloud started, the more
	// of the atmosphere is in front of it, and the more it dissolves into the sky already
	// drawn behind. Fading towards no-cloud rather than towards a colour is what makes this
	// correct without the shader needing the sky: the sky is exactly what is behind.
	//
	// Without it the layer stays fully opaque and fully bright right up to the horizon,
	// where a shell this curved puts hundreds of kilometres of cloud into a few pixels.
	// Anchored to maxDistance, because that is the edge being hidden: past it the march
	// stops outright, and without a fade the cutoff reads as a wall of cloud standing in
	// the sky. Held at full strength for the first third so the fade only ever touches the
	// last sliver above the horizon -- an exponential from zero distance instead eats cloud
	// that is plainly visible, since from a few hundred metres up a ray only ten degrees
	// above the horizon already enters the layer several kilometres out, and fading those
	// leaves the entire sky translucent.
	const float fadeBegin = clouds.maxDistance * 0.35f;
	const float horizonFade = 1.0f - smoothstep(fadeBegin, clouds.maxDistance, max(marchStart, 0.0f));

	scattering *= horizonFade;
	transmittance = lerp(1.0f, transmittance, horizonFade);

	target[id.xy] = float4(scattering, transmittance);
}
