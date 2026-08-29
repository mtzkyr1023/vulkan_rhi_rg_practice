#ifndef _MV_NOISE_H_
#define _MV_NOISE_H_

#include <memory>
#include <vector>

#include "rhi/rhi.h"

#include "util/types.h"

namespace mv
{
	namespace compute
	{
		class MipGenerator;
	}

	namespace noise
	{
		using namespace types;

		// The underlying field. Everything else here is a way of stacking one of these.
		enum class EBasis
		{
			// Hash a value per lattice point and interpolate. The cheapest of the four, and
			// the blockiest: the extrema sit on the lattice, so the grid stays visible
			// however smooth the interpolation is.
			eValue,

			// Hash a gradient per lattice point and interpolate the dot products. The value
			// is zero at every lattice point, which is what hides the grid.
			ePerlin,

			// Perlin on a simplex lattice: triangles rather than squares. Fewer corners to
			// visit and no axis-aligned bias, which shows up in Perlin as a faint
			// preference for horizontal and vertical features.
			eSimplex,

			// Distance to the nearest of a set of scattered feature points. Produces cells
			// and creases rather than hills, which is what makes it useful for rock.
			eWorley,
		};

		// How the octaves are combined.
		enum class EFractal
		{
			// One octave of the basis, untouched.
			eNone,

			// Sum of octaves at halving amplitude. The default, and what most people mean
			// by "noise": rolling hills.
			eFbm,

			// Sum of inverted absolute values. The folds that |n| creates become sharp
			// crests instead of smooth peaks, which is the classic mountain-range look.
			eRidged,

			// Sum of absolute values. The same fold the other way up: rounded lumps
			// separated by creases, like clouds or piled sand.
			eBillow,
		};

		struct NoiseDesc
		{
			EBasis basis = EBasis::ePerlin;
			EFractal fractal = EFractal::eFbm;

			// Cycles across the unit domain. The domain a generated texture covers is
			// always [0, 1], so this is directly "how many features across the image".
			f32 frequency = 4.0f;

			u32 octaves = 6;

			// Frequency multiplier per octave. 2 doubles it, which pairs with a gain of 0.5
			// to give each octave the same slope and produces the self-similar look.
			f32 lacunarity = 2.0f;

			// Amplitude multiplier per octave. Below 0.5 the result is smooth, above it is
			// increasingly rough.
			f32 gain = 0.5f;

			u32 seed = 1337;

			// Offsets the sample position by a second noise field before evaluating.
			//
			// This is the cheapest way to stop fBm looking like fBm. Plain octaves give
			// isotropic blobs; warping the domain stretches and folds them into ridges and
			// basins that read as erosion, without simulating any.
			f32 warpStrength = 0.0f;
			f32 warpFrequency = 2.0f;

			// Wraps the lattice so the result tiles seamlessly across the unit domain.
			//
			// Only the lattice bases can honour this: simplex has a skewed lattice with no
			// integer period in the sample space, and Worley's feature points would have to
			// be mirrored across the seam. Both ignore it.
			bool tileable = false;
		};

		// Evaluates the field at a point in the unit domain.
		//
		// Roughly [-1, 1] for fBm, and [0, 1] for ridged and billow, which fold the
		// negative half onto the positive one.
		f32 sample(const NoiseDesc& desc, f32 x, f32 y);

		// Fills width * height samples, row-major, at pixel centres of the unit domain.
		//
		// Rescales the result so it spans exactly [0, 1]. Without this the range depends on
		// how many octaves happened to line up, and a heightmap built from six octaves
		// would be visibly flatter than one built from three.
		void generate(const NoiseDesc& desc, u32 width, u32 height, f32* out);

		// The same, without the rescale, so several fields can be combined on a common
		// scale before any of them is normalised.
		void generateRaw(const NoiseDesc& desc, u32 width, u32 height, f32* out);

		// A greyscale texture with a full mip chain, for use as an ordinary material map.
		//
		// R8G8B8A8 rather than a single-channel format because that is what the bindless
		// array is built around; the value is replicated across rgb with a=1.
		rhi::TextureHandle createTexture(
			const std::shared_ptr<rhi::IRHI>& rhi,
			const NoiseDesc& desc,
			u32 width,
			u32 height,
			bool srgb = false,
			compute::MipGenerator* mipGenerator = nullptr);

		// Uploads caller-supplied RGBA8 pixels as a texture with a full mip chain. The
		// terrain bakes its maps from several noise fields at once and cannot go through
		// createTexture, but wants the same mip handling.
		//
		// With a mip generator, only level 0 is staged and the rest are produced by a
		// compute dispatch. Without one, the whole chain is resized on the CPU and uploaded
		// -- still the right answer when the caller needs the levels in system memory
		// afterwards, which is what a virtual texture system slicing pages out of them does.
		rhi::TextureHandle createTextureFromPixels(
			const std::shared_ptr<rhi::IRHI>& rhi,
			const u8* pixels,
			u32 width,
			u32 height,
			bool srgb,
			compute::MipGenerator* mipGenerator = nullptr);

		// Resizes the chain on the CPU and uploads every level into an existing texture.
		// The fallback when there is no compute path, and the only option for a caller that
		// needs the levels in system memory afterwards.
		void uploadWithCpuMips(
			const std::shared_ptr<rhi::IRHI>& rhi,
			rhi::TextureHandle texture,
			const u8* pixels,
			u32 width,
			u32 height,
			bool srgb);
	}
}

#endif
