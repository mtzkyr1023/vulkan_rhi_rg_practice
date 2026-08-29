// A water surface, drawn as a fullscreen pass rather than as geometry.
//
// The surface is one plane, y = level. A quad big enough to reach the horizon would need a
// far plane to match and would still be a mesh whose tessellation decides how far the waves
// can be seen; intersecting the plane analytically per pixel has neither problem, reaches
// exactly as far as the ray does, and gets the scene depth it needs for free -- the same
// texture it would otherwise have had to be depth-tested against.
//
// It is also the arrangement the cloud composite already uses, so the ray reconstruction,
// the depth linearisation and the premultiplied blend are all the ones proven there.
//
// What this does not do is refract. Bending the view ray needs the scene colour as a
// texture, and the scene colour is the target being blended into. Absorption over the path
// through the water carries most of what refraction would have shown -- that the bottom
// goes blue-green and then vanishes with depth -- and the surface distortion it leaves out
// is the part that a still lake does not have anyway.
//
// Reflection comes from two sources layered by confidence: the screen, where the separate
// SSR march found the reflected point on it, and the environment cube everywhere else. The
// wave field itself lives in water_common.hlsli, shared with that march -- a reflection
// bounced off waves even slightly different from the ones drawn here slides across them.

#define MV_CUSTOM_PUSH_CONSTANTS
#include "common.hlsli"
#include "water_common.hlsli"

#ifdef MV_TARGET_VULKAN
[[vk::push_constant]] WaterConstants water;
#else
ConstantBuffer<WaterConstants> water : register(b0, space9);
#endif

Texture2D<float> sceneDepth : register(t0, space0);

// The same cube the rest of the frame is lit by, which by now has the cloud layer in its
// filtered levels: what the water reflects is what is actually overhead.
TextureCube environmentCube : register(t1, space0);

SamplerState linearClamp : register(s2, space0);

// What the SSR march found: the reflected scene colour where the reflected ray landed on
// screen, and in alpha how much to trust it.
Texture2D<float4> ssrReflection : register(t3, space0);

struct WaterVSOutput
{
	float4 position : SV_POSITION;
	float2 uv       : TEXCOORD0;
};

WaterVSOutput VSMain(uint vertexId : SV_VertexID)
{
	WaterVSOutput output;

	output.uv = float2((vertexId << 1) & 2, vertexId & 2);
	output.position = float4(output.uv * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);

	return output;
}

