#include "util/noise.h"

#include "compute/mip_generator.h"
#include "util/parallel.h"

#include "stb_image_resize2.h"

#include <algorithm>
#include <cfloat>
#include <cmath>

namespace mv::noise
{
	namespace
	{
		// A bit-mixer, not a cryptographic hash: it only has to decorrelate neighbouring
		// integers, which is exactly what a lattice needs and all that it needs.
		inline u32 mix(u32 x)
		{
			x ^= x >> 16;
			x *= 0x7feb352du;
			x ^= x >> 15;
			x *= 0x846ca68bu;
			x ^= x >> 16;

			return x;
		}

		inline u32 hashLattice(s32 x, s32 y, u32 seed)
		{
			return mix((u32)x * 0x27d4eb2du ^ (u32)y * 0x165667b1u ^ seed);
		}

		// [0, 1) from the top bits, which are the best mixed.
		inline f32 hashToUnit(u32 h)
		{
			return (f32)(h >> 8) * (1.0f / 16777216.0f);
		}

		// Quintic: the second derivative vanishes at the lattice points as well as the
		// first, so neighbouring cells meet without the creases that a cubic leaves in a
		// normal map derived from the result.
		inline f32 smoothstepQuintic(f32 t)
		{
			return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
		}

		inline f32 lerp(f32 a, f32 b, f32 t)
		{
			return a + (b - a) * t;
		}

		// Wraps a lattice coordinate into [0, period) when tiling is on. period is the
		// octave's frequency, so each octave wraps at its own scale and the whole stack
		// tiles at the unit domain.
		inline s32 wrap(s32 v, s32 period)
		{
			if (period <= 0)
				return v;

			const s32 m = v % period;

			return (m < 0) ? m + period : m;
		}

		f32 valueNoise(f32 x, f32 y, u32 seed, s32 period)
		{
			const s32 xi = (s32)std::floor(x);
			const s32 yi = (s32)std::floor(y);

			const f32 tx = smoothstepQuintic(x - (f32)xi);
			const f32 ty = smoothstepQuintic(y - (f32)yi);

			const s32 x0 = wrap(xi, period);
			const s32 y0 = wrap(yi, period);
			const s32 x1 = wrap(xi + 1, period);
			const s32 y1 = wrap(yi + 1, period);

			const f32 v00 = hashToUnit(hashLattice(x0, y0, seed));
			const f32 v10 = hashToUnit(hashLattice(x1, y0, seed));
			const f32 v01 = hashToUnit(hashLattice(x0, y1, seed));
			const f32 v11 = hashToUnit(hashLattice(x1, y1, seed));

			const f32 value = lerp(lerp(v00, v10, tx), lerp(v01, v11, tx), ty);

			return value * 2.0f - 1.0f;
		}

		// One of sixteen evenly spaced directions. A table this small is enough in 2D, and
		// unit-length gradients keep the output range predictable.
		inline void gradient(u32 h, f32& gx, f32& gy)
		{
			const u32 index = h >> 28;
			const f32 angle = (f32)index * (6.28318530718f / 16.0f);

			gx = std::cos(angle);
			gy = std::sin(angle);
		}

		f32 perlinNoise(f32 x, f32 y, u32 seed, s32 period)
		{
			const s32 xi = (s32)std::floor(x);
			const s32 yi = (s32)std::floor(y);

			const f32 fx = x - (f32)xi;
			const f32 fy = y - (f32)yi;

			const f32 tx = smoothstepQuintic(fx);
			const f32 ty = smoothstepQuintic(fy);

			const s32 x0 = wrap(xi, period);
			const s32 y0 = wrap(yi, period);
			const s32 x1 = wrap(xi + 1, period);
			const s32 y1 = wrap(yi + 1, period);

			f32 gx = 0.0f;
			f32 gy = 0.0f;

			gradient(hashLattice(x0, y0, seed), gx, gy);
			const f32 d00 = gx * fx + gy * fy;

			gradient(hashLattice(x1, y0, seed), gx, gy);
			const f32 d10 = gx * (fx - 1.0f) + gy * fy;

			gradient(hashLattice(x0, y1, seed), gx, gy);
			const f32 d01 = gx * fx + gy * (fy - 1.0f);

			gradient(hashLattice(x1, y1, seed), gx, gy);
			const f32 d11 = gx * (fx - 1.0f) + gy * (fy - 1.0f);

			const f32 value = lerp(lerp(d00, d10, tx), lerp(d01, d11, tx), ty);

			// 2D Perlin peaks at 1/sqrt(2) with unit gradients.
			return value * 1.41421356f;
		}

