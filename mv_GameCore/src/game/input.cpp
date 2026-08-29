#include "game/input.h"

#include <Windows.h>
#include <cstring>

namespace mv::game
{
	namespace
	{
		// The one place a virtual key is named. Indexed by EKey.
		constexpr int kVirtualKeys[(u32)EKey::eCount] =
		{
			'W',        // eForward
			'S',        // eBack
			'A',        // eLeft
			'D',        // eRight
			VK_SPACE,   // eJump
			VK_SHIFT,   // eRun
			VK_CONTROL, // eCrouch
			'E',        // eInteract
			'Q',        // eAltInteract
			VK_ESCAPE,  // ePause
			VK_RETURN,  // eConfirm
			'F',        // eDig
			'G',        // eBuild
		};
	}

	void Input::update(bool hasFocus)
	{
		std::memcpy(previous_, current_, sizeof(previous_));

		if (!hasFocus)
		{
			std::memset(current_, 0, sizeof(current_));
			return;
		}

		for (u32 i = 0; i < (u32)EKey::eCount; i++)
		{
			current_[i] = (GetAsyncKeyState(kVirtualKeys[i]) & 0x8000) != 0;
		}
	}
}
