#ifndef _MV_NOISE_HLSLI_
#define _MV_NOISE_HLSLI_

// The GPU twin of util/noise.cpp.
//
// Kept deliberately line-for-line with the C++ so the two can be compared: the CPU version
// has not gone away, because the terrain's ground-height query and anything else that needs
// a value without a round trip still needs it. Any divergence between the two shows up as
// the camera walking through the ground, which is a good test.
//
// The basis and fractal selectors arrive as uints rather than being specialised per
// permutation. Sixteen pipelines to avoid a branch that every thread in a group takes the
// same way is not a trade worth making.

#define MV_NOISE_BASIS_VALUE   0
#define MV_NOISE_BASIS_PERLIN  1
#define MV_NOISE_BASIS_SIMPLEX 2
#define MV_NOISE_BASIS_WORLEY  3

#define MV_NOISE_FRACTAL_NONE   0
#define MV_NOISE_FRACTAL_FBM    1
#define MV_NOISE_FRACTAL_RIDGED 2
#define MV_NOISE_FRACTAL_BILLOW 3

// Must match NoiseDesc in util/noise.h, field for field.
struct NoiseParams
{
	uint basis;
	uint fractal;
	float frequency;
	uint octaves;

	float lacunarity;
	float gain;
	uint seed;
	float warpStrength;

	float warpFrequency;
	uint tileable;
	uint2 _noisePad;
};

// A bit-mixer, not a cryptographic hash: it only has to decorrelate neighbouring integers.
uint mvNoiseMix(uint x)
{
	x ^= x >> 16;
	x *= 0x7feb352du;
	x ^= x >> 15;
	x *= 0x846ca68bu;
	x ^= x >> 16;

	return x;
}

uint mvHashLattice(int x, int y, uint seed)
{
	return mvNoiseMix(uint(x) * 0x27d4eb2du ^ uint(y) * 0x165667b1u ^ seed);
}

float mvHashToUnit(uint h)
{
	return float(h >> 8) * (1.0f / 16777216.0f);
}

