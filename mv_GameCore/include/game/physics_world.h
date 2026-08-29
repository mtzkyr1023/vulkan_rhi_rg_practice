#ifndef _MV_GAME_PHYSICS_WORLD_H_
#define _MV_GAME_PHYSICS_WORLD_H_

#include <memory>
#include <vector>

#include "util/math.h"
#include "util/types.h"

namespace mv
{
	namespace game
	{
		using namespace types;

		using BodyHandle = u32;
		constexpr BodyHandle kInvalidBody = 0xFFFFFFFF;

		// What a query touched -- a ray's landing or a contact's participant. eWater
		// is a query-only surface: rays see it, bodies fall straight through.
		enum class EHitKind : u32
		{
			eNone,
			eTerrain,
			eWater,
			eBody,
			eCharacter,
			eSculpt,
		};

		struct RayHit
		{
			math::Vec3 position{};
			math::Vec3 normal{};
			f32 distance = 0.0f;
			EHitKind kind = EHitKind::eNone;

			// Valid only when kind == eBody.
			BodyHandle body = kInvalidBody;
		};

		// One collision worth hearing about, harvested from the solver after a step.
		// The impulse is the strongest contact point's, in Newton-seconds: a box set
		// gently on the ground reports nearly zero, a box thrown at a rock does not --
		// which is exactly the number a sound or a damage rule wants.
		struct ContactEvent
		{
			EHitKind kindA = EHitKind::eNone;
			EHitKind kindB = EHitKind::eNone;

			// Valid for the sides whose kind is eBody.
			BodyHandle bodyA = kInvalidBody;
			BodyHandle bodyB = kInvalidBody;

			math::Vec3 position{};
			f32 impulse = 0.0f;
		};

		// One wireframe segment out of Bullet's own debug draw.
		struct DebugLine
		{
			math::Vec3 from{};
			math::Vec3 to{};
			math::Vec3 color{};
		};

		// Bullet, behind a door.
		//
		// The header names no bt type on purpose: everything that includes it -- the
		// engine, the world, whoever comes later -- compiles without Bullet's headers, and
		// the day the physics library changes, this file does not. The price is a pimpl
		// and a handle table, both of which this codebase already speaks fluently.
		//
		// Stepping is the caller's fixed clock's business: step() advances exactly one
		// step of the given length, with Bullet's own substepping switched off, so the
		// simulation and the character controller march to the same drum.
		class PhysicsWorld
		{
		public:
			PhysicsWorld();
			~PhysicsWorld();

			bool initialize();
			void deinitialize();

			bool isReady() const;

			// One fixed step. Contacts strong enough to matter are collected as it runs;
			// take them with takeContactEvents.
			void step(f32 deltaSeconds);

			// Appends the contact events collected since the last call and clears the
			// backlog. Call once per frame after the fixed steps have drained -- events
			// from every step of the frame arrive together.
			void takeContactEvents(std::vector<ContactEvent>& out);

			// Replaces the terrain body. The heights array is normalised [0, 1] row-major,
			// the same buffer the renderer's heightmap keeps -- and Bullet references it
			// rather than copying, so it must stay alive and in place until the next call.
			void setHeightField(const f32* heights, u32 resolution, f32 worldSize, f32 heightScale);

			// Replaces one chunk of the sculptable surface with a triangle soup:
			// positions at the given byte stride, three vertices per triangle,
			// copied in. Each slot is its own static body, rebuilt whole when its
			// chunk changes -- at stroke rate and per-chunk triangle counts, the
			// BVH build is cheap enough not to be worth incremental cleverness.
			void setSculptMesh(u32 slot, const f32* positions, u32 vertexCount, u32 strideBytes);

			// A flat surface rays can land on -- the water. It belongs to no collision
			// group any body listens to, so nothing floats on it or bumps into it; only
			// raycast() sees it, and only when asked to.
			void setWaterPlane(f32 level);

			// A dynamic sphere, thrown: the pieces a prop needs to fall, bounce and roll.
			BodyHandle addSphere(
				const math::Vec3& position,
				f32 radius,
				f32 mass,
				const math::Vec3& linearVelocity);

			BodyHandle addBox(
				const math::Vec3& position,
				const math::Vec3& halfExtents,
				f32 mass,
				const math::Vec3& linearVelocity);

			// A convex hull built from raw vertex positions -- x, y, z at the given byte
			// stride, in the body's local space, so centre them before calling. The cloud
			// of points is simplified down to something a solver can afford before it
			// becomes the shape; the count can be a whole render mesh.
			BodyHandle addConvexHull(
				const math::Vec3& position,
				const f32* points,
				u32 pointCount,
				u32 strideBytes,
				f32 mass,
				const math::Vec3& linearVelocity);

			void removeBody(BodyHandle handle);

			// The body's transform in this codebase's convention -- row-major, row-vector,
			// basis in the first three rows, translation in the fourth. False for a stale
			// handle.
			bool bodyMatrix(BodyHandle handle, math::Mat4& out) const;

			// True once the body has come to rest -- Bullet's own sleep state, which is
			// what "the helmet stopped rolling" means.
			bool bodyAsleep(BodyHandle handle) const;

			u32 bodyCount() const;

			// --- the character -------------------------------------------------------
			//
			// One kinematic capsule, Bullet's own character controller behind it: convex
			// sweeps against everything in the world, so the player collides with the
			// terrain, steps over what stepHeight allows, and stops against thrown props
			// -- none of which a heightfield lookup could say.
			//
			// Positions at this boundary are always the FEET, in world space; the capsule
			// centre is the implementation's business.

			bool createCharacter(
				const math::Vec3& feetPosition,
				f32 radius,
				f32 totalHeight,
				f32 stepHeight,
				f32 gravity,
				f32 maxSlopeNormalY);

			bool hasCharacter() const;

			// A teleport: no velocity survives it.
			void warpCharacter(const math::Vec3& feetPosition);

			// Per fixed step, before step(): where to walk this step, and whether to
			// jump. The controller consumes the walk as a displacement, so the delta is
			// needed here and must match the step about to run.
			void moveCharacter(
				const math::Vec3& walkVelocity,
				f32 deltaSeconds,
				bool jump,
				f32 jumpSpeed);

			math::Vec3 characterFeet() const;
			bool characterGrounded() const;

			// Closest hit along the ray against everything at once -- terrain, props, the
			// water plane if includeWater -- through Bullet's own broadphase, which is why
			// this replaced the heightfield bisection: one call, every surface. The
			// character's own capsule is always skipped, so an eye-level ray does not
			// start by hitting its owner.
			bool raycast(
				const math::Vec3& from,
				const math::Vec3& direction,
				f32 maxDistance,
				RayHit& out,
				bool includeWater = true) const;

			// Appends Bullet's wireframes for the shapes worth seeing -- props and the
			// character capsule -- up to maxLines. The terrain and the water plane are
			// flagged out at creation: half a million heightfield edges is not a debug
			// view, it is a screen of green.
			void collectDebugLines(std::vector<DebugLine>& out, u32 maxLines) const;

		private:
			struct Impl;
			std::unique_ptr<Impl> impl_;
		};
	}
}

#endif