		f32 simplexNoise(f32 x, f32 y, u32 seed)
		{
			// Skew the square lattice into the simplex one and back. The constants are the
			// 2D cases of (sqrt(n + 1) - 1) / n and its inverse.
			constexpr f32 kF2 = 0.36602540378f;
			constexpr f32 kG2 = 0.21132486540f;

			const f32 s = (x + y) * kF2;
			const s32 i = (s32)std::floor(x + s);
			const s32 j = (s32)std::floor(y + s);

			const f32 t = (f32)(i + j) * kG2;

			// Unskewed cell origin, and the offset of the sample from it.
			const f32 x0 = x - ((f32)i - t);
			const f32 y0 = y - ((f32)j - t);

			// Which of the two triangles in the cell the sample landed in.
			const s32 i1 = (x0 > y0) ? 1 : 0;
			const s32 j1 = (x0 > y0) ? 0 : 1;

			const f32 x1 = x0 - (f32)i1 + kG2;
			const f32 y1 = y0 - (f32)j1 + kG2;
			const f32 x2 = x0 - 1.0f + 2.0f * kG2;
			const f32 y2 = y0 - 1.0f + 2.0f * kG2;

			f32 total = 0.0f;

			const s32 cornerI[3] = { i, i + i1, i + 1 };
			const s32 cornerJ[3] = { j, j + j1, j + 1 };
			const f32 cornerX[3] = { x0, x1, x2 };
			const f32 cornerY[3] = { y0, y1, y2 };

			for (u32 c = 0; c < 3; c++)
			{
				// The radial falloff. Corners further than the simplex's inradius
				// contribute nothing, which is what keeps this to three lookups.
				f32 falloff = 0.5f - cornerX[c] * cornerX[c] - cornerY[c] * cornerY[c];

				if (falloff <= 0.0f)
					continue;

				f32 gx = 0.0f;
				f32 gy = 0.0f;
				gradient(hashLattice(cornerI[c], cornerJ[c], seed), gx, gy);

				falloff *= falloff;
				total += falloff * falloff * (gx * cornerX[c] + gy * cornerY[c]);
			}

			// Empirical, and the reason simplex implementations all carry a magic number:
			// the sum of three radial lobes has no closed-form peak.
			return total * 70.0f;
		}

		f32 worleyNoise(f32 x, f32 y, u32 seed)
		{
			const s32 xi = (s32)std::floor(x);
			const s32 yi = (s32)std::floor(y);

			f32 nearest = FLT_MAX;

			// One feature point per cell, so the nearest can only be in the 3x3 around the
			// sample. Jittering within the cell rather than scattering freely is what makes
			// that bound hold.
			for (s32 dy = -1; dy <= 1; dy++)
			{
				for (s32 dx = -1; dx <= 1; dx++)
				{
					const s32 cx = xi + dx;
					const s32 cy = yi + dy;

					const u32 h = hashLattice(cx, cy, seed);

					const f32 px = (f32)cx + hashToUnit(h);
					const f32 py = (f32)cy + hashToUnit(mix(h));

					const f32 ox = px - x;
					const f32 oy = py - y;

					nearest = std::min(nearest, ox * ox + oy * oy);
				}
			}

			// Distances run to about 1.5 cells at the far corners; the scale brings the
			// common range onto [-1, 1] to match the other bases.
			return std::sqrt(nearest) * 2.0f - 1.0f;
		}

		f32 evaluateBasis(const NoiseDesc& desc, f32 x, f32 y, u32 seed, s32 period)
		{
			switch (desc.basis)
			{
			case EBasis::eValue:   return valueNoise(x, y, seed, period);
			case EBasis::eSimplex: return simplexNoise(x, y, seed);
			case EBasis::eWorley:  return worleyNoise(x, y, seed);

			case EBasis::ePerlin:
			default:               return perlinNoise(x, y, seed, period);
			}
		}

