// The cloud shadow map: how much sunlight survives the layer, as a shadow map -- a 2D
// texture parameterised in light space, on a plane perpendicular to the sun.
//
// Not indexed by world XZ. A top-down map answers "what does the layer let through onto
// this piece of ground", which is only right for receivers at ground level: a peak three
// hundred metres up, or the piece of air the fog march samples two kilometres out, sits on
// a *different* sun ray than the ground below it, and looking its shadow up by XZ slides
// the answer sideways by height / tan(elevation). Parameterising along the light instead
// gives every point on the same sun ray the same texel -- which is the entire idea of a
// shadow map, and why the lookup is exact at any altitude below the layer.
//
// Baked every frame rather than every bake: the wind slides the layer continuously, and a
// shadow that only moved when the volumes were rebaked would be a shadow painted on the
// ground. One dispatch with the detail volume off, which is cheap enough that the
// alternative -- marching towards the sun per shaded pixel -- is not worth considering.
//
// The plane follows the camera, snapped to a texel in light space so following it does not
// make the shadows crawl.

#define MV_CUSTOM_PUSH_CONSTANTS
#include "common.hlsli"
#include "clouds.hlsli"

// Must match ShadowGpuConstants in cloud_renderer.cpp.
struct CloudShadowConstants
{
	CloudFieldConstants field;

	// The direction light travels, matching the scene's.
	float3 lightDirection;
	float  mapExtent;

	// The light-space frame: two axes perpendicular to the sun, and the world point the
	// map is centred on. A receiver's texel is its offset from the origin projected onto
	// these axes -- the same transform the shading side applies.
	float3 planeRight;
	uint   mapSize;

	float3 planeUp;
	uint   steps;

	float3 planeOrigin;
	float  _shadowPad;
};

#ifdef MV_TARGET_VULKAN
[[vk::push_constant]] CloudShadowConstants shadow;
#else
ConstantBuffer<CloudShadowConstants> shadow : register(b0, space9);
#endif

Texture3D<float4> mvCloudShape   : register(t0, space0);
Texture3D<float4> mvCloudDetail  : register(t1, space0);
Texture2D<float4> mvCloudWeather : register(t2, space0);

SamplerState mvCloudRepeat : register(s3, space0);

#ifdef MV_TARGET_VULKAN
[[vk::image_format("rgba8")]] RWTexture2D<float4> target : register(u4, space0);
#else
RWTexture2D<float4> target : register(u4, space0);
#endif

#include "cloud_density.hlsli"

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
	if (id.x >= shadow.mapSize || id.y >= shadow.mapSize)
		return;

	const float2 uv = (float2(id.xy) + 0.5f) / float(shadow.mapSize);

	const float3 toSun = normalize(-shadow.lightDirection);

	// With the sun at or below the horizon there is no direct light left to occlude, and
	// the ray towards it would leave through the bottom of the shell rather than the top.
	if (toSun.y <= 0.01f)
	{
		target[id.xy] = 1.0f.xxxx;
		return;
	}

	// The world point this texel stands for: somewhere on the light-space plane.
	const float3 planePoint = shadow.planeOrigin
		+ shadow.planeRight * ((uv.x - 0.5f) * shadow.mapExtent)
		+ shadow.planeUp * ((uv.y - 0.5f) * shadow.mapExtent);

	// Slide it down its own sun ray to ground level before marching. Below the layer the
	// transmittance along one ray is the same from anywhere -- there is no cloud down
	// there to change it -- and starting from a canonical height keeps a texel from
	// answering with a partial layer just because the tilted plane happened to cross it
	// halfway up.
	const float3 groundPoint = planePoint - toSun * (planePoint.y / toSun.y);

	// Planet-centred, so the shell is two spheres and altitude is just a length. The
	// terrain's own relief is small against a layer this high; taking the ground as flat
	// costs a fraction of a per cent of the path and saves the pass a heightmap binding.
	const float3 origin = float3(groundPoint.x, shadow.field.planetRadius, groundPoint.z);

	const float transmittance = mvCloudBeamTransmittance(shadow.field, origin, toSun, shadow.steps);

	target[id.xy] = transmittance.xxxx;
}
