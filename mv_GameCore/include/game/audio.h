#ifndef _MV_GAME_AUDIO_H_
#define _MV_GAME_AUDIO_H_

#include <memory>

#include "util/math.h"
#include "util/types.h"

namespace mv
{
	namespace game
	{
		using namespace types;

		using SoundHandle = u32;
		constexpr SoundHandle kInvalidSound = 0xFFFFFFFF;

		// XAudio2, behind the same kind of door Bullet is: no platform type in the
		// header, a handle table behind it.
		//
		// Sounds are mono 44.1 kHz float sample buffers, registered once and played
		// many times over a small pool of voices; play() is fire-and-forget. There is
		// no file format in sight on purpose -- this project's sounds are synthesised
		// (see sound_synth.h), and a loader can join the party the day real assets do.
		//
		// Spatialisation is deliberately cheap: distance attenuation and stereo pan
		// from the listener's yaw. Ears mostly want "over there, to the left"; HRTFs
		// can wait for a game that needs them.
		class AudioSystem
		{
		public:
			AudioSystem();
			~AudioSystem();

			bool initialize();
			void deinitialize();

			bool isReady() const;

			// Registers a mono 44.1 kHz float PCM buffer; the samples are copied.
			SoundHandle create(const f32* frames, u32 frameCount);

			// Where the ears are, for play3d. Forward only needs to be roughly
			// horizontal -- the pan comes from its right-hand direction.
			void setListener(const math::Vec3& position, const math::Vec3& forward);

			void setMasterVolume(f32 volume);
			f32 masterVolume() const;

			// Fire-and-forget, in the listener's head (UI, own footsteps).
			void play(SoundHandle sound, f32 volume = 1.0f, f32 pitch = 1.0f);

			// Fire-and-forget, somewhere in the world: attenuated and panned.
			void play3d(SoundHandle sound, const math::Vec3& position, f32 volume = 1.0f, f32 pitch = 1.0f);

			// For the debug UI: voices currently sounding, and everything ever started.
			u32 activeVoices() const;
			u32 totalPlays() const;

		private:
			struct Impl;
			std::unique_ptr<Impl> impl_;
		};
	}
}

#endif
