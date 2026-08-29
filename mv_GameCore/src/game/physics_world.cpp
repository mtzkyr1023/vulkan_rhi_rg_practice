#include "game/physics_world.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include <btBulletDynamicsCommon.h>
#include <BulletCollision/CollisionShapes/btHeightfieldTerrainShape.h>
#include <BulletCollision/CollisionShapes/btShapeHull.h>
#include <BulletCollision/CollisionDispatch/btGhostObject.h>
#include <BulletDynamics/Character/btKinematicCharacterController.h>

namespace mv::game
{
	namespace
	{
		// A collision group of the water plane's own: no body's group overlaps it, so
		// the plane pairs with nothing in the simulation -- only a ray whose filter
		// asks for the bit ever meets it.
		constexpr int kWaterGroup = 64;

		// User indices to name what a ray hit. Bullet initialises the field to -1, so
		// zero and below mean "nobody said", and everything here starts above.
		constexpr int kUserTerrain = 1;
		constexpr int kUserWater = 2;
		constexpr int kUserCharacter = 3;
		constexpr int kUserSculpt = 4;
		constexpr int kUserBodyBase = 100;

		// Contacts weaker than this never become events: a box at rest still reports
		// tiny stabilising impulses every step, and nobody wants to hear those.
		constexpr f32 kContactImpulseFloor = 1.5f;

		EHitKind kindFromUserIndex(int userIndex)
		{
			if (userIndex == kUserTerrain) return EHitKind::eTerrain;
			if (userIndex == kUserWater) return EHitKind::eWater;
			if (userIndex == kUserCharacter) return EHitKind::eCharacter;
			if (userIndex == kUserSculpt) return EHitKind::eSculpt;
			if (userIndex >= kUserBodyBase) return EHitKind::eBody;

			return EHitKind::eNone;
		}
	}
	namespace
	{
		// Bullet pushes its wireframes through this a segment at a time; the collector
		// only ever appends to whatever vector the current collect call pointed it at.
		class LineCollector : public btIDebugDraw
		{
		public:
			std::vector<DebugLine>* out = nullptr;
			u32 maxLines = 0;

			void drawLine(const btVector3& from, const btVector3& to, const btVector3& color) override
			{
				if (out == nullptr || out->size() >= maxLines)
					return;

				out->push_back({
					{ from.x(), from.y(), from.z() },
					{ to.x(), to.y(), to.z() },
					{ color.x(), color.y(), color.z() } });
			}

			void drawContactPoint(const btVector3&, const btVector3&, btScalar, int, const btVector3&) override {}
			void reportErrorWarning(const char*) override {}
			void draw3dText(const btVector3&, const char*) override {}
			void setDebugMode(int mode) override { mode_ = mode; }
			int getDebugMode() const override { return mode_; }

		private:
			int mode_ = DBG_DrawWireframe;
		};
	}

	// Everything Bullet lives here, where the header promised it would.
	struct PhysicsWorld::Impl
	{
		LineCollector lineCollector;
		std::unique_ptr<btDefaultCollisionConfiguration> configuration;
		std::unique_ptr<btCollisionDispatcher> dispatcher;
		std::unique_ptr<btDbvtBroadphase> broadphase;
		std::unique_ptr<btSequentialImpulseConstraintSolver> solver;
		std::unique_ptr<btDiscreteDynamicsWorld> world;

		// The terrain: shape, motion state and body live and die together.
		std::unique_ptr<btHeightfieldTerrainShape> terrainShape;
		std::unique_ptr<btDefaultMotionState> terrainMotion;
		std::unique_ptr<btRigidBody> terrainBody;

		// The water: a static plane in the ray-only group.
		std::unique_ptr<btStaticPlaneShape> waterShape;
		std::unique_ptr<btDefaultMotionState> waterMotion;
		std::unique_ptr<btRigidBody> waterBody;

		// The sculptable surface, one slot per chunk: triangles are copied in, the
		// BVH shape wraps them, and a whole slot is replaced when its chunk changes.
		struct SculptSlot
		{
			std::unique_ptr<btTriangleMesh> triangles;
			std::unique_ptr<btBvhTriangleMeshShape> shape;
			std::unique_ptr<btDefaultMotionState> motion;
			std::unique_ptr<btRigidBody> body;
		};

		std::vector<SculptSlot> sculptSlots;

