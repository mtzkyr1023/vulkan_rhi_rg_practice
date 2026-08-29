
#ifndef _MV_MATH_H_
#define _MV_MATH_H_

#include <cmath>
#include <cstring>

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

		// IEEE binary16, for filling a half-float texture from the CPU. Values beyond the
		// half range saturate to its maximum rather than becoming infinity, which keeps a
		// very bright sun from poisoning everything it is later filtered into.
		inline types::u16 floatToHalf(f32 value)
		{
			types::u32 bits;
			std::memcpy(&bits, &value, sizeof(bits));

			const types::u32 sign = (bits >> 16) & 0x8000u;
			types::s32 exponent = (types::s32)((bits >> 23) & 0xFFu) - 127 + 15;
			types::u32 mantissa = bits & 0x7FFFFFu;

			if (exponent >= 0x1F)
				return (types::u16)(sign | 0x7BFFu);

			if (exponent <= 0)
				return (types::u16)sign;

			return (types::u16)(sign | ((types::u32)exponent << 10) | (mantissa >> 13));
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

		// Unit quaternion, glTF's (x, y, z, w) order. Rotations only ever pass through
		// here on their way to a matrix; nothing composes quaternions at runtime.
		struct Quat
		{
			f32 x = 0.0f;
			f32 y = 0.0f;
			f32 z = 0.0f;
			f32 w = 1.0f;
		};

		// Named apart from normalize: a three-element braced list would otherwise be
		// ambiguous between Vec3 and a Quat with a defaulted w.
		inline Quat normalizeQuat(const Quat& q)
		{
			const f32 length = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);

			if (length < 1e-8f)
				return {};

			return { q.x / length, q.y / length, q.z / length, q.w / length };
		}

		// Normalised lerp: cheaper than slerp and indistinguishable at keyframe
		// spacing. Negating one side keeps the blend on the short arc -- q and -q are
		// the same rotation, but lerping between them passes through zero.
		inline Quat nlerp(const Quat& a, const Quat& b, f32 t)
		{
			const f32 dot = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
			const f32 sign = dot < 0.0f ? -1.0f : 1.0f;

			return normalizeQuat({
				a.x + (b.x * sign - a.x) * t,
				a.y + (b.y * sign - a.y) * t,
				a.z + (b.z * sign - a.z) * t,
				a.w + (b.w * sign - a.w) * t });
		}

		// Rotation matrix in this codebase's row-vector convention: the transpose of
		// the column-vector textbook form.
		inline Mat4 matFromQuat(const Quat& q)
		{
			const f32 xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z;
			const f32 xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z;
			const f32 wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;

			Mat4 r = Mat4::identity();

			r.m[0] = 1.0f - 2.0f * (yy + zz); r.m[1] = 2.0f * (xy + wz);        r.m[2] = 2.0f * (xz - wy);
			r.m[4] = 2.0f * (xy - wz);        r.m[5] = 1.0f - 2.0f * (xx + zz); r.m[6] = 2.0f * (yz + wx);
			r.m[8] = 2.0f * (xz + wy);        r.m[9] = 2.0f * (yz - wx);        r.m[10] = 1.0f - 2.0f * (xx + yy);

			return r;
		}

		// Scale, then rotate, then translate -- the order every node transform means.
		inline Mat4 composeTRS(const Vec3& translation, const Quat& rotation, const Vec3& scale)
		{
			Mat4 r = matFromQuat(rotation);

			for (u32 c = 0; c < 3; c++)
			{
				r.m[0 + c] *= scale.x;
				r.m[4 + c] *= scale.y;
				r.m[8 + c] *= scale.z;
			}

			r.m[12] = translation.x;
			r.m[13] = translation.y;
			r.m[14] = translation.z;

			return r;
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

		// Same handedness and depth range as perspectiveRH. A directional light has no eye
		// point, so its cascades are fitted with one of these rather than a frustum.
		inline Mat4 orthoRH(f32 left, f32 right, f32 bottom, f32 top, f32 nearZ, f32 farZ)
		{
			Mat4 r = Mat4::identity();
			r.m[0] = 2.0f / (right - left);
			r.m[5] = 2.0f / (top - bottom);
			r.m[10] = 1.0f / (nearZ - farZ);
			r.m[12] = -(right + left) / (right - left);
			r.m[13] = -(top + bottom) / (top - bottom);
			r.m[14] = nearZ / (nearZ - farZ);
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

		// General 4x4 inverse, by cofactor expansion. Temporal reprojection needs the
		// inverse of a view-projection, which is neither affine nor orthogonal, so none of
		// the cheaper shortcuts apply.
		inline Mat4 inverse(const Mat4& mat)
		{
			const f32* m = mat.m;
			Mat4 out;
			f32* r = out.m;

			r[0]  =  m[5]*m[10]*m[15] - m[5]*m[11]*m[14] - m[9]*m[6]*m[15] + m[9]*m[7]*m[14] + m[13]*m[6]*m[11] - m[13]*m[7]*m[10];
			r[4]  = -m[4]*m[10]*m[15] + m[4]*m[11]*m[14] + m[8]*m[6]*m[15] - m[8]*m[7]*m[14] - m[12]*m[6]*m[11] + m[12]*m[7]*m[10];
			r[8]  =  m[4]*m[9]*m[15]  - m[4]*m[11]*m[13] - m[8]*m[5]*m[15] + m[8]*m[7]*m[13] + m[12]*m[5]*m[11] - m[12]*m[7]*m[9];
			r[12] = -m[4]*m[9]*m[14]  + m[4]*m[10]*m[13] + m[8]*m[5]*m[14] - m[8]*m[6]*m[13] - m[12]*m[5]*m[10] + m[12]*m[6]*m[9];

			r[1]  = -m[1]*m[10]*m[15] + m[1]*m[11]*m[14] + m[9]*m[2]*m[15] - m[9]*m[3]*m[14] - m[13]*m[2]*m[11] + m[13]*m[3]*m[10];
			r[5]  =  m[0]*m[10]*m[15] - m[0]*m[11]*m[14] - m[8]*m[2]*m[15] + m[8]*m[3]*m[14] + m[12]*m[2]*m[11] - m[12]*m[3]*m[10];
			r[9]  = -m[0]*m[9]*m[15]  + m[0]*m[11]*m[13] + m[8]*m[1]*m[15] - m[8]*m[3]*m[13] - m[12]*m[1]*m[11] + m[12]*m[3]*m[9];
			r[13] =  m[0]*m[9]*m[14]  - m[0]*m[10]*m[13] - m[8]*m[1]*m[14] + m[8]*m[2]*m[13] + m[12]*m[1]*m[10] - m[12]*m[2]*m[9];

			r[2]  =  m[1]*m[6]*m[15]  - m[1]*m[7]*m[14]  - m[5]*m[2]*m[15] + m[5]*m[3]*m[14] + m[13]*m[2]*m[7]  - m[13]*m[3]*m[6];
			r[6]  = -m[0]*m[6]*m[15]  + m[0]*m[7]*m[14]  + m[4]*m[2]*m[15] - m[4]*m[3]*m[14] - m[12]*m[2]*m[7]  + m[12]*m[3]*m[6];
			r[10] =  m[0]*m[5]*m[15]  - m[0]*m[7]*m[13]  - m[4]*m[1]*m[15] + m[4]*m[3]*m[13] + m[12]*m[1]*m[7]  - m[12]*m[3]*m[5];
			r[14] = -m[0]*m[5]*m[14]  + m[0]*m[6]*m[13]  + m[4]*m[1]*m[14] - m[4]*m[2]*m[13] - m[12]*m[1]*m[6]  + m[12]*m[2]*m[5];

			r[3]  = -m[1]*m[6]*m[11]  + m[1]*m[7]*m[10]  + m[5]*m[2]*m[11] - m[5]*m[3]*m[10] - m[9]*m[2]*m[7]   + m[9]*m[3]*m[6];
			r[7]  =  m[0]*m[6]*m[11]  - m[0]*m[7]*m[10]  - m[4]*m[2]*m[11] + m[4]*m[3]*m[10] + m[8]*m[2]*m[7]   - m[8]*m[3]*m[6];
			r[11] = -m[0]*m[5]*m[11]  + m[0]*m[7]*m[9]   + m[4]*m[1]*m[11] - m[4]*m[3]*m[9]  - m[8]*m[1]*m[7]   + m[8]*m[3]*m[5];
			r[15] =  m[0]*m[5]*m[10]  - m[0]*m[6]*m[9]   - m[4]*m[1]*m[10] + m[4]*m[2]*m[9]  + m[8]*m[1]*m[6]   - m[8]*m[2]*m[5];

			const f32 det = m[0]*r[0] + m[1]*r[4] + m[2]*r[8] + m[3]*r[12];
			if (det == 0.0f)
				return Mat4::identity();

			const f32 invDet = 1.0f / det;
			for (u32 i = 0; i < 16; i++)
			{
				r[i] *= invDet;
			}

			return out;
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
