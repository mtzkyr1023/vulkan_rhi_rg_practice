
#include "env/environment.h"

#include "util/parallel.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <thread>

namespace mv::env
{
	namespace
	{
		constexpr f32 kPi = 3.14159265359f;

		// --- cube geometry --------------------------------------------------------

		// Face order is +X, -X, +Y, -Y, +Z, -Z, and within a face u runs right and v runs
		// down. Both APIs agree on this, which is why one table serves both.
		math::Vec3 faceDirection(u32 face, f32 u, f32 v)
		{
			switch (face)
			{
			case rhi::eCubePosX: return {  1.0f,   -v,   -u };
			case rhi::eCubeNegX: return { -1.0f,   -v,    u };
			case rhi::eCubePosY: return {     u, 1.0f,    v };
			case rhi::eCubeNegY: return {     u,-1.0f,   -v };
			case rhi::eCubePosZ: return {     u,   -v, 1.0f };
			default:             return {    -u,   -v,-1.0f };
			}
		}

		// The inverse: which face a direction lands on, and where.
		void directionToFace(const math::Vec3& d, u32& face, f32& u, f32& v)
		{
			const f32 ax = std::abs(d.x);
			const f32 ay = std::abs(d.y);
			const f32 az = std::abs(d.z);

			if (ax >= ay && ax >= az)
			{
				const f32 inv = 1.0f / ax;
				if (d.x > 0.0f) { face = rhi::eCubePosX; u = -d.z * inv; v = -d.y * inv; }
				else            { face = rhi::eCubeNegX; u =  d.z * inv; v = -d.y * inv; }
			}
			else if (ay >= az)
			{
				const f32 inv = 1.0f / ay;
				if (d.y > 0.0f) { face = rhi::eCubePosY; u = d.x * inv; v =  d.z * inv; }
				else            { face = rhi::eCubeNegY; u = d.x * inv; v = -d.z * inv; }
			}
			else
			{
				const f32 inv = 1.0f / az;
				if (d.z > 0.0f) { face = rhi::eCubePosZ; u =  d.x * inv; v = -d.y * inv; }
				else            { face = rhi::eCubeNegZ; u = -d.x * inv; v = -d.y * inv; }
			}
		}

		// The solid angle a cube texel covers. Texels near a face corner subtend far less
		// than ones at the centre, and a projection that ignored that would tilt every
		// coefficient towards the corners.
		f32 areaElement(f32 x, f32 y)
		{
			return std::atan2(x * y, std::sqrt(x * x + y * y + 1.0f));
		}

		f32 texelSolidAngle(u32 x, u32 y, u32 size)
		{
			const f32 inv = 2.0f / (f32)size;

			const f32 u0 = (f32)x * inv - 1.0f;
			const f32 v0 = (f32)y * inv - 1.0f;
			const f32 u1 = u0 + inv;
			const f32 v1 = v0 + inv;

			return areaElement(u0, v0) - areaElement(u0, v1) - areaElement(u1, v0) + areaElement(u1, v1);
		}

		// --- sampling -------------------------------------------------------------

		// Low-discrepancy pairs. Radical inverse in base 2 by bit reversal.
		f32 radicalInverse(u32 bits)
		{
			bits = (bits << 16u) | (bits >> 16u);
			bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
			bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
			bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
			bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);

			return (f32)bits * 2.3283064365386963e-10f;
		}