		// The character: a ghost capsule and Bullet's kinematic controller acting on it.
		// The ghost pair callback lives on the broadphase and has to be installed before
		// the ghost is added, which is why initialize() owns it.
		std::unique_ptr<btGhostPairCallback> ghostPairCallback;
		std::unique_ptr<btPairCachingGhostObject> characterGhost;
		std::unique_ptr<btConvexShape> characterShape;
		std::unique_ptr<btKinematicCharacterController> character;

		// Feet-to-centre offset for the capsule, settled at creation.
		f32 characterHalfHeight = 0.0f;

		// Dynamic bodies by handle. A slot holds its shape too: Bullet does not own
		// shapes, and a sphere per body is cheap enough not to share.
		struct Slot
		{
			std::unique_ptr<btCollisionShape> shape;
			std::unique_ptr<btDefaultMotionState> motion;
			std::unique_ptr<btRigidBody> body;
		};

		std::vector<Slot> slots;
		std::vector<u32> freeList;
		u32 alive = 0;

		// Contacts harvested after each step, waiting for takeContactEvents.
		std::vector<ContactEvent> contacts;

		btRigidBody* body(BodyHandle handle)
		{
			if (handle >= slots.size())
				return nullptr;

			return slots[handle].body.get();
		}

		const btRigidBody* body(BodyHandle handle) const
		{
			return const_cast<Impl*>(this)->body(handle);
		}

		// The plumbing every dynamic prop shares once its shape is decided: inertia,
		// a slot, tuning that makes a thrown thing eventually stop, and a user index
		// so a ray can name it later.
		BodyHandle add(
			std::unique_ptr<btCollisionShape> shape,
			const math::Vec3& position,
			f32 mass,
			const math::Vec3& linearVelocity)
		{
			u32 index;

			if (!freeList.empty())
			{
				index = freeList.back();
				freeList.pop_back();
			}
			else
			{
				index = (u32)slots.size();
				slots.emplace_back();
			}

			Slot& slot = slots[index];

			slot.shape = std::move(shape);

			btVector3 inertia(0.0f, 0.0f, 0.0f);
			slot.shape->calculateLocalInertia(mass, inertia);

			btTransform transform;
			transform.setIdentity();
			transform.setOrigin(btVector3(position.x, position.y, position.z));

			slot.motion = std::make_unique<btDefaultMotionState>(transform);

			btRigidBody::btRigidBodyConstructionInfo info(mass, slot.motion.get(), slot.shape.get(), inertia);

			// Enough friction and damping that a thrown thing eventually stops instead of
			// rolling to the sea, and a little bounce so landing reads as landing.
			info.m_friction = 0.8f;
			info.m_rollingFriction = 0.15f;
			info.m_restitution = 0.25f;
			info.m_linearDamping = 0.05f;
			info.m_angularDamping = 0.2f;

			slot.body = std::make_unique<btRigidBody>(info);
			slot.body->setLinearVelocity(btVector3(linearVelocity.x, linearVelocity.y, linearVelocity.z));
			slot.body->setUserIndex(kUserBodyBase + (int)index);

			world->addRigidBody(slot.body.get());
			alive++;

			return index;
		}
	};

	PhysicsWorld::PhysicsWorld() = default;
	PhysicsWorld::~PhysicsWorld() = default;

	bool PhysicsWorld::initialize()
	{
		impl_ = std::make_unique<Impl>();

		impl_->configuration = std::make_unique<btDefaultCollisionConfiguration>();
		impl_->dispatcher = std::make_unique<btCollisionDispatcher>(impl_->configuration.get());
		impl_->broadphase = std::make_unique<btDbvtBroadphase>();
		impl_->solver = std::make_unique<btSequentialImpulseConstraintSolver>();

		impl_->world = std::make_unique<btDiscreteDynamicsWorld>(
			impl_->dispatcher.get(),
			impl_->broadphase.get(),
			impl_->solver.get(),
			impl_->configuration.get());

		impl_->world->setGravity(btVector3(0.0f, -9.81f, 0.0f));

		// Installed up front so a ghost object added later finds it: the character's
		// overlap bookkeeping runs through the broadphase's pair cache.
		impl_->ghostPairCallback = std::make_unique<btGhostPairCallback>();
		impl_->broadphase->getOverlappingPairCache()->setInternalGhostPairCallback(impl_->ghostPairCallback.get());

		// The drawer sits armed but silent: it costs nothing until debugDrawWorld is
		// actually called by collectDebugLines.
		impl_->world->setDebugDrawer(&impl_->lineCollector);

		return true;
	}

