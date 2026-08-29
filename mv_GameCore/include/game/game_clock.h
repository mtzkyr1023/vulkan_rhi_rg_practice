#ifndef _MV_GAME_CLOCK_H_
#define _MV_GAME_CLOCK_H_

#include "util/types.h"

namespace mv
{
	namespace game
	{
		using namespace types;

		// The fixed-timestep clock: rendering runs as fast as it likes, simulation runs at
		// one unchanging rate.
		//
		// Everything a game simulates -- movement, gravity, collision -- behaves
		// differently at different step sizes, and a frame rate is exactly a step size
		// that changes whenever a cloud drifts on screen. The accumulator turns however
		// much real time passed into zero or more steps of the same length, and the
		// leftover fraction is the interpolation alpha the renderer draws with, so motion
		// stays smooth between steps it never simulated.
		class GameClock
		{
		public:
			// Feed it the frame's real elapsed time, then drain steps:
			//
			//   clock.tick(deltaSeconds);
			//   while (clock.step()) simulate(clock.fixedDelta());
			//   render(clock.alpha());
			void tick(f32 deltaSeconds)
			{
				// A debugger pause or a long load otherwise arrives as one enormous delta,
				// and the loop below dutifully simulates the whole stall in one frame.
				if (deltaSeconds > 0.25f)
					deltaSeconds = 0.25f;

				accumulator_ += deltaSeconds;
				totalTime_ += deltaSeconds;
			}

			bool step()
			{
				if (accumulator_ < kFixedDelta)
					return false;

				accumulator_ -= kFixedDelta;
				return true;
			}

			f32 fixedDelta() const { return kFixedDelta; }

			// How far between the last simulated step and the next the renderer is asked
			// to draw, in [0, 1). Lerping previous and current state by this is what keeps
			// a 60 Hz simulation from stuttering at 144 Hz -- or at 45.
			f32 alpha() const { return accumulator_ / kFixedDelta; }

			f32 totalTime() const { return totalTime_; }

		private:
			static constexpr f32 kFixedDelta = 1.0f / 60.0f;

			f32 accumulator_ = 0.0f;
			f32 totalTime_ = 0.0f;
		};
	}
}

#endif