		// A half vector drawn from the GGX distribution around `normal`. Importance
		// sampling rather than a uniform sweep is what keeps the sample count at 64: the
		// samples land where the lobe actually has energy.
		math::Vec3 importanceSampleGGX(u32 index, u32 count, f32 roughness, const math::Vec3& normal)
		{
			const f32 a = roughness * roughness;

			const f32 u1 = (f32)index / (f32)count;
			const f32 u2 = radicalInverse(index);

			const f32 phi = 2.0f * kPi * u1;
			const f32 cosTheta = std::sqrt((1.0f - u2) / (1.0f + (a * a - 1.0f) * u2));
			const f32 sinTheta = std::sqrt(std::max(0.0f, 1.0f - cosTheta * cosTheta));

			const math::Vec3 tangentH{ sinTheta * std::cos(phi), sinTheta * std::sin(phi), cosTheta };

			const math::Vec3 up = (std::abs(normal.z) < 0.999f)
				? math::Vec3{ 0.0f, 0.0f, 1.0f }
				: math::Vec3{ 1.0f, 0.0f, 0.0f };

			const math::Vec3 tangentX = math::normalize(math::cross(up, normal));
			const math::Vec3 tangentY = math::cross(normal, tangentX);

			return tangentX * tangentH.x + tangentY * tangentH.y + normal * tangentH.z;
		}
	}

	bool Environment::initialize(const std::shared_ptr<rhi::IRHI>& rhi, compute::EnvironmentBaker* baker)
	{
		rhi_ = rhi;
		baker_ = baker;

		rhi::TextureDesc desc{};
		desc.width = kCubeFaceSize;
		desc.height = kCubeFaceSize;
		desc.depth = 1;
		desc.usage = rhi::ETextureUsage::eSampled | rhi::ETextureUsage::eTransferDst;

		// Writable when the bake is going to be dispatches rather than an upload.
		if (baker_ != nullptr && baker_->isReady())
			desc.usage = desc.usage | rhi::ETextureUsage::eStorage;
		desc.mipLevels = kMipCount;
		desc.arrayLayers = rhi::eCubeFaceCount;
		desc.cube = true;
		// The sky carries values well above one around the sun, and clipping them would
		// throw away exactly the part that dominates both the reflection and the
		// coefficients.
		desc.format = rhi::ETextureFormat::eR16G16B16A16_SFLOAT;
		desc.memoryType = rhi::EMemoryType::eDeviceLocalImage;

		cubemap_ = rhi_->createTexture(desc);

		return cubemap_ != INVALID_HANDLE;
	}

	void Environment::deinitialize()
	{
		if (!rhi_) return;

		if (cubemap_ != INVALID_HANDLE)
		{
			rhi_->freeImage(cubemap_);
			cubemap_ = INVALID_HANDLE;
		}

		rhi_.reset();
	}

	math::Vec3 Environment::skyRadiance(const math::Vec3& direction, const SkyParams& params) const
	{
		// Single-scattering through a spherical atmosphere: Rayleigh for the blue of the
		// sky and Mie for the white haze that pools around the sun.
		constexpr f32 kEarthRadius = 6360e3f;
		constexpr f32 kAtmosphereRadius = 6420e3f;
		constexpr f32 kRayleighScaleHeight = 7994.0f;
		constexpr f32 kMieScaleHeight = 1200.0f;

		const math::Vec3 betaR{ 3.8e-6f, 13.5e-6f, 33.1e-6f };
		const f32 betaM = 21e-6f * params.turbidity;

		// Towards the sun, opposite the direction its light travels.
		const math::Vec3 sun = math::normalize(params.lightDirection * -1.0f);

		const math::Vec3 dir = math::normalize(direction);

		// Below the horizon the ray hits the ground rather than leaving the atmosphere. The
		// caller supplies that colour, because it is the same for every downward direction
		// and integrating the sky again for each of them would double the cost of a bake.
		if (dir.y < 0.0f)
			return groundRadiance_;

		// Distance to the top of the atmosphere from an observer just above the surface.
		const math::Vec3 origin{ 0.0f, kEarthRadius + 1.0f, 0.0f };

		auto atmosphereDistance = [&](const math::Vec3& o, const math::Vec3& d)
		{
			const f32 b = 2.0f * math::dot(o, d);
			const f32 c = math::dot(o, o) - kAtmosphereRadius * kAtmosphereRadius;
			const f32 disc = b * b - 4.0f * c;

			if (disc < 0.0f)
				return 0.0f;

			return (-b + std::sqrt(disc)) * 0.5f;
		};

		const f32 rayLength = atmosphereDistance(origin, dir);
		if (rayLength <= 0.0f)
			return { 0.0f, 0.0f, 0.0f };

		// Step counts kept low deliberately: this runs for every texel of every face, and
		// the result is about to be blurred into nine coefficients and a mip chain anyway.
		constexpr u32 kViewSamples = 16;
		constexpr u32 kLightSamples = 8;

		const f32 segment = rayLength / (f32)kViewSamples;

		f32 opticalDepthR = 0.0f;
		f32 opticalDepthM = 0.0f;

		math::Vec3 sumR{};
		math::Vec3 sumM{};

		const f32 mu = math::dot(dir, sun);

		// Rayleigh scatters almost evenly; Mie throws light strongly forward, which is what
		// puts the bright halo right around the sun.
		const f32 phaseR = 3.0f / (16.0f * kPi) * (1.0f + mu * mu);

		const f32 g = 0.76f;
		const f32 phaseM = 3.0f / (8.0f * kPi) * ((1.0f - g * g) * (1.0f + mu * mu)) /
			((2.0f + g * g) * std::pow(1.0f + g * g - 2.0f * g * mu, 1.5f));

		for (u32 i = 0; i < kViewSamples; i++)
		{
			const math::Vec3 samplePosition = origin + dir * (segment * ((f32)i + 0.5f));
			const f32 height = std::sqrt(math::dot(samplePosition, samplePosition)) - kEarthRadius;

			const f32 hr = std::exp(-height / kRayleighScaleHeight) * segment;
			const f32 hm = std::exp(-height / kMieScaleHeight) * segment;

			opticalDepthR += hr;
			opticalDepthM += hm;

			// How much of the sunlight survives the trip down to this sample.
			const f32 lightLength = atmosphereDistance(samplePosition, sun);
			const f32 lightSegment = lightLength / (f32)kLightSamples;

			f32 lightDepthR = 0.0f;
			f32 lightDepthM = 0.0f;

			bool blocked = false;
			for (u32 j = 0; j < kLightSamples; j++)
			{
				const math::Vec3 lightPosition = samplePosition + sun * (lightSegment * ((f32)j + 0.5f));
				const f32 lightHeight = std::sqrt(math::dot(lightPosition, lightPosition)) - kEarthRadius;

				if (lightHeight < 0.0f)
				{
					blocked = true;
					break;
				}

				lightDepthR += std::exp(-lightHeight / kRayleighScaleHeight) * lightSegment;
				lightDepthM += std::exp(-lightHeight / kMieScaleHeight) * lightSegment;
			}

			if (blocked)
				continue;

			const math::Vec3 tau
			{
				betaR.x * (opticalDepthR + lightDepthR) + betaM * 1.1f * (opticalDepthM + lightDepthM),
				betaR.y * (opticalDepthR + lightDepthR) + betaM * 1.1f * (opticalDepthM + lightDepthM),
				betaR.z * (opticalDepthR + lightDepthR) + betaM * 1.1f * (opticalDepthM + lightDepthM),
			};

			const math::Vec3 attenuation{ std::exp(-tau.x), std::exp(-tau.y), std::exp(-tau.z) };

			sumR = sumR + attenuation * hr;
			sumM = sumM + attenuation * hm;
		}

		math::Vec3 result
		{
			(sumR.x * betaR.x * phaseR + sumM.x * betaM * phaseM),
			(sumR.y * betaR.y * phaseR + sumM.y * betaM * phaseM),
			(sumR.z * betaR.z * phaseR + sumM.z * betaM * phaseM),
		};

		return result * params.sunIntensity;
	}

	math::Vec3 Environment::sampleLevel(const std::vector<math::Vec3>* faces, u32 size, const math::Vec3& direction) const
	{
		u32 face = 0;
		f32 u = 0.0f;
		f32 v = 0.0f;
		directionToFace(direction, face, u, v);

		// Face coordinates are in [-1, 1]; texel centres sit at half-texel offsets.
		const f32 x = (u * 0.5f + 0.5f) * (f32)size - 0.5f;
		const f32 y = (v * 0.5f + 0.5f) * (f32)size - 0.5f;

		const s32 x0 = (s32)std::floor(x);
		const s32 y0 = (s32)std::floor(y);

		const f32 fx = x - (f32)x0;
		const f32 fy = y - (f32)y0;

		// A tap that runs off the edge of a face is resolved through the direction it names
		// rather than clamped back inside. Clamping leaves a seam that the prefilter widens
		// with every level, because each level reads the one above it and pulls the wrong
		// colour a little further in each time.
		auto texel = [&](s32 tx, s32 ty)
		{
			if (tx >= 0 && ty >= 0 && tx < (s32)size && ty < (s32)size)
				return faces[face][(size_t)ty * size + tx];

			// Off the face. The out-of-range coordinate still names a real direction, which
			// lands on whichever neighbour actually owns it.
			const f32 ou = ((f32)tx + 0.5f) / (f32)size * 2.0f - 1.0f;
			const f32 ov = ((f32)ty + 0.5f) / (f32)size * 2.0f - 1.0f;

			const math::Vec3 d = math::normalize(faceDirection(face, ou, ov));

			u32 nFace = 0;
			f32 nu = 0.0f;
			f32 nv = 0.0f;
			directionToFace(d, nFace, nu, nv);

			const s32 nx = std::clamp((s32)((nu * 0.5f + 0.5f) * (f32)size), 0, (s32)size - 1);
			const s32 ny = std::clamp((s32)((nv * 0.5f + 0.5f) * (f32)size), 0, (s32)size - 1);

			return faces[nFace][(size_t)ny * size + nx];
		};

		const math::Vec3 c00 = texel(x0, y0);
		const math::Vec3 c10 = texel(x0 + 1, y0);
		const math::Vec3 c01 = texel(x0, y0 + 1);
		const math::Vec3 c11 = texel(x0 + 1, y0 + 1);

		const math::Vec3 top = c00 * (1.0f - fx) + c10 * fx;
		const math::Vec3 bottom = c01 * (1.0f - fx) + c11 * fx;

		return top * (1.0f - fy) + bottom * fy;
	}

	void Environment::projectToSh(const std::vector<math::Vec3>* faces, u32 size)
	{
		for (auto& coefficient : sh_)
		{
			coefficient = 0.0f;
		}

		for (u32 face = 0; face < rhi::eCubeFaceCount; face++)
		{
			for (u32 y = 0; y < size; y++)
			{
				for (u32 x = 0; x < size; x++)
				{
					const f32 u = ((f32)x + 0.5f) / (f32)size * 2.0f - 1.0f;
					const f32 v = ((f32)y + 0.5f) / (f32)size * 2.0f - 1.0f;

					const math::Vec3 d = math::normalize(faceDirection(face, u, v));
					const f32 dw = texelSolidAngle(x, y, size);

					const math::Vec3 L = faces[face][(size_t)y * size + x];

					// The real spherical harmonics up to l = 2, in the same order the
					// shader reconstructs them in.
					const f32 basis[kShCoefficientCount] =
					{
						0.282095f,
						0.488603f * d.y,
						0.488603f * d.z,
						0.488603f * d.x,
						1.092548f * d.x * d.y,
						1.092548f * d.y * d.z,
						0.315392f * (3.0f * d.z * d.z - 1.0f),
						1.092548f * d.x * d.z,
						0.546274f * (d.x * d.x - d.y * d.y),
					};

					for (u32 i = 0; i < kShCoefficientCount; i++)
					{
						sh_[i * 3 + 0] += L.x * basis[i] * dw;
						sh_[i * 3 + 1] += L.y * basis[i] * dw;
						sh_[i * 3 + 2] += L.z * basis[i] * dw;
					}
				}
			}
		}
	}

	void Environment::prefilter(
		const std::vector<math::Vec3>* source, u32 sourceSize,
		std::vector<math::Vec3>* target, u32 targetSize, f32 roughness)
	{
		constexpr u32 kSampleCount = 64;

		for (u32 face = 0; face < rhi::eCubeFaceCount; face++)
		{
			target[face].resize((size_t)targetSize * targetSize);
		}

		util::parallelFor(rhi::eCubeFaceCount * targetSize, [&](u32 index)
			{
				const u32 face = index / targetSize;
				const u32 y = index % targetSize;

				for (u32 x = 0; x < targetSize; x++)
				{
					const f32 u = ((f32)x + 0.5f) / (f32)targetSize * 2.0f - 1.0f;
					const f32 v = ((f32)y + 0.5f) / (f32)targetSize * 2.0f - 1.0f;

					// The split-sum approximation assumes the view direction equals the
					// normal. It is wrong at grazing angles, which is why the reflection
					// stretches less than it should, and it is what makes a single
					// prefiltered chain usable for every view at all.
					const math::Vec3 n = math::normalize(faceDirection(face, u, v));

					math::Vec3 sum{};
					f32 weight = 0.0f;

					for (u32 i = 0; i < kSampleCount; i++)
					{
						const math::Vec3 h = importanceSampleGGX(i, kSampleCount, roughness, n);
						const math::Vec3 l = h * (2.0f * math::dot(n, h)) - n;

						const f32 NdotL = math::dot(n, l);
						if (NdotL <= 0.0f)
							continue;

						sum = sum + sampleLevel(source, sourceSize, l) * NdotL;
						weight += NdotL;
					}

					target[face][(size_t)y * targetSize + x] = (weight > 0.0f)
						? sum * (1.0f / weight)
						: sampleLevel(source, sourceSize, n);
				}
			});
	}

	void Environment::bake(const SkyParams& params, const compute::EnvironmentBaker::CloudLayer& clouds)
	{
		const auto start = std::chrono::high_resolution_clock::now();

		bakedOnGpu_ = (baker_ != nullptr) && baker_->isReady();

		if (bakedOnGpu_)
		{
			// The sky and the whole prefiltered chain, in one submit. What comes back is
			// level 0's radiance, face-major, which is the only thing the CPU still needs.
			std::vector<math::Vec3> radiance;
			baker_->bake(params, cubemap_, kCubeFaceSize, kMipCount, radiance, clouds);

			if (radiance.size() == (size_t)kCubeFaceSize * kCubeFaceSize * rhi::eCubeFaceCount)
			{
				std::vector<math::Vec3> faces[rhi::eCubeFaceCount];

				for (u32 face = 0; face < rhi::eCubeFaceCount; face++)
				{
					const size_t offset = (size_t)face * kCubeFaceSize * kCubeFaceSize;

					faces[face].assign(
						radiance.begin() + offset,
						radiance.begin() + offset + (size_t)kCubeFaceSize * kCubeFaceSize);
				}

				projectToSh(faces, kCubeFaceSize);
			}

			const auto gpuEnd = std::chrono::high_resolution_clock::now();
			lastBakeMs_ = std::chrono::duration<f32, std::milli>(gpuEnd - start).count();

			return;
		}

		// The ground is the same colour in every downward direction, so it is integrated
		// once here rather than per texel. It matters: half the cube faces the ground.
		{
			// Zenith radiance stands in for the whole sky the ground can see.
			groundRadiance_ = { 0.0f, 0.0f, 0.0f };

			const math::Vec3 skyAbove = skyRadiance({ 0.0f, 1.0f, 0.0f }, params);
			const math::Vec3 sun = math::normalize(params.lightDirection * -1.0f);

			const f32 sunOnGround = std::max(0.0f, sun.y);
			const math::Vec3 lit = skyAbove * 0.5f + math::Vec3{ 1.0f, 0.95f, 0.85f } * (params.sunIntensity * 0.02f * sunOnGround);

			groundRadiance_ =
			{
				lit.x * params.groundAlbedo.x,
				lit.y * params.groundAlbedo.y,
				lit.z * params.groundAlbedo.z,
			};
		}

		// Level 0 is the sky itself: roughness zero is a mirror, so no filtering applies.
		std::vector<math::Vec3> levels[kMipCount][rhi::eCubeFaceCount];

		for (u32 face = 0; face < rhi::eCubeFaceCount; face++)
		{
			levels[0][face].resize((size_t)kCubeFaceSize * kCubeFaceSize);
		}

		// One task per row of every face. Each writes only its own row, so nothing needs
		// locking.
		util::parallelFor(rhi::eCubeFaceCount * kCubeFaceSize, [&](u32 index)
			{
				const u32 face = index / kCubeFaceSize;
				const u32 y = index % kCubeFaceSize;

				for (u32 x = 0; x < kCubeFaceSize; x++)
				{
					const f32 u = ((f32)x + 0.5f) / (f32)kCubeFaceSize * 2.0f - 1.0f;
					const f32 v = ((f32)y + 0.5f) / (f32)kCubeFaceSize * 2.0f - 1.0f;

					levels[0][face][(size_t)y * kCubeFaceSize + x] =
						skyRadiance(faceDirection(face, u, v), params);
				}
			});

		// The coefficients come from the unfiltered sky, because the projection is itself
		// the filter: convolving with a cosine lobe is exactly what the nine numbers encode.
		projectToSh(levels[0], kCubeFaceSize);

		for (u32 mip = 1; mip < kMipCount; mip++)
		{
			const u32 size = kCubeFaceSize >> mip;
			const f32 roughness = (f32)mip / (f32)(kMipCount - 1);

			prefilter(levels[0], kCubeFaceSize, levels[mip], size, roughness);
		}

		// Half floats, laid out layer-major to match what uploadTexture expects.
		std::vector<std::vector<u16>> storage;
		std::vector<rhi::TextureUpload> uploads;

		storage.reserve(rhi::eCubeFaceCount * kMipCount);
		uploads.reserve(rhi::eCubeFaceCount * kMipCount);

		for (u32 face = 0; face < rhi::eCubeFaceCount; face++)
		{
			for (u32 mip = 0; mip < kMipCount; mip++)
			{
				const u32 size = kCubeFaceSize >> mip;

				std::vector<u16> texels((size_t)size * size * 4);
				for (size_t i = 0; i < (size_t)size * size; i++)
				{
					const math::Vec3& c = levels[mip][face][i];

					texels[i * 4 + 0] = math::floatToHalf(c.x);
					texels[i * 4 + 1] = math::floatToHalf(c.y);
					texels[i * 4 + 2] = math::floatToHalf(c.z);
					texels[i * 4 + 3] = math::floatToHalf(1.0f);
				}

				storage.push_back(std::move(texels));
				uploads.push_back({ storage.back().data(), storage.back().size() * sizeof(u16) });
			}
		}

		rhi_->uploadTexture(cubemap_, uploads.data(), (u32)uploads.size());

		const auto end = std::chrono::high_resolution_clock::now();
		lastBakeMs_ = std::chrono::duration<f32, std::milli>(end - start).count();
	}
}
