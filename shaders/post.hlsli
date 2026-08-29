
// Shared declarations for the post-process chain.
//
// Set 2 is the effect's own resources: up to three textures and one sampler. Three
// because the effects worth having are the ones that need more than the chain input --
// temporal anti-aliasing reads its history and the depth buffer, bloom reads the level it
// is adding into as well as the one it is adding.

#ifndef _MV_POST_HLSLI_
#define _MV_POST_HLSLI_

#define MV_CUSTOM_PUSH_CONSTANTS
#include "common.hlsli"

Texture2D    postTexture0 : register(t0, space2);
Texture2D    postTexture1 : register(t1, space2);
Texture2D    postTexture2 : register(t2, space2);
SamplerState postSampler  : register(s3, space2);

// Every effect's parameters arrive here. A push constant rather than a buffer, so an
// effect needs no per-frame storage and nothing has to be double buffered.
struct EffectConstants
{
	float4 params0;
	float4 params1;
	float4 params2;
	float4 params3;
};

#ifdef MV_TARGET_VULKAN
[[vk::push_constant]] EffectConstants effectConstants;
#else
ConstantBuffer<EffectConstants> effectConstants : register(b0, space9);
#endif

struct PostVSOutput
{
	float4 position : SV_POSITION;
	float2 uv       : TEXCOORD0;
};

// One oversized triangle rather than a quad: no diagonal seam, and the whole screen is
// covered by three vertices with no vertex buffer. Defined here so every effect file
// carries an identical copy, which is what the build step expects -- it compiles both a
// VSMain and a PSMain out of each .hlsl.
PostVSOutput VSMain(uint vertexId : SV_VertexID)
{
	PostVSOutput output;

	output.uv = float2((vertexId << 1) & 2, vertexId & 2);
	output.position = float4(output.uv * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);

	return output;
}

float luminance(float3 color)
{
	return dot(color, float3(0.2126f, 0.7152f, 0.0722f));
}

#endif
