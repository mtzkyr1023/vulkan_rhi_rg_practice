// Screen-space reflections for the water surface.
//
// For each pixel where the water plane is the visible surface, this bounces the view ray
// off the same wavy normal the water pass shades with and marches the reflected ray
// against the scene depth buffer. Where it lands on something that is on screen, the
// scene's own colour is the reflection -- the island in the lake, at full sharpness, which
// no prefiltered cube can provide. Where it leaves the screen or hits nothing, the alpha
// says so and the water pass falls back to the cube exactly as before, so SSR only ever
// adds what the screen can prove.
//
// A separate dispatch rather than part of the water pixel shader for one hard reason: the
// reflection has to sample the scene colour, and during the water draw the scene colour is
// the render target. By the time this runs the geometry and the clouds are already in it,
// so a cloud over the lake reflects in the lake.
//
// Full resolution, unlike the cloud march. A cloud is a low-frequency thing; a reflected
// mountain ridge is not, and at half rate its edge shimmers against the waves.

#define MV_CUSTOM_PUSH_CONSTANTS
#include "common.hlsli"
#include "water_common.hlsli"

#ifdef MV_TARGET_VULKAN
[[vk::push_constant]] WaterConstants water;
#else
ConstantBuffer<WaterConstants> water : register(b0, space9);
#endif

Texture2D<float>  sceneDepth : register(t0, space0);
Texture2D<float4> sceneColor : register(t1, space0);

SamplerState linearClamp : register(s2, space0);

#ifdef MV_TARGET_VULKAN
[[vk::image_format("rgba16f")]] RWTexture2D<float4> target : register(u3, space0);
#else
RWTexture2D<float4> target : register(u3, space0);
#endif

// View depth of whatever the depth buffer holds at a UV.
float mvSceneViewZ(float2 uv)
{
	const float raw = sceneDepth.SampleLevel(linearClamp, uv, 0.0f);

	return water.depthLinearB / (raw + water.depthLinearA);
}

