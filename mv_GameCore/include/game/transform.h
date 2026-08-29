#ifndef _MV_GAME_TRANSFORM_H_
#define _MV_GAME_TRANSFORM_H_

#include <cmath>

#include "util/math.h"
#include "util/types.h"

namespace mv
{
	namespace game
	{
		using namespace types;

		// Where a thing is, which way it faces and how big it is.
		//
		// Rotation is yaw-pitch-roll rather than a quaternion, which is the honest choice
		// for what this engine does with it: the camera and every character in it turn
		// about the world's up axis and look up or down, and Euler angles say that
		// directly. The day something tumbles freely is the day a quaternion earns its
		// bookkeeping.
		struct Transform
		{
			math::Vec3 position{ 0.0f, 0.0f, 0.0f };

			// Radians: yaw about world Y, then pitch about local X, then roll about the
			// view axis. The same convention the free-fly camera uses.
			math::Vec3 rotation{ 0.0f, 0.0f, 0.0f };

			math::Vec3 scale{ 1.0f, 1.0f, 1.0f };

			// The engine's camera convention exactly: yaw zero faces -z, and pitch lifts
			// towards +y. One convention shared by the camera, the characters and the
			// entities is worth more than any particular choice of it.
			math::Vec3 forward() const
			{
				const f32 yaw = rotation.y;
				const f32 pitch = rotation.x;

				return {
					std::sin(yaw) * std::cos(pitch),
					std::sin(pitch),
					-std::cos(yaw) * std::cos(pitch),
				};
			}

			math::Vec3 right() const
			{
				const f32 yaw = rotation.y;

				return { std::cos(yaw), 0.0f, std::sin(yaw) };
			}

			// Row-vector, row-major, matching util/math.h: scale, then roll, pitch, yaw,
			// then move. Composed so that (0, 0, -1) lands on forward() -- the matrix and
			// the helper answering differently is how a prop faces one way and walks
			// another.
			math::Mat4 matrix() const
			{
				const f32 cy = std::cos(rotation.y), sy = std::sin(rotation.y);
				const f32 cp = std::cos(rotation.x), sp = std::sin(rotation.x);
				const f32 cr = std::cos(rotation.z), sr = std::sin(rotation.z);

				const f32 r00 = cr * cy - sr * sp * sy;
				const f32 r01 = sr * cp;
				const f32 r02 = cr * sy + sr * sp * cy;
				const f32 r10 = -sr * cy - cr * sp * sy;
				const f32 r11 = cr * cp;
				const f32 r12 = -sr * sy + cr * sp * cy;
				const f32 r20 = -cp * sy;
				const f32 r21 = -sp;
				const f32 r22 = cp * cy;

				math::Mat4 m = math::Mat4::identity();

				m.m[0] = r00 * scale.x; m.m[1] = r01 * scale.x; m.m[2] = r02 * scale.x;
				m.m[4] = r10 * scale.y; m.m[5] = r11 * scale.y; m.m[6] = r12 * scale.y;
				m.m[8] = r20 * scale.z; m.m[9] = r21 * scale.z; m.m[10] = r22 * scale.z;
				m.m[12] = position.x; m.m[13] = position.y; m.m[14] = position.z;

				return m;
			}
		};
	}
}

#endif
