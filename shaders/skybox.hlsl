
// Skybox: one oversized triangle, with the view ray reconstructed from the camera basis
// rather than from an inverse projection matrix.
//
// It runs before the scene so that anything drawn afterwards simply covers it. That costs
// a screen's worth of overdraw, but it means neither the forward path nor the visibility
// buffer resolve has to know the sky exists: the resolve already discards where no
// geometry was written, and the sky is what shows through.

#include "common.hlsli"
#include "pbr.hlsli"

struct VSOutput
{
	float4 position : SV_POSITION;
	float2 ndc      : TEXCOORD0;
};

VSOutput VSMain(uint vertexId : SV_VertexID)
{
	VSOutput output;

	float2 uv = float2((vertexId << 1) & 2, vertexId & 2);
	output.position = float4(uv * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
	output.ndc = output.position.xy;

	return output;
}

struct SkyOutput
{
	float4 color    : SV_TARGET0;
	float2 velocity : SV_TARGET1;
};

SkyOutput PSMain(VSOutput input)
{
	// The camera basis, rebuilt from the forward vector the scene constants already carry.
	// Passing the whole inverse view-projection would be the alternative, and would need a
	// matrix inverse this codebase has no use for anywhere else.
	float3 forward = normalize(cameraForward);
	float3 right = normalize(cross(forward, float3(0.0f, 1.0f, 0.0f)));
	float3 up = cross(right, forward);

	// Matches the 60 degree vertical field of view the projection is built with.
	const float tanHalfFov = 0.5773502692f;
	float aspect = viewportSize.x / viewportSize.y;

	float3 direction = normalize(
		forward +
		right * (input.ndc.x * tanHalfFov * aspect) +
		up * (input.ndc.y * tanHalfFov));

	// Mip 0 is the unfiltered sky. Sampling any lower level here would show the roughness
	// convolution rather than the sky itself.
	float3 radiance = environmentMap.SampleLevel(samplers[MV_SAMPLER_LINEAR_CLAMP], direction, 0.0f).rgb;

	// The sun itself, drawn analytically here rather than baked into the cube: the
	// cube feeds the irradiance and the reflection chain, and a thousand-sun texel
	// in there becomes fireflies in every glossy surface. Here it is only ever a
	// picture of the sun -- and the clouds composite over it afterwards, so an
	// overcast sky still hides it for free.
	//
	// The disc is a touch over the real half degree with a soft limb, and bright
	// enough (hundreds of times the sky) that the bloom pass grows the glare the
	// eye expects; the analytic halo underneath just seeds it.
	float3 sun = float3(0.0f, 0.0f, 0.0f);
	{
		const float3 toSun = -normalize(lightDirection);
		const float cosSun = dot(direction, toSun);

		// A couple of degrees across rather than the physical half degree: the real
		// sun reads as a point through bloom, and what a sky wants is the picture
		// everyone recognises.
		// Bright enough to saturate to a clean white disc, restrained enough that the
		// bloom pass does not flood the whole neighbourhood -- the first attempt fed
		// it forty suns and the glare buried the disc it was meant to flatter.
		const float disc = smoothstep(0.99920f, 0.99976f, cosSun);
		const float halo = pow(saturate(cosSun), 800.0f);

		sun = lightColor * (disc * 8.0f + halo * 0.3f);
	}

	SkyOutput output;

	// Linear radiance, like the shaded path: the chain tone maps both together at the end,
	// so the sky and the geometry in front of it cannot disagree about what a given
	// radiance looks like. The sun rides outside the IBL scale on purpose: its
	// brightness belongs to the light, and dimming the ambient should not put it out.
	output.color = float4(radiance * iblIntensity + sun, 1.0f);

	// The sky is at infinity, so it only moves when the camera turns. Without this the
	// temporal pass would hold it still while the world rotated underneath and smear it.
	output.velocity = computeDirectionVelocity(direction, input.position.xy / viewportSize);

	return output;
}
