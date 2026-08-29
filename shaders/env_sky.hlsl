// Level 0 of the environment cube: the sky itself. Roughness zero is a mirror, so no
// filtering applies here.
//
// The cloud layer is marched into it too. Without that the cube is a clear sky, and every
// surface in the scene is lit and reflects as though the clouds overhead were not there --
// the one place the omission shows worst is a wet or metallic surface, which mirrors a blue
// sky back at a viewer who can plainly see it is overcast. Because the nine irradiance
// coefficients and the prefiltered chain are both derived from this level, marching here is
// also the only place it has to be done: the ambient term and the reflections both follow.
//
// The march is coarser than the view one -- fewer steps, no detail volume -- which costs
// nothing visible. Every texel of this is about to be blurred into a mip chain and summed
// into nine coefficients.
//
// Also writes the radiance to a plain buffer. The nine spherical-harmonic coefficients are
// a sum over every texel of every face weighted by solid angle, and they have to end up in
// the scene constant buffer on the CPU -- a two-stage GPU reduction and a readback to
// deliver twenty-seven floats is more machinery than the projection itself costs.

#define MV_CUSTOM_PUSH_CONSTANTS
#include "common.hlsli"
#include "env.hlsli"
#include "clouds.hlsli"

// SkyConstants plus what the cloud march needs, which together are exactly the hundred and
// twenty-eight bytes a push constant block is guaranteed. That is why the lighting fields
// here are a hand-picked few rather than the whole of CloudConstants: the layer's geometry
// comes in as a CloudFieldConstants, and everything about a camera, a screen or a depth
// buffer is left behind.
struct SkyCloudConstants
{
	SkyConstants sky;

	CloudFieldConstants field;

	float3 cloudSunColor;
	float  cloudAmbient;

	float  forwardScattering;
	float  backwardScattering;
	float  scatterBlend;
	// Zero marches nothing, which is what a frame with the clouds switched off passes.
	uint   cloudSteps;
};

#ifdef MV_TARGET_VULKAN
[[vk::push_constant]] SkyCloudConstants constants;
#else
ConstantBuffer<SkyCloudConstants> constants : register(b0, space9);
#endif

#ifdef MV_TARGET_VULKAN
[[vk::image_format("rgba16f")]] RWTexture2DArray<float4> cubeFaces : register(u0, space0);
#else
RWTexture2DArray<float4> cubeFaces : register(u0, space0);
#endif

// Face-major, matching the layout the CPU projection walks.
RWStructuredBuffer<float3> radiance : register(u1, space0);

Texture3D<float4> mvCloudShape   : register(t2, space0);
Texture3D<float4> mvCloudDetail  : register(t3, space0);
Texture2D<float4> mvCloudWeather : register(t4, space0);

SamplerState mvCloudRepeat : register(s5, space0);

#include "cloud_density.hlsli"

// The cloud layer along one direction, composited over the sky already computed for it.
//
// The origin is on the ground rather than at the camera. A cube baked from the camera would
// have to be rebaked as it moves, and at this layer's scale -- kilometres up, tens of
// kilometres across -- the few hundred metres the camera ever climbs changes nothing the
// nine coefficients could show.
float3 mvCloudsOverSky(float3 direction, float3 skyColor)
{
	if (constants.cloudSteps == 0)
		return skyColor;

	const float3 origin = float3(0.0f, constants.field.planetRadius, 0.0f);

	// The planet is in the way for everything below the horizon. Testing it is not optional:
	// standing inside both shells, the far root of either sphere is positive for *every*
	// direction, including straight down, so without this a downward ray marches out the
	// other side of the world and comes back with the clouds it found there. The lower half
	// of the cube then fills with cloud, which is both wrong and, since the ground radiance
	// it replaces is what bounces light back up, wrong in a way the irradiance carries.
	if (mvRaySphereNear(origin, direction, constants.field.planetRadius) > 0.0f)
		return skyColor;

	const float innerRadius = constants.field.planetRadius + constants.field.layerBottom;
	const float outerRadius = constants.field.planetRadius + constants.field.layerTop;

	// Standing under the layer, so the shell is entered at its bottom and left at its top.
	const float marchStart = mvRaySphereFar(origin, direction, innerRadius);
	const float marchEnd = mvRaySphereFar(origin, direction, outerRadius);

	if (marchStart <= 0.0f || marchEnd <= marchStart)
		return skyColor;

	const float3 toSun = normalize(-constants.sky.lightDirection);
	const float cosAngle = dot(direction, toSun);

	const float phase = lerp(
		mvHenyeyGreenstein(cosAngle, constants.forwardScattering),
		mvHenyeyGreenstein(cosAngle, -constants.backwardScattering),
		constants.scatterBlend);

	const float stepLength = (marchEnd - marchStart) / float(constants.cloudSteps);

	float3 scattering = 0.0f.xxx;
	float transmittance = 1.0f;

	float travelled = marchStart + stepLength * 0.5f;

	for (uint i = 0; i < constants.cloudSteps; i++)
	{
		if (transmittance < 0.01f)
			break;

		const float3 position = origin + direction * travelled;

		float heightFraction;
		const float density = mvCloudDensity(constants.field, position, false, heightFraction);

		if (density > 0.0f)
		{
			// Two light steps rather than the view march's six. The result is a mip chain
			// and nine coefficients; the difference between a well-shaded cloud and a
			// roughly shaded one does not survive either.
			const float sunTransmittance = mvCloudSunTransmittance(constants.field, position, toSun, 2);

			const float3 ambient = constants.cloudSunColor * constants.cloudAmbient
				* lerp(0.35f, 1.0f, heightFraction);

			const float3 luminance = constants.cloudSunColor * sunTransmittance * phase + ambient;

			const float stepExtinction = max(density * constants.field.extinction, 1e-6f);
			const float stepTransmittance = exp(-stepExtinction * stepLength);

			scattering += transmittance * luminance * (1.0f - stepTransmittance);
			transmittance *= stepTransmittance;
		}

		travelled += stepLength;
	}

	return skyColor * transmittance + scattering;
}

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
	if (id.x >= constants.sky.faceSize || id.y >= constants.sky.faceSize)
		return;

	const uint face = id.z;

	// Face coordinates run [-1, 1] with texel centres at half-texel offsets.
	const float u = ((float(id.x) + 0.5f) / float(constants.sky.faceSize)) * 2.0f - 1.0f;
	const float v = ((float(id.y) + 0.5f) / float(constants.sky.faceSize)) * 2.0f - 1.0f;

	const float3 direction = normalize(mvFaceDirection(face, u, v));

	const float3 color = mvCloudsOverSky(direction, mvSkyRadiance(constants.sky, direction));

	cubeFaces[uint3(id.xy, face)] = float4(color, 1.0f);

	radiance[(face * constants.sky.faceSize + id.y) * constants.sky.faceSize + id.x] = color;
}