	void PhysicsWorld::deinitialize()
	{
		// Teardown order matters to Bullet: bodies out of the world before anything the
		// world points at goes away.
		if (impl_ && impl_->world)
		{
			if (impl_->character)
				impl_->world->removeAction(impl_->character.get());

			if (impl_->characterGhost)
				impl_->world->removeCollisionObject(impl_->characterGhost.get());

			for (auto& slot : impl_->slots)
			{
				if (slot.body)
					impl_->world->removeRigidBody(slot.body.get());
			}

			if (impl_->terrainBody)
				impl_->world->removeRigidBody(impl_->terrainBody.get());

			if (impl_->waterBody)
				impl_->world->removeRigidBody(impl_->waterBody.get());

			for (auto& slot : impl_->sculptSlots)
			{
				if (slot.body)
					impl_->world->removeRigidBody(slot.body.get());
			}
		}

		impl_.reset();
	}

	bool PhysicsWorld::isReady() const
	{
		return impl_ && impl_->world;
	}

	void PhysicsWorld::step(f32 deltaSeconds)
	{
		if (!isReady())
			return;

		// maxSubSteps zero: exactly this step, no interpolation, no catching up. The
		// caller's fixed clock already did both jobs.
		impl_->world->stepSimulation(deltaSeconds, 0);

		// Harvest this step's contacts from the solver's manifolds while they are
		// fresh: the applied impulses are this step's answers and the next step
		// overwrites them. One event per manifold, carrying its strongest point.
		const int manifoldCount = impl_->dispatcher->getNumManifolds();

		for (int m = 0; m < manifoldCount; m++)
		{
			const btPersistentManifold* manifold = impl_->dispatcher->getManifoldByIndexInternal(m);

			f32 bestImpulse = 0.0f;
			btVector3 bestPoint(0.0f, 0.0f, 0.0f);

			const int pointCount = manifold->getNumContacts();

			for (int p = 0; p < pointCount; p++)
			{
				const btManifoldPoint& point = manifold->getContactPoint(p);

				if (point.getAppliedImpulse() > bestImpulse)
				{
					bestImpulse = point.getAppliedImpulse();
					bestPoint = point.getPositionWorldOnB();
				}
			}

			if (bestImpulse < kContactImpulseFloor)
				continue;

			const int userA = manifold->getBody0()->getUserIndex();
			const int userB = manifold->getBody1()->getUserIndex();

			ContactEvent event{};
			event.kindA = kindFromUserIndex(userA);
			event.kindB = kindFromUserIndex(userB);
			event.bodyA = event.kindA == EHitKind::eBody ? (BodyHandle)(userA - kUserBodyBase) : kInvalidBody;
			event.bodyB = event.kindB == EHitKind::eBody ? (BodyHandle)(userB - kUserBodyBase) : kInvalidBody;
			event.position = { bestPoint.x(), bestPoint.y(), bestPoint.z() };
			event.impulse = bestImpulse;

			impl_->contacts.push_back(event);
		}
	}

	void PhysicsWorld::takeContactEvents(std::vector<ContactEvent>& out)
	{
		if (!isReady())
			return;

		out.insert(out.end(), impl_->contacts.begin(), impl_->contacts.end());
		impl_->contacts.clear();
	}

	void PhysicsWorld::setHeightField(const f32* heights, u32 resolution, f32 worldSize, f32 heightScale)
	{
		if (!isReady() || heights == nullptr || resolution < 2)
			return;

		if (impl_->terrainBody)
		{
			impl_->world->removeRigidBody(impl_->terrainBody.get());
			impl_->terrainBody.reset();
			impl_->terrainMotion.reset();
			impl_->terrainShape.reset();
		}

		// Normalised heights with the metres pushed into the local scaling: x and z
		// stretch grid cells to world spacing, y stretches [0, 1] to the height range.
		impl_->terrainShape = std::make_unique<btHeightfieldTerrainShape>(
			(int)resolution, (int)resolution,
			heights,
			1.0f,
			0.0f, 1.0f,
			1,
			PHY_FLOAT,
			false);

		const f32 spacing = worldSize / (f32)(resolution - 1);

		impl_->terrainShape->setLocalScaling(btVector3(spacing, heightScale, spacing));

		// Bullet centres the height range on the body's origin, so the body sits at half
		// the range: local height 0 then lands on world height 0.
		btTransform transform;
		transform.setIdentity();
		transform.setOrigin(btVector3(0.0f, heightScale * 0.5f, 0.0f));

		impl_->terrainMotion = std::make_unique<btDefaultMotionState>(transform);

		btRigidBody::btRigidBodyConstructionInfo info(
			0.0f, impl_->terrainMotion.get(), impl_->terrainShape.get());

		info.m_friction = 0.9f;

		impl_->terrainBody = std::make_unique<btRigidBody>(info);
		impl_->terrainBody->setUserIndex(kUserTerrain);
		impl_->terrainBody->setCollisionFlags(
			impl_->terrainBody->getCollisionFlags() | btCollisionObject::CF_DISABLE_VISUALIZE_OBJECT);

		impl_->world->addRigidBody(impl_->terrainBody.get());
	}

