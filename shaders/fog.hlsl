// Height fog and the light shafts inside it, in one fullscreen pass.
//
// The fog is an exponential density profile -- pooling in the valleys, thinning with
// altitude -- and fog is also the medium the shafts live in: a sunbeam is nothing but fog
// that can see the sun, and the dark bands beside it are the same fog inside a mountain's
// or a cloud's shadow. So the two are one integral. The pass marches the view ray through
// the profile, and at each step asks the cascade atlas and the cloud shadow map whether
// the sun reaches that piece of air: what it accumulates is the in-scattered sun, and what
// it attenuates by is the fog itself.
//
// The march is short and jittered, and the temporal pass that runs later in the chain is
// what turns the jitter into smoothness. Beyond the march -- and wherever the shafts are
// switched off -- the profile's closed-form integral supplies the rest of the fog, so
// turning the shafts off returns exactly the analytic fog this pass started as.
//
// It binds the scene set for the cascades and the cloud shadow map, which is why its own
// resources live in space2, exactly as the visibility resolve arranges things.

#define MV_CUSTOM_PUSH_CONSTANTS
#include "common.hlsli"
#include "shadow.hlsli"

// Must match FogGpuConstants in height_fog.cpp.
struct FogConstants
{
	float3 cameraPosition;
	float  density;

	float3 cameraForward;
	float  tanHalfFov;

	float2 viewportSize;
	float  depthLinearA;
	float  depthLinearB;

	// 1 / metres over which the density falls to 1/e of its sea-level value; metres of
	// clear air before the haze starts; the most a pixel may be swallowed; and what the
	// sun is worth once the phase function has had its say.
	float  heightFalloff;
	float  startDistance;
	float  maxOpacity;
	float  sunIntensity;

	// The direction light travels, and the artist scale on the in-scattered sun.
	float3 lightDirection;
	float  shaftIntensity;

	// How far the march reaches, how strongly the scattering favours the sun's direction,
	// and how many steps pay for it.
	float  shaftDistance;
	float  shaftAnisotropy;
	uint   shaftSteps;
	float  _fogPad;
};

#ifdef MV_TARGET_VULKAN
[[vk::push_constant]] FogConstants fogc;
#else
ConstantBuffer<FogConstants> fogc : register(b0, space9);
#endif

// space2, because spaces 0 and 1 are the scene and bindless sets this pass borrows for
// the shadow atlas, the cloud shadow map, the environment cube and the sampler presets.
Texture2D<float> sceneDepth : register(t0, space2);

struct FogVSOutput
{
	float4 position : SV_POSITION;
	float2 uv       : TEXCOORD0;
};

FogVSOutput VSMain(uint vertexId : SV_VertexID)
{
	FogVSOutput output;

	output.uv = float2((vertexId << 1) & 2, vertexId & 2);
	output.position = float4(output.uv * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);

	return output;
}

// The closed-form integral of the exponential profile along a ray segment. The guard is
// the limit of a level ray, where the expression is 0/0 and the answer is the density at
// the segment's own height.
float mvFogOpticalDepth(float3 origin, float3 direction, float length_)
{
	const float k = fogc.heightFalloff;
	const float climb = direction.y * length_;

	float heightIntegral = 1.0f;

	if (abs(k * climb) > 1e-3f)
		heightIntegral = (1.0f - exp(-k * climb)) / (k * climb);

	return fogc.density * exp(-k * origin.y) * heightIntegral * length_;
}