// Quintic: the second derivative vanishes at the lattice points as well as the first, so
// neighbouring cells meet without the creases a cubic leaves in a derived normal map.
float mvSmoothstepQuintic(float t)
{
	return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

// Wraps a lattice coordinate into [0, period) when tiling is on.
int mvWrapLattice(int v, int period)
{
	if (period <= 0)
		return v;

	const int m = v % period;

	return (m < 0) ? m + period : m;
}

float mvValueNoise(float x, float y, uint seed, int period)
{
	const int xi = int(floor(x));
	const int yi = int(floor(y));

	const float tx = mvSmoothstepQuintic(x - float(xi));
	const float ty = mvSmoothstepQuintic(y - float(yi));

	const int x0 = mvWrapLattice(xi, period);
	const int y0 = mvWrapLattice(yi, period);
	const int x1 = mvWrapLattice(xi + 1, period);
	const int y1 = mvWrapLattice(yi + 1, period);

	const float v00 = mvHashToUnit(mvHashLattice(x0, y0, seed));
	const float v10 = mvHashToUnit(mvHashLattice(x1, y0, seed));
	const float v01 = mvHashToUnit(mvHashLattice(x0, y1, seed));
	const float v11 = mvHashToUnit(mvHashLattice(x1, y1, seed));

	const float value = lerp(lerp(v00, v10, tx), lerp(v01, v11, tx), ty);

	return value * 2.0f - 1.0f;
}

// One of sixteen evenly spaced directions.
void mvGradient(uint h, out float gx, out float gy)
{
	const uint index = h >> 28;
	const float angle = float(index) * (6.28318530718f / 16.0f);

	gx = cos(angle);
	gy = sin(angle);
}

float mvPerlinNoise(float x, float y, uint seed, int period)
{
	const int xi = int(floor(x));
	const int yi = int(floor(y));

	const float fx = x - float(xi);
	const float fy = y - float(yi);

	const float tx = mvSmoothstepQuintic(fx);
	const float ty = mvSmoothstepQuintic(fy);

	const int x0 = mvWrapLattice(xi, period);
	const int y0 = mvWrapLattice(yi, period);
	const int x1 = mvWrapLattice(xi + 1, period);
	const int y1 = mvWrapLattice(yi + 1, period);

	float gx, gy;

	mvGradient(mvHashLattice(x0, y0, seed), gx, gy);
	const float d00 = gx * fx + gy * fy;

	mvGradient(mvHashLattice(x1, y0, seed), gx, gy);
	const float d10 = gx * (fx - 1.0f) + gy * fy;

	mvGradient(mvHashLattice(x0, y1, seed), gx, gy);
	const float d01 = gx * fx + gy * (fy - 1.0f);

	mvGradient(mvHashLattice(x1, y1, seed), gx, gy);
	const float d11 = gx * (fx - 1.0f) + gy * (fy - 1.0f);

	const float value = lerp(lerp(d00, d10, tx), lerp(d01, d11, tx), ty);

	// 2D Perlin peaks at 1/sqrt(2) with unit gradients.
	return value * 1.41421356f;
}

float mvSimplexNoise(float x, float y, uint seed)
{
	// Skew the square lattice into the simplex one and back.
	const float kF2 = 0.36602540378f;
	const float kG2 = 0.21132486540f;

	const float s = (x + y) * kF2;
	const int i = int(floor(x + s));
	const int j = int(floor(y + s));

	const float t = float(i + j) * kG2;

	const float x0 = x - (float(i) - t);
	const float y0 = y - (float(j) - t);

	const int i1 = (x0 > y0) ? 1 : 0;
	const int j1 = (x0 > y0) ? 0 : 1;

	const float x1 = x0 - float(i1) + kG2;
	const float y1 = y0 - float(j1) + kG2;
	const float x2 = x0 - 1.0f + 2.0f * kG2;
	const float y2 = y0 - 1.0f + 2.0f * kG2;

	const int cornerI[3] = { i, i + i1, i + 1 };
	const int cornerJ[3] = { j, j + j1, j + 1 };
	const float cornerX[3] = { x0, x1, x2 };
	const float cornerY[3] = { y0, y1, y2 };

	float total = 0.0f;

	[unroll]
	for (int c = 0; c < 3; c++)
	{
		float falloff = 0.5f - cornerX[c] * cornerX[c] - cornerY[c] * cornerY[c];

		if (falloff <= 0.0f)
			continue;

		float gx, gy;
		mvGradient(mvHashLattice(cornerI[c], cornerJ[c], seed), gx, gy);

		falloff *= falloff;
		total += falloff * falloff * (gx * cornerX[c] + gy * cornerY[c]);
	}

	return total * 70.0f;
}

float mvWorleyNoise(float x, float y, uint seed)
{
	const int xi = int(floor(x));
	const int yi = int(floor(y));

	float nearest = 3.402823466e+38f;

	[unroll]
	for (int dy = -1; dy <= 1; dy++)
	{
		[unroll]
		for (int dx = -1; dx <= 1; dx++)
		{
			const int cx = xi + dx;
			const int cy = yi + dy;

			const uint h = mvHashLattice(cx, cy, seed);

			const float px = float(cx) + mvHashToUnit(h);
			const float py = float(cy) + mvHashToUnit(mvNoiseMix(h));

			const float ox = px - x;
			const float oy = py - y;

			nearest = min(nearest, ox * ox + oy * oy);
		}
	}

	return sqrt(nearest) * 2.0f - 1.0f;
}

float mvEvaluateBasis(uint basis, float x, float y, uint seed, int period)
{
	switch (basis)
	{
	case MV_NOISE_BASIS_VALUE:   return mvValueNoise(x, y, seed, period);
	case MV_NOISE_BASIS_SIMPLEX: return mvSimplexNoise(x, y, seed);
	case MV_NOISE_BASIS_WORLEY:  return mvWorleyNoise(x, y, seed);
	default:                     return mvPerlinNoise(x, y, seed, period);
	}
}

bool mvBasisTiles(uint basis)
{
	return basis == MV_NOISE_BASIS_VALUE || basis == MV_NOISE_BASIS_PERLIN;
}

float mvFractal(NoiseParams p, float x, float y)
{
	const uint octaves = max(1u, p.octaves);

	float frequency = p.frequency;
	float amplitude = 1.0f;
	float total = 0.0f;
	float normalisation = 0.0f;

	const bool tile = (p.tileable != 0) && mvBasisTiles(p.basis);

	for (uint octave = 0; octave < octaves; octave++)
	{
		// The period has to be an integer for the lattice to meet itself, so the octave's
		// frequency is rounded to one and used for both.
		const int period = tile ? max(1, int(round(frequency))) : 0;
		const float octaveFrequency = tile ? float(period) : frequency;

		const uint octaveSeed = p.seed + octave * 0x9e3779b9u;

		float value = mvEvaluateBasis(p.basis, x * octaveFrequency, y * octaveFrequency, octaveSeed, period);

		if (p.fractal == MV_NOISE_FRACTAL_RIDGED)
		{
			value = 1.0f - abs(value);
			value *= value;
		}
		else if (p.fractal == MV_NOISE_FRACTAL_BILLOW)
		{
			value = abs(value) * 2.0f - 1.0f;
		}

		total += value * amplitude;
		normalisation += amplitude;

		if (p.fractal == MV_NOISE_FRACTAL_NONE)
			break;

		frequency *= p.lacunarity;
		amplitude *= p.gain;
	}

	return (normalisation > 0.0f) ? total / normalisation : total;
}

float mvNoiseSample(NoiseParams p, float x, float y)
{
	if (p.warpStrength <= 0.0f)
		return mvFractal(p, x, y);

	// The warp field is the same basis at a lower frequency, offset seeds apart so the two
	// axes are independent.
	NoiseParams warp = p;
	warp.fractal = MV_NOISE_FRACTAL_FBM;
	warp.octaves = 2;
	warp.frequency = p.warpFrequency;
	warp.warpStrength = 0.0f;

	warp.seed = p.seed + 0x51ed2701u;
	const float offsetX = mvFractal(warp, x, y);

	warp.seed = p.seed + 0x1b56c4e9u;
	const float offsetY = mvFractal(warp, x, y);

	return mvFractal(p, x + offsetX * p.warpStrength, y + offsetY * p.warpStrength);
}

#endif