	void PhysicsWorld::setSculptMesh(u32 slot, const f32* positions, u32 vertexCount, u32 strideBytes)
	{
		if (!isReady())
			return;

		if (slot >= impl_->sculptSlots.size())
			impl_->sculptSlots.resize(slot + 1);

		Impl::SculptSlot& s = impl_->sculptSlots[slot];

		if (s.body)
		{
			impl_->world->removeRigidBody(s.body.get());
			s.body.reset();
			s.motion.reset();
			s.shape.reset();
			s.triangles.reset();
		}

		if (positions == nullptr || vertexCount < 3)
			return;

		s.triangles = std::make_unique<btTriangleMesh>();

		const u32 strideFloats = strideBytes / sizeof(f32);

		for (u32 v = 0; v + 2 < vertexCount; v += 3)
		{
			const f32* a = positions + (size_t)(v + 0) * strideFloats;
			const f32* b = positions + (size_t)(v + 1) * strideFloats;
			const f32* c = positions + (size_t)(v + 2) * strideFloats;

			s.triangles->addTriangle(
				btVector3(a[0], a[1], a[2]),
				btVector3(b[0], b[1], b[2]),
				btVector3(c[0], c[1], c[2]));
		}

		s.shape = std::make_unique<btBvhTriangleMeshShape>(s.triangles.get(), true);

		btTransform transform;
		transform.setIdentity();

		s.motion = std::make_unique<btDefaultMotionState>(transform);

		btRigidBody::btRigidBodyConstructionInfo info(0.0f, s.motion.get(), s.shape.get());

		info.m_friction = 0.9f;

		s.body = std::make_unique<btRigidBody>(info);
		s.body->setUserIndex(kUserSculpt);

		impl_->world->addRigidBody(s.body.get());
	}

	void PhysicsWorld::setWaterPlane(f32 level)
	{
		if (!isReady())
			return;

		if (impl_->waterBody)
		{
			impl_->world->removeRigidBody(impl_->waterBody.get());
			impl_->waterBody.reset();
			impl_->waterMotion.reset();
			impl_->waterShape.reset();
		}

		impl_->waterShape = std::make_unique<btStaticPlaneShape>(btVector3(0.0f, 1.0f, 0.0f), 0.0f);

		btTransform transform;
		transform.setIdentity();
		transform.setOrigin(btVector3(0.0f, level, 0.0f));

		impl_->waterMotion = std::make_unique<btDefaultMotionState>(transform);

		btRigidBody::btRigidBodyConstructionInfo info(
			0.0f, impl_->waterMotion.get(), impl_->waterShape.get());

		impl_->waterBody = std::make_unique<btRigidBody>(info);
		impl_->waterBody->setUserIndex(kUserWater);
		impl_->waterBody->setCollisionFlags(
			impl_->waterBody->getCollisionFlags() | btCollisionObject::CF_DISABLE_VISUALIZE_OBJECT);

		// Group and mask both the water bit: no body's group carries it, so the
		// simulation never pairs with the plane -- it exists for raycast() alone.
		impl_->world->addRigidBody(impl_->waterBody.get(), kWaterGroup, kWaterGroup);
	}

	BodyHandle PhysicsWorld::addSphere(
		const math::Vec3& position,
		f32 radius,
		f32 mass,
		const math::Vec3& linearVelocity)
	{
		if (!isReady())
			return kInvalidBody;

		return impl_->add(std::make_unique<btSphereShape>(radius), position, mass, linearVelocity);
	}