float4 PSMain(WaterVSOutput input) : SV_TARGET
{
	const float3 forward = normalize(water.cameraForward);
	float3 right;
	float3 up;
	mvWaterBasis(forward, right, up);

	const float aspect = water.viewportSize.x / water.viewportSize.y;

	const float3 rayDirection = mvWaterRay(input.uv, forward, right, up, water.tanHalfFov, aspect);
	const float3 rayOrigin = water.cameraPosition;

	const float heightAbove = rayOrigin.y - water.level;

	// How far the scene behind this pixel is, along the same ray. The depth buffer measures
	// along the camera axis, so a ray at the edge of the frame travels further to reach the
	// same plane -- the same correction the cloud march makes.
	const float rawDepth = sceneDepth.SampleLevel(linearClamp, input.uv, 0.0f);

	const float cosToForward = dot(rayDirection, forward);

	float distanceToScene = 1e9f;

	if (rawDepth < 1.0f)
		distanceToScene = (water.depthLinearB / (rawDepth + water.depthLinearA)) / max(cosToForward, 1e-4f);

	// --- under the surface ---------------------------------------------------
	//
	// Every ray leaves the eye already in the water, so there is no surface to intersect in
	// front of anything: what there is instead is a path length through water for the whole
	// frame. Drawing only the plane here would leave the view identical above and below the
	// waterline, which is the one thing that cannot be true.
	//
	// The underside of the surface is not drawn. Looking up out of water is Snell's window,
	// and a window is refraction -- which this pass deliberately does not do. What is drawn
	// is the absorption, which is most of what being underwater looks like anyway.
	if (heightAbove < 0.0f)
	{
		// A ray heading up leaves the water at the surface; past that it is air, and the
		// sky it reaches is not attenuated by water it never crossed.
		float path = distanceToScene;

		if (rayDirection.y > 0.0f)
			path = min(path, -heightAbove / rayDirection.y);

		path = min(path, 5000.0f);

		// Scalar, unlike the per-channel absorption above the surface: the blend has one
		// alpha and the destination is the scene already drawn, so the colour has to ride
		// in the scattered term rather than in what survives.
		const float mean = dot(water.extinction, 0.3333f.xxx);
		const float opacity = 1.0f - exp(-mean * path);

		return float4(water.scatterColor * opacity * water.iblIntensity, opacity);
	}

	// Above the surface and looking up: the plane is behind the camera.
	if (rayDirection.y >= 0.0f)
		discard;

	const float distanceToWater = -heightAbove / rayDirection.y;

	// The terrain is in front of the water here, so there is no water to see.
	if (distanceToScene <= distanceToWater)
		discard;

	const float3 surfacePosition = rayOrigin + rayDirection * distanceToWater;

	const float3 normal = mvWaterNormal(
		surfacePosition.xz, distanceToWater, water.waveScale, water.waveHeight, water.time);
	const float3 viewDirection = -rayDirection;

	const float NdotV = saturate(dot(normal, viewDirection));

	// The path the light travels through the water: down to the bottom and back up. Halved
	// for the return trip rather than doubled, because the ray already covers the way in
	// and the way out is shorter the more vertical it is. An approximation, and the one
	// place a depth-only model can be told from a refracting one.
	const float pathLength = min(distanceToScene - distanceToWater, 5000.0f);

	// Beer-Lambert, per channel. Red is absorbed first, which is the whole of why water is
	// blue and why it stops being blue in a glass.
	const float3 absorption = exp(-water.extinction * pathLength);

	const float fresnel = mvWaterFresnel(NdotV) * water.reflectionStrength;

	// The sky and the clouds in it, off the wavy surface. Roughness picks a filtered level,
	// which is what keeps the far surface from being a field of aliasing highlights.
	//
	// Never level 0. The environment bake strips the clouds back out of that one level so
	// the skybox can draw a sharp sky under the volumetric pass, so it is the one level
	// whose reflection would be missing them; every filtered level under it carries the
	// layer. Waves keep the surface rougher than a mirror everywhere anyway.
	const float3 reflectionDirection = reflect(rayDirection, normal);

	const float mipCount = 6.0f;
	float3 reflection = environmentCube.SampleLevel(
		linearClamp, reflectionDirection, max(water.roughness * (mipCount - 1.0f), 1.0f)).rgb;

	// The cube is image-based lighting like any other, so it obeys the same intensity the
	// rest of the scene is lit by. The SSR result below deliberately does not: it is a copy
	// of scene colour that was already lit -- and already scaled -- once.
	reflection *= water.iblIntensity;

	// Where the SSR march landed on something the screen can see, that wins: it is the
	// island in the lake, which the cube -- baked at the origin with no terrain in it --
	// never had. The alpha already carries the edge fade and the strength control, so a
	// miss is exactly the cube and everything between is a crossfade.
	const float4 ssr = ssrReflection.SampleLevel(linearClamp, input.uv, 0.0f);
	reflection = lerp(reflection, ssr.rgb, ssr.a);

	// The sun, off the same surface. A Blinn lobe rather than a full GGX: this is one light
	// on one flat material, and the lobe is the only part of it anyone can see.
	const float3 toSun = normalize(-water.lightDirection);
	const float3 halfVector = normalize(toSun + viewDirection);

	// Blinn exponent from the roughness, capped. Uncapped, water's roughness puts it in the
	// hundreds of thousands, and a lobe that narrow is a highlight no pixel centre ever
	// lands in -- the sun simply does not appear. What makes a real sun path on water wide
	// is not the material, it is the waves tilting the surface underneath it, and those are
	// in the normal already.
	const float alpha = max(water.roughness * water.roughness, 1e-3f);
	const float shininess = min(2.0f / (alpha * alpha), 8192.0f);

	const float specular = pow(saturate(dot(normal, halfVector)), shininess) * saturate(toSun.y);

	// What the water itself sends back: the light that got in, scattered, and came out
	// again. Scaled by what did not survive the trip, so shallow water is the bottom and
	// deep water is this.
	const float3 scattered = water.scatterColor * (1.0f.xxx - absorption) * water.iblIntensity;

	// How much of the pixel the water accounts for. Two things make it opaque: depth, and
	// the reflection at a grazing angle. The shore fade is what keeps the waterline from
	// being a hard cut across the sand.
	const float bodyOpacity = saturate(pathLength / max(water.shoreFade, 0.01f));
	const float opacity = saturate(max(bodyOpacity * (1.0f - dot(absorption, 0.3333f.xxx)), fresnel));

	// Premultiplied, so the blend is one multiply-add and the sun glint is not scaled down
	// by an opacity it does not have: a highlight sits on the surface whether the water
	// under it is a metre deep or a hundred.
	float3 color = lerp(scattered, reflection, fresnel) * opacity;

	color += (water.sunIntensity * specular * water.specularStrength * fresnel).xxx;

	return float4(color, opacity);
}
