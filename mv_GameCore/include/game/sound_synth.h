#ifndef _MV_GAME_SOUND_SYNTH_H_
#define _MV_GAME_SOUND_SYNTH_H_

#include <vector>

#include "util/types.h"

namespace mv
{
	namespace game
	{
		namespace synth
		{
			using namespace types;

			// The project's sound "assets", synthesised: mono 44.1 kHz float buffers,
			// ready for AudioSystem::create. No files to ship, no licences to read,
			// and the character of a sound is a handful of numbers away -- which for a
			// practice project beats a sample library. Real recordings can replace any
			// of these without the callers noticing: they only ever see a SoundHandle.

			constexpr u32 kSampleRate = 44100;

			// A body landing on something hard: pitch-dropping thump plus a noise click.
			std::vector<f32> impact();

			// A body going through the water surface: shaped noise, darkening as it sinks.
			std::vector<f32> splash();

			// One footfall on grass: a short, dull noise tap.
			std::vector<f32> footstep();

			// Leaving the ground: a small upward swish.
			std::vector<f32> jump();

			// Coming back to it: a softer, lower cousin of the impact.
			std::vector<f32> land();
		}
	}
}

#endif
