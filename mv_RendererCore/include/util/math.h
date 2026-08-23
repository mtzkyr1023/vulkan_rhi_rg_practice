
#ifndef _MV_MATH_H_
#define _MV_MATH_H_

#include <cmath>

#include "util/types.h"

namespace mv
{
	namespace math
	{
		using namespace types;

		struct Vec3
		{
			f32 x = 0.0f;
			f32 y = 0.0f;
			f32 z = 0.0f;
		};

		inline Vec3 operator+(const Vec3& a, const Vec3& b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
		inline Vec3 operator-(const Vec3& a, const Vec3& b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
		inline Vec3 operator*(const Vec3& a, f32 s) { return { a.x * s, a.y * s, a.z * s }; }

		inline f32 dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

		inline Vec3 cross(const Vec3& a, const Vec3& b)
		{
			return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
		}

		inline Vec3 normalize(const Vec3& v)
		{
			const f32 length = std::sqrt(dot(v, v));
			return (length > 0.0f) ? v * (1.0f / length) : v;
		}

		// Row-major storage, row-vector convention: v' = v * M, and A * B applies A first.
		// This matches the row_major declarations and mul(v, M) calls in the shaders.
		struct Mat4
		{
			f32 m[16]{};

			static Mat4 identity()
			{
				Mat4 r;
				r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0f;
				return r;
			}
		};

		inline Mat4 operator*(const Mat4& a, const Mat4& b)
		{
			Mat4 r;
			for (u32 row = 0; row < 4; row++)
			{
				for (u32 col = 0; col < 4; col++)
				{
					f32 sum = 0.0f;
					for (u32 k = 0; k < 4; k++)
					{
						sum += a.m[row * 4 + k] * b.m[k * 4 + col];
					}
					r.m[row * 4 + col] = sum;
				}
			}
			return r;
		}

		// Transforms a point (w = 1).
		inline Vec3 transformPoint(const Mat4& mat, const Vec3& v)
		{
			return {
				v.x * mat.m[0] + v.y * mat.m[4] + v.z * mat.m[8] + mat.m[12],
				v.x * mat.m[1] + v.y * mat.m[5] + v.z * mat.m[9] + mat.m[13],
				v.x * mat.m[2] + v.y * mat.m[6] + v.z * mat.m[10] + mat.m[14],
			};
		}

		// Transforms a direction (w = 0), ignoring translation.
		inline Vec3 transformDirection(const Mat4& mat, const Vec3& v)
		{
			return {
				v.x * mat.m[0] + v.y * mat.m[4] + v.z * mat.m[8],
				v.x * mat.m[1] + v.y * mat.m[5] + v.z * mat.m[9],
				v.x * mat.m[2] + v.y * mat.m[6] + v.z * mat.m[10],
			};
		}

		// Right-handed, matching glTF's coordinate system, with a 0..1 depth range as both
		// D3D12 and (after the backend's viewport flip) Vulkan expect.
		inline Mat4 perspectiveRH(f32 fovY, f32 aspect, f32 nearZ, f32 farZ)
		{
			const f32 h = 1.0f / std::tan(fovY * 0.5f);
			const f32 w = h / aspect;

			Mat4 r;
			r.m[0] = w;
			r.m[5] = h;
			r.m[10] = farZ / (nearZ - farZ);
			r.m[11] = -1.0f;
			r.m[14] = nearZ * farZ / (nearZ - farZ);
			return r;
		}

		inline Mat4 lookAtRH(const Vec3& eye, const Vec3& target, const Vec3& up)
		{
			const Vec3 zaxis = normalize(eye - target);
			const Vec3 xaxis = normalize(cross(up, zaxis));
			const Vec3 yaxis = cross(zaxis, xaxis);

			Mat4 r = Mat4::identity();
			r.m[0] = xaxis.x; r.m[1] = yaxis.x; r.m[2] = zaxis.x;
			r.m[4] = xaxis.y; r.m[5] = yaxis.y; r.m[6] = zaxis.y;
			r.m[8] = xaxis.z; r.m[9] = yaxis.z; r.m[10] = zaxis.z;
			r.m[12] = -dot(xaxis, eye);
			r.m[13] = -dot(yaxis, eye);
			r.m[14] = -dot(zaxis, eye);
			return r;
		}

		// Inverse-transpose of the upper 3x3, needed to transform normals correctly when
		// the matrix carries non-uniform scale.
		inline Mat4 normalMatrix(const Mat4& mat)
		{
			const f32 a = mat.m[0], b = mat.m[1], c = mat.m[2];
			const f32 d = mat.m[4], e = mat.m[5], f = mat.m[6];
			const f32 g = mat.m[8], h = mat.m[9], i = mat.m[10];

			const f32 det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
			if (det == 0.0f)
				return Mat4::identity();

			const f32 invDet = 1.0f / det;

			Mat4 r = Mat4::identity();
			// cofactor matrix / det, already transposed twice (inverse-transpose == cofactor / det)
			r.m[0] = (e * i - f * h) * invDet;
			r.m[1] = (f * g - d * i) * invDet;
			r.m[2] = (d * h - e * g) * invDet;
			r.m[4] = (c * h - b * i) * invDet;
			r.m[5] = (a * i - c * g) * invDet;
			r.m[6] = (b * g - a * h) * invDet;
			r.m[8] = (b * f - c * e) * invDet;
			r.m[9] = (c * d - a * f) * invDet;
			r.m[10] = (a * e - b * d) * invDet;
			return r;
		}
	}
}

#endif
