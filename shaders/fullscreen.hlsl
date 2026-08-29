// The passthrough. Its VSMain is the one every effect uses; its PSMain copies one texture
// to another, which is what temporal anti-aliasing needs to get its resolved history back
// into the chain without disturbing the copy it must keep until next frame.

#include "post.hlsli"

float4 PSMain(PostVSOutput input) : SV_TARGET
{
	return postTexture0.SampleLevel(postSampler, input.uv, 0.0f);
}
