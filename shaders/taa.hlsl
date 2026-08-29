
// Temporal anti-aliasing.
//
// The camera is jittered by a sub-pixel offset each frame, so successive frames sample
// the same surface at different points inside the pixel. Blending each frame into an
// accumulated history converges on the supersampled result.
//
// Finding the right history pixel is the whole problem. Every mesh in this scene is
// static with its transform baked into the vertices, so the camera is the only thing that
// moves and the depth buffer alone locates a pixel in the previous frame: unproject it to
// a world position, then project that with last frame's matrix. Anything animated would
// need velocities written out by the geometry pass instead.
//
// Where that lands on a surface which was hidden last frame, the history belongs to
// something else entirely, and blending it in is what produces trailing smears. The
// neighbourhood clamp is the defence: the history is pulled into the range of colours
// actually present around the current pixel, so a sample from a different surface is
// rejected rather than accumulated.

#include "post.hlsli"

float4 PSMain(PostVSOutput input) : SV_TARGET
{
	float blendFactor = effectConstants.params0.x;
	float clampScale = effectConstants.params0.y;
	bool historyValid = effectConstants.params0.z > 0.5f;

	float3 current = postTexture0.SampleLevel(postSampler, input.uv, 0.0f).rgb;

	// Nothing to blend with on the first frame, or after the chain was interrupted.
	if (!historyValid)
		return float4(current, 1.0f);

	float2 texel = 1.0f / viewportSize;

	// The 3x3 neighbourhood, used both to find the range the history is allowed to occupy
	// and to steady the current sample.
	float3 minColor = current;
	float3 maxColor = current;
	float3 sum = current;
	float3 sumSquares = current * current;

	[unroll]
	for (int y = -1; y <= 1; y++)
	{
		[unroll]
		for (int x = -1; x <= 1; x++)
		{
			if (x == 0 && y == 0)
				continue;

			float3 neighbour = postTexture0.SampleLevel(postSampler, input.uv + float2(x, y) * texel, 0.0f).rgb;

			minColor = min(minColor, neighbour);
			maxColor = max(maxColor, neighbour);

			sum += neighbour;
			sumSquares += neighbour * neighbour;
		}
	}

	// A box drawn from the mean and spread rather than the hard extremes. The hard box is
	// too generous wherever one bright neighbour widens it, and too tight on a gradient.
	float3 mean = sum / 9.0f;
	float3 variance = max(0.0f.xxx, sumSquares / 9.0f - mean * mean);
	float3 deviation = sqrt(variance) * clampScale;

	minColor = max(minColor, mean - deviation);
	maxColor = min(maxColor, mean + deviation);

	// Where this surface was last frame, read from the velocity the geometry pass wrote.
	//
	// This used to unproject the depth buffer and reproject the world position with the
	// previous camera, which is exact only while nothing moves but the camera. Velocity
	// answers the question the temporal pass is actually asking -- where did this surface
	// go -- rather than where a point in space went, and that is the form that survives
	// anything animated.
	float2 velocity = postTexture2.SampleLevel(postSampler, input.uv, 0.0f).rg;

	float2 previousUv = input.uv - velocity;

	// Off the edge of last frame there is no history at all.
	if (any(previousUv < 0.0f) || any(previousUv > 1.0f))
		return float4(current, 1.0f);

	float3 history = postTexture1.SampleLevel(postSampler, previousUv, 0.0f).rgb;

	// Where the history disagrees with everything around this pixel, it is stale.
	history = clamp(history, minColor, maxColor);

	return float4(lerp(history, current, blendFactor), 1.0f);
}