// How trustworthy a hit at this UV is. Fades to zero at the screen border, because a ray
// about to leave the screen has been reflecting less and less of what it claims to, and a
// hard cut there draws the edge of the screen onto the middle of the lake.
float mvEdgeFade(float2 uv)
{
	const float2 border = min(uv, 1.0f - uv);

	return saturate(min(border.x, border.y) / 0.08f);
}

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
	if (id.x >= (uint)water.viewportSize.x || id.y >= (uint)water.viewportSize.y)
		return;

	const float2 uv = (float2(id.xy) + 0.5f) / water.viewportSize;

	const float3 forward = normalize(water.cameraForward);
	float3 right;
	float3 up;
	mvWaterBasis(forward, right, up);

	const float aspect = water.viewportSize.x / water.viewportSize.y;

	const float3 rayDirection = mvWaterRay(uv, forward, right, up, water.tanHalfFov, aspect);
	const float3 rayOrigin = water.cameraPosition;

	const float heightAbove = rayOrigin.y - water.level;

	// Underwater there is no reflection to march -- the pass above the pixel shader's
	// underwater branch never reads this texture -- and a ray heading up misses the plane.
	if (water.ssrStrength <= 0.0f || heightAbove <= 0.0f || rayDirection.y >= 0.0f)
	{
		target[id.xy] = 0.0f.xxxx;
		return;
	}

	const float distanceToWater = -heightAbove / rayDirection.y;

	// The terrain is in front of the plane here: this pixel shows ground, not water.
	const float cosToForward = dot(rayDirection, forward);
	if (mvSceneViewZ(uv) / max(cosToForward, 1e-4f) <= distanceToWater)
	{
		target[id.xy] = 0.0f.xxxx;
		return;
	}

	const float3 surfacePosition = rayOrigin + rayDirection * distanceToWater;

	// The same waves the water pass will shade with, at the same time. Anything else and
	// the reflection slides against the surface that is supposedly producing it.
	const float3 normal = mvWaterNormal(
		surfacePosition.xz, distanceToWater, water.waveScale, water.waveHeight, water.time);

	const float3 reflected = reflect(rayDirection, normal);

	// A wave can tilt the reflection back down into the water, and what is under the
	// surface is exactly what the screen cannot answer with.
	if (reflected.y <= 0.001f)
	{
		target[id.xy] = 0.0f.xxxx;
		return;
	}

	// The march: geometric steps, short where the reflected image has detail worth being
	// exact about and long where a missed metre no longer lands on a different pixel.
	// 64 steps at this growth reach a bit over two kilometres, which is as far as anything
	// the lake can see; past it the cube takes over via the miss path.
	const uint kSteps = 64;
	const float kGrowth = 1.145f;

	float stepLength = 0.4f;
	float travelled = stepLength;
	float previous = 0.0f;

	float hitConfidence = 0.0f;
	float2 hitUv = 0.0f.xx;

	for (uint i = 0; i < kSteps; i++)
	{
		const float3 position = surfacePosition + reflected * travelled;

		const float3 projected = mvWaterProject(
			position, rayOrigin, forward, right, up, water.tanHalfFov, aspect);

		// Behind the camera, or off the screen: nothing on screen will ever answer this
		// ray, however much further it marches.
		if (projected.z <= 0.1f || any(projected.xy < 0.0f) || any(projected.xy > 1.0f))
			break;

		const float rawDepth = sceneDepth.SampleLevel(linearClamp, projected.xy, 0.0f);

		// The sky is not a surface. A cleared depth linearises to the far plane, and the
		// far water reaches past it, so without this test every ray reflecting towards the
		// horizon "hits" the sky immediately and paints a sharp copy of it -- clouds
		// included, which the depth buffer cannot possibly know about -- onto the water.
		// Marching on rather than stopping, because the ray may pass over sky pixels and
		// land on a mountain further along.
		if (rawDepth >= 1.0f)
		{
			previous = travelled;
			stepLength *= kGrowth;
			travelled += stepLength;

			continue;
		}

		const float sceneZ = water.depthLinearB / (rawDepth + water.depthLinearA);

		if (projected.z > sceneZ + 0.01f)
		{
			// Crossed. Only a crossing close behind the surface is a hit: a large gap
			// means the ray passed behind something -- a peak in front of a far slope --
			// and drawing the slope where the hidden face should be smears it. The window
			// grows with the step, because so does how far past the true crossing this
			// sample can land.
			if (projected.z - sceneZ < max(2.0f, (travelled - previous) * 2.0f))
			{
				// Binary refinement between the last two samples: the linear march found
				// the interval, this finds the pixel.
				float lo = previous;
				float hi = travelled;

				float2 refinedUv = projected.xy;

				[unroll]
				for (uint r = 0; r < 5; r++)
				{
					const float mid = (lo + hi) * 0.5f;

					const float3 midProjected = mvWaterProject(
						surfacePosition + reflected * mid,
						rayOrigin, forward, right, up, water.tanHalfFov, aspect);

					if (midProjected.z > mvSceneViewZ(midProjected.xy))
					{
						hi = mid;
						refinedUv = midProjected.xy;
					}
					else
					{
						lo = mid;
					}
				}

				hitUv = refinedUv;
				hitConfidence = mvEdgeFade(refinedUv);

				break;
			}

			// Passed behind an occluder: keep going, the far side may still be visible.
		}

		previous = travelled;
		stepLength *= kGrowth;
		travelled += stepLength;
	}

	if (hitConfidence <= 0.0f)
	{
		target[id.xy] = 0.0f.xxxx;
		return;
	}

	const float3 color = sceneColor.SampleLevel(linearClamp, hitUv, 0.0f).rgb;

	target[id.xy] = float4(color, hitConfidence * water.ssrStrength);
}
