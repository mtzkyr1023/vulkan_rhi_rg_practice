#ifndef _MV_GAME_CHARACTER_CONTROLLER_H_
#define _MV_GAME_CHARACTER_CONTROLLER_H_

#include "game/physics_world.h"
#include "util/math.h"
#include "util/types.h"

namespace mv
{
	namespace game
	{
		using namespace types;

		struct CharacterParams
		{
			// Metres per second on the flat, and the run multiplier on top.
			f32 walkSpeed = 6.0f;
			f32 runMultiplier = 2.5f;

			// Metres per second straight up at the moment of a jump, and the gravity that
			// takes it back. 9.81 reads floaty at game scale; games lie upward.
			f32 jumpSpeed = 7.0f;
			f32 gravity = 22.0f;

			// The capsule: feet to crown, its radius, and the tallest ledge it walks
			// straight over instead of stopping against.
			f32 height = 1.75f;
			f32 radius = 0.5f;
			f32 stepHeight = 0.4f;

			// Metres from the feet to the camera. Deliberately above the capsule's crown:
			// the world here is oversized -- the grass alone stands taller than a person
			// -- and a strictly anatomical eye sits buried in the lawn.
			f32 eyeHeight = 2.4f;

			// Ground steeper than this stops being walkable. Compared against the ground
			// normal's y; the physics side turns it into a slope angle.
			f32 maxSlopeNormalY = 0.55f;

			// How fast velocity turns towards the wanted direction, per second. Low is
			// ice, high is stop-on-a-dime; ten is boots on grass.
			f32 acceleration = 10.0f;
		};

		// The character, riding Bullet's kinematic capsule.
		//
		// What stays here is the game feel and the frame plumbing: the exponential
		// steering that makes walking read as boots rather than a cart, the jump edge,
		// and the previous/current pair the renderer interpolates between. What left is
		// every collision question -- the capsule sweeps against the real world now, so
		// the player steps over ledges, stops against thrown props and slides off
		// overhangs, none of which the old heightfield lookup could say.
		//
		// The split follows the physics tick: update() before the step steers the
		// capsule, sync() after it reads back where the world actually let it go.
		class CharacterController
		{
		public:
			// Places the character with no velocity, both interpolation endpoints on the
			// spot -- a teleport, not a move. Warps the physics capsule with it.
			void teleport(const math::Vec3& position, PhysicsWorld& physics);

			// One fixed step's steering, before physics.step(). moveDirection is the
			// wanted direction in world XZ (zero for no input).
			void update(
				f32 deltaSeconds,
				const math::Vec3& moveDirection,
				bool run,
				bool jump,
				PhysicsWorld& physics,
				const CharacterParams& params);

			// After physics.step(): where the world actually put the capsule.
			void sync(PhysicsWorld& physics);

			bool grounded() const { return grounded_; }

			const math::Vec3& position() const { return position_; }

			// Feet and eye, blended between the previous and current step by the clock's
			// alpha: what the renderer should draw.
			math::Vec3 interpolated(f32 alpha) const
			{
				return previous_ + (position_ - previous_) * alpha;
			}

			math::Vec3 eyePosition(f32 alpha, const CharacterParams& params) const
			{
				math::Vec3 eye = interpolated(alpha);
				eye.y += params.eyeHeight;

				return eye;
			}

		private:
			math::Vec3 position_{ 0.0f, 0.0f, 0.0f };
			math::Vec3 previous_{ 0.0f, 0.0f, 0.0f };

			// Horizontal steering only: the vertical part belongs to the capsule.
			math::Vec3 velocity_{ 0.0f, 0.0f, 0.0f };

			bool grounded_ = false;
		};
	}
}

#endif
