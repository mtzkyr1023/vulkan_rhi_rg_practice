#include "game/world.h"

#include <cstring>

namespace mv::game
{
	EntityHandle World::create(const char* name)
	{
		u32 index;

		if (!freeList_.empty())
		{
			index = freeList_.back();
			freeList_.pop_back();
		}
		else
		{
			index = (u32)slots_.size();
			slots_.emplace_back();
		}

		Slot& slot = slots_[index];
		slot.entity = Entity{};
		slot.alive = true;

		if (name != nullptr)
		{
			std::strncpy(slot.entity.name, name, sizeof(slot.entity.name) - 1);
			slot.entity.name[sizeof(slot.entity.name) - 1] = '\0';
		}

		aliveCount_++;

		return { index, slot.generation };
	}

	void World::destroy(EntityHandle handle)
	{
		Entity* entity = get(handle);

		if (entity == nullptr)
			return;

		Slot& slot = slots_[handle.index];

		slot.alive = false;

		// The bump is what retires every outstanding handle to this slot: they carry the
		// old generation and stop matching.
		slot.generation++;

		freeList_.push_back(handle.index);
		aliveCount_--;
	}

	Entity* World::get(EntityHandle handle)
	{
		if (handle.index >= (u32)slots_.size())
			return nullptr;

		Slot& slot = slots_[handle.index];

		if (!slot.alive || slot.generation != handle.generation)
			return nullptr;

		return &slot.entity;
	}

	const Entity* World::get(EntityHandle handle) const
	{
		return const_cast<World*>(this)->get(handle);
	}
}
