#ifndef _MV_GAME_WORLD_H_
#define _MV_GAME_WORLD_H_

#include <vector>

#include "game/transform.h"
#include "util/types.h"

namespace mv
{
	namespace game
	{
		using namespace types;

		// A handle with a generation baked in, so a slot reused for a new entity does not
		// answer to the old entity's handle -- the mistake the renderer's plain-index
		// handles get away with only because nothing there outlives its owner.
		struct EntityHandle
		{
			u32 index = 0xFFFFFFFF;
			u32 generation = 0;

			bool operator==(const EntityHandle&) const = default;
		};

		constexpr EntityHandle kInvalidEntity{};

		struct Entity
		{
			Transform transform;

			// Which loaded primitive draws this entity, INVALID_HANDLE for none. The
			// renderer stays ignorant of entities; something walks the world each frame
			// and turns these into draws.
			u32 primitive = 0xFFFFFFFF;

			// The rigid body driving this entity, kInvalidBody for none. When set, the
			// physics world owns the transform and gameplay only reads it back.
			u32 physicsBody = 0xFFFFFFFF;

			char name[32]{};
		};

		// The entity registry: a pool with a free list and generation checks.
		//
		// Not an ECS. Components-as-arrays earn their machinery at ten thousand entities
		// with divergent shapes; a practice game has a player, some props and a camera,
		// and what those need is stable handles, O(1) create and destroy, and iteration
		// that skips the dead. A struct in a pool does all three legibly.
		class World
		{
		public:
			EntityHandle create(const char* name);
			void destroy(EntityHandle handle);

			// Null when the handle is stale or was never valid -- a destroyed entity's
			// handle stops answering rather than answering for someone else.
			Entity* get(EntityHandle handle);
			const Entity* get(EntityHandle handle) const;

			bool alive(EntityHandle handle) const { return get(handle) != nullptr; }

			u32 aliveCount() const { return aliveCount_; }

			// Visits every live entity: callback(EntityHandle, Entity&).
			template <typename F>
			void forEach(F&& callback)
			{
				for (u32 i = 0; i < (u32)slots_.size(); i++)
				{
					if (slots_[i].alive)
						callback(EntityHandle{ i, slots_[i].generation }, slots_[i].entity);
				}
			}

		private:
			struct Slot
			{
				Entity entity;
				u32 generation = 0;
				bool alive = false;
			};

			std::vector<Slot> slots_;
			std::vector<u32> freeList_;
			u32 aliveCount_ = 0;
		};
	}
}

#endif
