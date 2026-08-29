#include "game/character_controller.h"

#include <cmath>

namespace mv::game
{
	void CharacterController::teleport(const math::Vec3& position, PhysicsWorld& physics)
	{
		position_ = position;
		previous_ = position;
		velocity_ = { 0.0f, 0.0f, 0.0f };
		grounded_ = false;

		physics.warpCharacter(position);
	}

	void CharacterController::update(
		f32 deltaSeconds,
		const math::Vec3& moveDirection,
		bool run,
		bool jump,
		PhysicsWorld& physics,
		const CharacterParams& params)
	{
		previous_ = position_;

		if (!physics.hasCharacter())
			return;

		// The steering: turn the velocity towards what was asked for, at the same
		// fraction of the remaining difference every second whatever the step size.
		// This is the whole of what makes walking feel like boots rather than a cart,
		// and it deliberately stays out of Bullet's hands.
		const f32 targetSpeed = params.walkSpeed * (run ? params.runMultiplier : 1.0f);

		math::Vec3 wanted{ 0.0f, 0.0f, 0.0f };

		const f32 moveLength = std::sqrt(
			moveDirection.x * moveDirection.x + moveDirection.z * moveDirection.z);

		if (moveLength > 0.001f)
		{
			wanted.x = moveDirection.x / moveLength * targetSpeed;
			wanted.z = moveDirection.z / moveLength * targetSpeed;
		}

		const f32 blend = 1.0f - std::exp(-params.acceleration * deltaSeconds);

		velocity_.x += (wanted.x - velocity_.x) * blend;
		velocity_.z += (wanted.z - velocity_.z) * blend;

		// The capsule takes it from here: gravity, the ground, steps, slopes and
		// whatever it runs into are all the sweep's business now.
		physics.moveCharacter(
			{ velocity_.x, 0.0f, velocity_.z },
			deltaSeconds,
			jump && grounded_,
			params.jumpSpeed);
	}

	void CharacterController::sync(PhysicsWorld& physics)
	{
		if (!physics.hasCharacter())
			return;

		position_ = physics.characterFeet();
		grounded_ = physics.characterGrounded();
	}
}