float4 PSMain(FogVSOutput input) : SV_TARGET
{
	const float rawDepth = sceneDepth.SampleLevel(samplers[MV_SAMPLER_LINEAR_CLAMP], input.uv, 0.0f);

	const bool isSky = rawDepth >= 1.0f;

	const float3 forward = normalize(fogc.cameraForward);
	const float3 right = normalize(cross(forward, float3(0.0f, 1.0f, 0.0f)));
	const float3 up = cross(right, forward);

	const float aspect = fogc.viewportSize.x / fogc.viewportSize.y;

	const float2 ndc = float2(input.uv.x * 2.0f - 1.0f, 1.0f - input.uv.y * 2.0f);

	const float3 rayDirection = normalize(
		forward +
		right * (ndc.x * fogc.tanHalfFov * aspect) +
		up * (ndc.y * fogc.tanHalfFov));

	// Distance along the ray to the surface. The sky has none, but the air in front of it
	// still does: shafts fan out across the sky behind a ridge, so the march runs there
	// too -- only the fog amount stays zero, because the sky's haze is the atmosphere
	// model's own business.
	float surfaceDistance = 1e9f;

	if (!isSky)
	{
		const float viewZ = fogc.depthLinearB / (rawDepth + fogc.depthLinearA);
		surfaceDistance = viewZ / max(dot(rayDirection, forward), 1e-4f);
	}

	const float marchStart = fogc.startDistance;
	const float marchEnd = min(surfaceDistance, fogc.shaftDistance);

	float transmittance = 1.0f;
	float3 shaft = 0.0f.xxx;

	// The fraction of the fogged air along this ray that the sun actually reaches,
	// weighted the same way the eye weighs it: by density, and by how much of the view
	// the nearer fog has already taken.
	float litFraction = 1.0f;

	if (fogc.shaftIntensity > 0.0f && fogc.shaftSteps > 0u && marchEnd > marchStart)
	{
		const float stepLength = (marchEnd - marchStart) / float(fogc.shaftSteps);

		// A per-pixel offset so the step boundaries do not line up into bands. The
		// temporal pass later in the chain averages what is left of them.
		const float jitter = frac(sin(dot(input.position.xy, float2(12.9898f, 78.233f))) * 43758.5453f);

		const float3 toSun = normalize(-fogc.lightDirection);

		// Henyey-Greenstein, the same convention the clouds use: no 1/4pi. What matters
		// here is that in this convention the phase averages to exactly one over the
		// sphere -- so everything above one is the forward lobe and nothing else.
		//
		// Only that excess is added. The fog's own colour comes from the sky, and the sky
		// IS in-scattered sunlight: adding the full phase on top of it counted the sun
		// twice, and because sunlit air is most of the sky on a clear day, the double
		// count showed up as a white veil over the whole frame rather than as shafts.
		// What the sun uniquely contributes beyond the sky's average is the glow that
		// hangs around its own direction -- the forward excess -- and the *absence* of
		// light in the shadowed bands, which litFraction below carries.
		const float g = fogc.shaftAnisotropy;
		const float cosAngle = dot(rayDirection, toSun);
		const float phase = (1.0f - g * g) / pow(max(1.0f + g * g - 2.0f * g * cosAngle, 1e-4f), 1.5f);

		const float forward = max(phase - 1.0f, 0.0f);

		float visibleDepth = 0.0f;
		float totalDepth = 0.0f;

		float travelled = marchStart + stepLength * jitter;

		for (uint i = 0; i < fogc.shaftSteps; i++)
		{
			const float3 position = fogc.cameraPosition + rayDirection * travelled;

			// This step's slice of the fog integral, and whether the sun reaches it. The
			// two shadow sources multiply: air under both a ridge and a cloud is darker
			// than under either.
			const float stepDepth =
				fogc.density * exp(-fogc.heightFalloff * position.y) * stepLength;

			const float visibility = shadowVisibilityAt(position) * cloudShadowFactor(position);

			visibleDepth += transmittance * visibility * stepDepth;
			totalDepth += transmittance * stepDepth;

			transmittance *= exp(-stepDepth);

			travelled += stepLength;
		}

		// The forward glow: sunlit fog only, and only in the lobe around the sun.
		shaft = (fogc.sunIntensity * fogc.shaftIntensity * forward * visibleDepth).xxx;

		if (totalDepth > 1e-6f)
			litFraction = visibleDepth / totalDepth;
	}
	else if (marchEnd > marchStart)
	{
		// Shafts off: the whole marched span collapses back into the closed form.
		const float3 origin = fogc.cameraPosition + rayDirection * marchStart;

		transmittance = exp(-mvFogOpticalDepth(origin, rayDirection, marchEnd - marchStart));
	}

	// The fog past the march, in closed form: from where the march gave up to the surface.
	if (!isSky && surfaceDistance > marchEnd)
	{
		const float3 origin = fogc.cameraPosition + rayDirection * marchEnd;

		transmittance *= exp(-mvFogOpticalDepth(origin, rayDirection, surfaceDistance - marchEnd));
	}

	const float amount = isSky ? 0.0f : min(1.0f - transmittance, fogc.maxOpacity);

	if (amount <= 0.0f && all(shaft <= 0.0f))
		discard;

	// The sky in this ray's horizontal direction, held a touch above the horizon so the
	// blurred mip's footprint never dips into the cube's below-horizon ground.
	const float3 horizontal = normalize(float3(rayDirection.x, 0.06f, rayDirection.z));

	// Shadowed air keeps only part of the sky's colour: the sky sample stands for fog in
	// full sun, and fog inside a mountain's or a cloud's shadow is lit by the rest of the
	// sky alone. The floor is not zero -- shadowed air still sees the whole hemisphere --
	// and this darkening, not the additive glow, is what draws the visible bands.
	const float3 fogColor =
		environmentMap.SampleLevel(samplers[MV_SAMPLER_LINEAR_CLAMP], horizontal, 2.0f).rgb
		* lerp(0.35f, 1.0f, litFraction);

	// Premultiplied over, with the shafts riding additively on top: the blend gives
	// scene * (1 - amount) + fogColor * amount + shaft.
	return float4(fogColor * amount + shaft, amount);
}
