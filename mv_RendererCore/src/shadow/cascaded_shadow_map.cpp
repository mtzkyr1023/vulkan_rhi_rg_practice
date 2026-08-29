
#include "shadow/cascaded_shadow_map.h"

#include <algorithm>
#include <cmath>

namespace mv::shadow
{
	namespace
	{
		// How far the scene reaches along one axis, measured as the extent of its bounding
		// box projected onto that direction. This is how far back a cascade has to start so
		// that a caster anywhere in the scene still lands in its depth range.
		f32 extentAlong(const math::Vec3& boundsMin, const math::Vec3& boundsMax, const math::Vec3& direction)
		{
			const math::Vec3 half
			{
				(boundsMax.x - boundsMin.x) * 0.5f,
				(boundsMax.y - boundsMin.y) * 0.5f,
				(boundsMax.z - boundsMin.z) * 0.5f,
			};

			return 2.0f * (std::abs(direction.x) * half.x + std::abs(direction.y) * half.y + std::abs(direction.z) * half.z);
		}
	}

	bool CascadedShadowMap::initialize(const std::shared_ptr<rhi::IRHI>& rhi)
	{
		rhi_ = rhi;

		rhi::TextureDesc desc{};
		desc.width = kAtlasSize;
		desc.height = kAtlasSize;
		desc.depth = 1;
		// Both, and that combination is the whole reason the D3D12 backend has to create
		// depth resources as typeless: a depth format is only legal on a depth view.
		desc.usage = rhi::ETextureUsage::eDepthStencilAttachment | rhi::ETextureUsage::eSampled;
		desc.mipLevels = 1;
		desc.format = rhi::ETextureFormat::eD32_SFLOAT;
		desc.memoryType = rhi::EMemoryType::eDeviceLocalImage;

		texture_ = rhi_->createTexture(desc);

		return texture_ != INVALID_HANDLE;
	}

	void CascadedShadowMap::deinitialize()
	{
		if (!rhi_) return;

		if (texture_ != INVALID_HANDLE)
		{
			rhi_->freeImage(texture_);
			texture_ = INVALID_HANDLE;
		}

		rhi_.reset();
	}

	void CascadedShadowMap::update(
		const CameraView& camera,
		const math::Vec3& lightDirection,
		const math::Vec3& sceneBoundsMin,
		const math::Vec3& sceneBoundsMax)
	{
		const math::Vec3 lightDir = math::normalize(lightDirection);

		const f32 nearZ = camera.nearZ;
		const f32 farZ = distance_;

		// Where the progression starts. Anchoring it to the camera near plane is what makes
		// a textbook logarithmic split useless in practice: with a near plane of a few
		// centimetres the first boundaries land centimetres away too, and everything the
		// viewer can actually see ends up in the last cascade.
		const f32 splitNear = std::max(nearDistance_, nearZ);

		// Split positions, in view-space depth. The logarithmic term gives every cascade the
		// same far-to-near ratio, which is what keeps the projected texel size roughly
		// constant; lambda blends towards a uniform split for comparison.
		f32 splits[kMaxCascades + 1]{};

		// Cascade 0 still reaches back to the camera, so geometry closer than splitNear is
		// covered rather than left unshadowed.
		splits[0] = nearZ;
		splits[kMaxCascades] = farZ;

		for (u32 i = 1; i < kMaxCascades; i++)
		{
			const f32 p = (f32)i / (f32)kMaxCascades;

			const f32 logSplit = splitNear * std::pow(farZ / splitNear, p);
			const f32 uniformSplit = splitNear + (farZ - splitNear) * p;

			splits[i] = lambda_ * logSplit + (1.0f - lambda_) * uniformSplit;
		}

		const f32 tanHalfFov = std::tan(camera.fovY * 0.5f);
		const f32 sceneExtent = extentAlong(sceneBoundsMin, sceneBoundsMax, lightDir);

		for (u32 i = 0; i < kMaxCascades; i++)
		{
			const f32 sliceNear = splits[i];
			const f32 sliceFar = splits[i + 1];

			// The eight corners of this slice of the view frustum, in world space. Built
			// from the camera basis rather than by inverting a matrix, which keeps this
			// independent of the projection convention.
			math::Vec3 corners[8];
			for (u32 c = 0; c < 8; c++)
			{
				const f32 depth = (c < 4) ? sliceNear : sliceFar;
				const f32 halfHeight = tanHalfFov * depth;
				const f32 halfWidth = halfHeight * camera.aspect;

				const f32 sx = (c & 1) ? 1.0f : -1.0f;
				const f32 sy = (c & 2) ? 1.0f : -1.0f;

				corners[c] =
					camera.position +
					camera.forward * depth +
					camera.right * (halfWidth * sx) +
					camera.up * (halfHeight * sy);
			}

			math::Vec3 center{};
			for (const auto& corner : corners)
			{
				center = center + corner;
			}
			center = center * (1.0f / 8.0f);

			// A bounding sphere rather than a box: its size does not change as the camera
			// rotates, so the cascade cannot grow and shrink from frame to frame and make
			// the shadow edges crawl.
			f32 radius = 0.0f;
			for (const auto& corner : corners)
			{
				const math::Vec3 d = corner - center;
				radius = std::max(radius, std::sqrt(math::dot(d, d)));
			}

			// Quantised so that sub-pixel camera movement cannot change it at all.
			radius = std::ceil(radius * 16.0f) / 16.0f;

			const f32 backoff = radius + sceneExtent;

			// Straight down the light would make the up vector degenerate.
			const math::Vec3 up = (std::abs(lightDir.y) > 0.99f)
				? math::Vec3{ 0.0f, 0.0f, 1.0f }
				: math::Vec3{ 0.0f, 1.0f, 0.0f };

			const math::Mat4 view = math::lookAtRH(center - lightDir * backoff, center, up);
			math::Mat4 proj = math::orthoRH(-radius, radius, -radius, radius, 0.0f, backoff + radius);

			// Texel snapping. Without it the cascade slides continuously with the camera and
			// every shadow edge shimmers, because the same world position keeps landing on a
			// different part of a texel. Rounding the projection's origin to a whole texel
			// pins the sampling grid to the world instead.
			{
				const math::Mat4 unsnapped = view * proj;

				const f32 halfResolution = (f32)kCascadeResolution * 0.5f;

				const math::Vec3 origin = math::transformPoint(unsnapped, { 0.0f, 0.0f, 0.0f });

				const f32 x = origin.x * halfResolution;
				const f32 y = origin.y * halfResolution;

				proj.m[12] += (std::round(x) - x) / halfResolution;
				proj.m[13] += (std::round(y) - y) / halfResolution;
			}

			cascades_[i].viewProj = view * proj;
			cascades_[i].splitDepth = sliceFar;
			cascades_[i].texelWorldSize = (2.0f * radius) / (f32)kCascadeResolution;
		}
	}
}