		bool basisTiles(EBasis basis)
		{
			return basis == EBasis::eValue || basis == EBasis::ePerlin;
		}

		f32 fractal(const NoiseDesc& desc, f32 x, f32 y)
		{
			const u32 octaves = std::max(1u, desc.octaves);

			f32 frequency = desc.frequency;
			f32 amplitude = 1.0f;
			f32 total = 0.0f;
			f32 normalisation = 0.0f;

			const bool tile = desc.tileable && basisTiles(desc.basis);

			for (u32 octave = 0; octave < octaves; octave++)
			{
				// The period has to be an integer for the lattice to meet itself, so the
				// octave's frequency is rounded to one and used for both.
				const s32 period = tile ? std::max(1, (s32)std::lround(frequency)) : 0;
				const f32 octaveFrequency = tile ? (f32)period : frequency;

				const u32 octaveSeed = desc.seed + octave * 0x9e3779b9u;

				f32 value = evaluateBasis(desc, x * octaveFrequency, y * octaveFrequency, octaveSeed, period);

				switch (desc.fractal)
				{
				case EFractal::eRidged:
					value = 1.0f - std::fabs(value);
					value *= value;
					break;

				case EFractal::eBillow:
					value = std::fabs(value) * 2.0f - 1.0f;
					break;

				default:
					break;
				}

				total += value * amplitude;
				normalisation += amplitude;

				if (desc.fractal == EFractal::eNone)
					break;

				frequency *= desc.lacunarity;
				amplitude *= desc.gain;
			}

			return (normalisation > 0.0f) ? total / normalisation : total;
		}
	}

	f32 sample(const NoiseDesc& desc, f32 x, f32 y)
	{
		if (desc.warpStrength <= 0.0f)
			return fractal(desc, x, y);

		// The warp field is the same basis at a lower frequency, offset seeds apart so the
		// two axes are independent. Reusing the basis keeps the warp in character with what
		// it is warping.
		NoiseDesc warp = desc;
		warp.fractal = EFractal::eFbm;
		warp.octaves = 2;
		warp.frequency = desc.warpFrequency;
		warp.warpStrength = 0.0f;

		warp.seed = desc.seed + 0x51ed2701u;
		const f32 offsetX = fractal(warp, x, y);

		warp.seed = desc.seed + 0x1b56c4e9u;
		const f32 offsetY = fractal(warp, x, y);

		return fractal(desc, x + offsetX * desc.warpStrength, y + offsetY * desc.warpStrength);
	}

	void generateRaw(const NoiseDesc& desc, u32 width, u32 height, f32* out)
	{
		if (width == 0 || height == 0 || out == nullptr)
			return;

		util::parallelFor(height, [&](u32 y)
			{
				const f32 v = ((f32)y + 0.5f) / (f32)height;

				for (u32 x = 0; x < width; x++)
				{
					const f32 u = ((f32)x + 0.5f) / (f32)width;

					out[(size_t)y * width + x] = sample(desc, u, v);
				}
			});
	}

	void generate(const NoiseDesc& desc, u32 width, u32 height, f32* out)
	{
		generateRaw(desc, width, height, out);

		if (width == 0 || height == 0 || out == nullptr)
			return;

		const size_t count = (size_t)width * height;

		f32 lowest = FLT_MAX;
		f32 highest = -FLT_MAX;

		for (size_t i = 0; i < count; i++)
		{
			lowest = std::min(lowest, out[i]);
			highest = std::max(highest, out[i]);
		}

		const f32 range = highest - lowest;

		// A constant field has no range to stretch; flattening it to the middle is the
		// only answer that does not divide by zero.
		if (range <= 1e-6f)
		{
			for (size_t i = 0; i < count; i++)
			{
				out[i] = 0.5f;
			}

			return;
		}

		const f32 scale = 1.0f / range;

		for (size_t i = 0; i < count; i++)
		{
			out[i] = (out[i] - lowest) * scale;
		}
	}

