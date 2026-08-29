#ifndef _MV_GAME_INPUT_H_
#define _MV_GAME_INPUT_H_

#include "util/types.h"

namespace mv
{
	namespace game
	{
		using namespace types;

		// The keys a game reads, by role rather than by scan code. Adding one is adding an
		// entry here and its mapping in input.cpp -- no caller names a virtual key.
		enum class EKey : u32
		{
			eForward,
			eBack,
			eLeft,
			eRight,
			eJump,
			eRun,
			eCrouch,
			eInteract,
			eAltInteract,
			ePause,
			eConfirm,
			eDig,
			eBuild,

			eCount,
		};

		// Polled keyboard state with edge detection.
		//
		// Polled rather than message-driven on purpose: a game loop asks "is this held
		// now" every fixed step, and a message queue answers a different question --
		// "what happened since last time" -- that then has to be reassembled into state
		// anyway. One update() per frame snapshots everything; pressed() and released()
		// are the differences against the previous snapshot, so an edge is never missed
		// and never double-counted no matter how many fixed steps consume it.
		class Input
		{
		public:
			// Once per frame, before the simulation drains its steps. hasFocus gates the
			// whole snapshot: a background window reading the keyboard is how a game types
			// into someone's editor.
			void update(bool hasFocus);

			bool held(EKey key) const { return current_[(u32)key]; }
			bool pressed(EKey key) const { return current_[(u32)key] && !previous_[(u32)key]; }
			bool released(EKey key) const { return !current_[(u32)key] && previous_[(u32)key]; }

		private:
			bool current_[(u32)EKey::eCount]{};
			bool previous_[(u32)EKey::eCount]{};
		};
	}
}

#endif