	BodyHandle PhysicsWorld::addBox(
		const math::Vec3& position,
		const math::Vec3& halfExtents,
		f32 mass,
		const math::Vec3& linearVelocity)
	{
		if (!isReady())
			return kInvalidBody;

		return impl_->add(
			std::make_unique<btBoxShape>(btVector3(halfExtents.x, halfExtents.y, halfExtents.z)),
			position, mass, linearVelocity);
	}

	BodyHandle PhysicsWorld::addConvexHull(
		const math::Vec3& position,
		const f32* points,
		u32 pointCount,
		u32 strideBytes,
		f32 mass,
		const math::Vec3& linearVelocity)
	{
		if (!isReady() || points == nullptr || pointCount < 4)
			return kInvalidBody;

		// The render mesh's cloud of points, then btShapeHull to boil it down: a raw
		// hull over thousands of vertices would make GJK pay per contact, every step,
		// for detail no collision can see.
		btConvexHullShape raw(points, (int)pointCount, (int)strideBytes);

		btShapeHull hull(&raw);

		if (!hull.buildHull(raw.getMargin()))
			return kInvalidBody;

		auto shape = std::make_unique<btConvexHullShape>(
			(const btScalar*)hull.getVertexPointer(), hull.numVertices(), (int)sizeof(btVector3));

		return impl_->add(std::move(shape), position, mass, linearVelocity);
	}

	void PhysicsWorld::removeBody(BodyHandle handle)
	{
		btRigidBody* body = impl_ ? impl_->body(handle) : nullptr;

		if (body == nullptr)
			return;

		impl_->world->removeRigidBody(body);

		Impl::Slot& slot = impl_->slots[handle];
		slot.body.reset();
		slot.motion.reset();
		slot.shape.reset();

		impl_->freeList.push_back(handle);
		impl_->alive--;
	}

	bool PhysicsWorld::bodyMatrix(BodyHandle handle, math::Mat4& out) const
	{
		const btRigidBody* body = impl_ ? impl_->body(handle) : nullptr;

		if (body == nullptr)
			return false;

		btTransform transform;
		body->getMotionState()->getWorldTransform(transform);

		// Bullet's OpenGL layout is column-major with the basis in the first three
		// columns -- which in memory is exactly this codebase's row-major row-vector
		// matrix: basis vectors in rows 0..2, translation in row 3.
		transform.getOpenGLMatrix(out.m);

		return true;
	}

	bool PhysicsWorld::bodyAsleep(BodyHandle handle) const
	{
		const btRigidBody* body = impl_ ? impl_->body(handle) : nullptr;

		return body != nullptr && !body->isActive();
	}

	u32 PhysicsWorld::bodyCount() const
	{
		return impl_ ? impl_->alive : 0;
	}

	bool PhysicsWorld::createCharacter(
		const math::Vec3& feetPosition,
		f32 radius,
		f32 totalHeight,
		f32 stepHeight,
		f32 gravity,
		f32 maxSlopeNormalY)
	{
		if (!isReady() || impl_->character)
			return false;

		// btCapsuleShape takes the cylinder part; the caller speaks in feet-to-crown.
		const f32 cylinderHeight = (std::max)(totalHeight - 2.0f * radius, 0.1f);

		impl_->characterHalfHeight = (cylinderHeight + 2.0f * radius) * 0.5f;
		impl_->characterShape = std::make_unique<btCapsuleShape>(radius, cylinderHeight);

		impl_->characterGhost = std::make_unique<btPairCachingGhostObject>();
		impl_->characterGhost->setCollisionShape(impl_->characterShape.get());
		impl_->characterGhost->setCollisionFlags(btCollisionObject::CF_CHARACTER_OBJECT);
		impl_->characterGhost->setUserIndex(kUserCharacter);

		btTransform transform;
		transform.setIdentity();
		transform.setOrigin(btVector3(
			feetPosition.x,
			feetPosition.y + impl_->characterHalfHeight,
			feetPosition.z));

		impl_->characterGhost->setWorldTransform(transform);

		// The up axis is not optional: Bullet's default for this controller is +X, a
		// trap laid for exactly this call site.
		impl_->character = std::make_unique<btKinematicCharacterController>(
			impl_->characterGhost.get(),
			impl_->characterShape.get(),
			stepHeight,
			btVector3(0.0f, 1.0f, 0.0f));

		impl_->character->setGravity(btVector3(0.0f, -gravity, 0.0f));
		impl_->character->setMaxSlope(std::acos((std::min)((std::max)(maxSlopeNormalY, 0.0f), 1.0f)));

		impl_->world->addCollisionObject(
			impl_->characterGhost.get(),
			btBroadphaseProxy::CharacterFilter,
			btBroadphaseProxy::StaticFilter | btBroadphaseProxy::DefaultFilter);

		impl_->world->addAction(impl_->character.get());

		return true;
	}