	rhi::TextureHandle createTexture(
		const std::shared_ptr<rhi::IRHI>& rhi,
		const NoiseDesc& desc,
		u32 width,
		u32 height,
		bool srgb,
		compute::MipGenerator* mipGenerator)
	{
		std::vector<f32> values((size_t)width * height);
		generate(desc, width, height, values.data());

		std::vector<u8> pixels((size_t)width * height * 4);

		for (size_t i = 0; i < values.size(); i++)
		{
			const u8 v = (u8)std::clamp((s32)std::lround(values[i] * 255.0f), 0, 255);

			pixels[i * 4 + 0] = v;
			pixels[i * 4 + 1] = v;
			pixels[i * 4 + 2] = v;
			pixels[i * 4 + 3] = 255;
		}

		return createTextureFromPixels(rhi, pixels.data(), width, height, srgb, mipGenerator);
	}

	rhi::TextureHandle createTextureFromPixels(
		const std::shared_ptr<rhi::IRHI>& rhi,
		const u8* pixels,
		u32 width,
		u32 height,
		bool srgb,
		compute::MipGenerator* mipGenerator)
	{
		const bool onGpu = (mipGenerator != nullptr) && mipGenerator->isReady();

		rhi::TextureDesc desc{};
		desc.width = width;
		desc.height = height;
		desc.depth = 1;

		// 0 asks for a full chain. A terrain map is viewed at every scale from underfoot to
		// the horizon, so this is not optional.
		desc.mipLevels = 0;
		desc.format = srgb ? rhi::ETextureFormat::eR8G8B8A8_SRGB : rhi::ETextureFormat::eR8G8B8A8_UNORM;
		desc.usage = rhi::ETextureUsage::eSampled | rhi::ETextureUsage::eTransferDst;

		// Writable only when a shader is going to fill the chain. Asking for it always
		// would make every sRGB texture typeless for no reason.
		if (onGpu)
			desc.usage = desc.usage | rhi::ETextureUsage::eStorage;

		desc.memoryType = rhi::EMemoryType::eDeviceLocalImage;

		const rhi::TextureHandle texture = rhi->createTexture(desc);

		const u32 mipLevels = rhi::mipLevelsFor(width, height);

		if (onGpu)
		{
			// Only the top level is staged; the rest never exist in system memory at all.
			const rhi::TextureUpload top{ pixels, (u64)width * height * 4 };
			rhi->uploadTexture(texture, &top, 1);

			mipGenerator->generate(texture, width, height, mipLevels, srgb);

			return texture;
		}

		uploadWithCpuMips(rhi, texture, pixels, width, height, srgb);

		return texture;
	}

	void uploadWithCpuMips(
		const std::shared_ptr<rhi::IRHI>& rhi,
		rhi::TextureHandle texture,
		const u8* pixels,
		u32 width,
		u32 height,
		bool srgb)
	{
		// Successive halving, each level resampled from the one above it.
		std::vector<std::vector<u8>> levels;

		const u8* source = pixels;
		u32 sourceWidth = width;
		u32 sourceHeight = height;

		while (sourceWidth > 1 || sourceHeight > 1)
		{
			const u32 levelWidth = (sourceWidth > 1) ? sourceWidth / 2 : 1;
			const u32 levelHeight = (sourceHeight > 1) ? sourceHeight / 2 : 1;

			std::vector<u8> level((size_t)levelWidth * levelHeight * 4);

			// Averaging sRGB-encoded texels directly darkens the result, so the encode has
			// to be undone around the filter.
			if (srgb)
			{
				stbir_resize_uint8_srgb(
					source, (int)sourceWidth, (int)sourceHeight, 0,
					level.data(), (int)levelWidth, (int)levelHeight, 0,
					STBIR_RGBA);
			}
			else
			{
				stbir_resize_uint8_linear(
					source, (int)sourceWidth, (int)sourceHeight, 0,
					level.data(), (int)levelWidth, (int)levelHeight, 0,
					STBIR_RGBA);
			}

			levels.push_back(std::move(level));

			source = levels.back().data();
			sourceWidth = levelWidth;
			sourceHeight = levelHeight;
		}

		std::vector<rhi::TextureUpload> uploads;
		uploads.reserve(levels.size() + 1);
		uploads.push_back({ pixels, (u64)width * height * 4 });

		for (const auto& level : levels)
		{
			uploads.push_back({ level.data(), (u64)level.size() });
		}

		rhi->uploadTexture(texture, uploads.data(), (u32)uploads.size());
	}
}