	bool PhysicsWorld::hasCharacter() const
	{
		return impl_ && impl_->character != nullptr;
	}

	void PhysicsWorld::warpCharacter(const math::Vec3& feetPosition)
	{
		if (!hasCharacter())
			return;

		impl_->character->warp(btVector3(
			feetPosition.x,
			feetPosition.y + impl_->characterHalfHeight,
			feetPosition.z));

		impl_->character->setWalkDirection(btVector3(0.0f, 0.0f, 0.0f));
	}

	void PhysicsWorld::moveCharacter(
		const math::Vec3& walkVelocity,
		f32 deltaSeconds,
		bool jump,
		f32 jumpSpeed)
	{
		if (!hasCharacter())
			return;

		// The controller wants the step's displacement, not a velocity, which is why the
		// delta crosses this boundary: it must be the same delta the next step() runs.
		impl_->character->setWalkDirection(btVector3(
			walkVelocity.x * deltaSeconds,
			0.0f,
			walkVelocity.z * deltaSeconds));

		if (jump && impl_->character->canJump())
			impl_->character->jump(btVector3(0.0f, jumpSpeed, 0.0f));
	}

	math::Vec3 PhysicsWorld::characterFeet() const
	{
		if (!hasCharacter())
			return { 0.0f, 0.0f, 0.0f };

		const btVector3 centre = impl_->characterGhost->getWorldTransform().getOrigin();

		return { centre.x(), centre.y() - impl_->characterHalfHeight, centre.z() };
	}

	bool PhysicsWorld::characterGrounded() const
	{
		return hasCharacter() && impl_->character->onGround();
	}

	void PhysicsWorld::collectDebugLines(std::vector<DebugLine>& out, u32 maxLines) const
	{
		if (!isReady() || maxLines == 0)
			return;

		impl_->lineCollector.out = &out;
		impl_->lineCollector.maxLines = maxLines;

		impl_->world->debugDrawWorld();

		impl_->lineCollector.out = nullptr;
	}

	bool PhysicsWorld::raycast(
		const math::Vec3& from,
		const math::Vec3& direction,
		f32 maxDistance,
		RayHit& out,
		bool includeWater) const
	{
		if (!isReady() || maxDistance <= 0.0f)
			return false;

		const btVector3 origin(from.x, from.y, from.z);
		const btVector3 target(
			from.x + direction.x * maxDistance,
			from.y + direction.y * maxDistance,
			from.z + direction.z * maxDistance);

		btCollisionWorld::ClosestRayResultCallback callback(origin, target);

		// The ray's group answers every proxy's mask (the water plane only accepts its
		// own bit); the ray's own mask picks what it can land on. The character is
		// always out: rays leave from its eye, and a ray that opens by hitting its
		// owner answers nothing.
		callback.m_collisionFilterGroup = btBroadphaseProxy::AllFilter;
		callback.m_collisionFilterMask = btBroadphaseProxy::AllFilter & ~btBroadphaseProxy::CharacterFilter;

		if (!includeWater)
			callback.m_collisionFilterMask &= ~kWaterGroup;

		impl_->world->rayTest(origin, target, callback);

		if (!callback.hasHit())
			return false;

		out.position = {
			callback.m_hitPointWorld.x(),
			callback.m_hitPointWorld.y(),
			callback.m_hitPointWorld.z() };
		out.normal = {
			callback.m_hitNormalWorld.x(),
			callback.m_hitNormalWorld.y(),
			callback.m_hitNormalWorld.z() };
		out.distance = maxDistance * callback.m_closestHitFraction;

		const int userIndex = callback.m_collisionObject ? callback.m_collisionObject->getUserIndex() : 0;

		// The same naming the contact events use -- one mapping, or the day a new
		// kind arrives one of the two forgets it (which is how the sculpt mesh spent
		// an afternoon answering rays as "nothing").
		out.kind = kindFromUserIndex(userIndex);
		out.body = out.kind == EHitKind::eBody ? (BodyHandle)(userIndex - kUserBodyBase) : kInvalidBody;

		return true;
	}
}
