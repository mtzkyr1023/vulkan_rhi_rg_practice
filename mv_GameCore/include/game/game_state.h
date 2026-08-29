#ifndef _MV_GAME_GAME_STATE_H_
#define _MV_GAME_GAME_STATE_H_

#include "util/types.h"

namespace mv
{
	namespace game
	{
		using namespace types;

		enum class EGameState : u32
		{
			eTitle,
			ePlaying,
			ePaused,
		};

		// The game's top-level mode, with the transitions written down.
		//
		// A bool called playMode was fine while there were two modes; the third one is
		// where every flag-based scheme starts sprouting "if (paused && !playing)"
		// weeds. Here the states are named, the legal moves are a table, and an
		// illegal request is refused rather than half-applied -- transition() says no
		// and the caller finds out, instead of the game finding out later.
		//
		// Deliberately not a framework: no state objects, no virtual on-enter methods.
		// The caller owns what entering a state means (the engine teleports the player,
		// freezes the clock, and so on); this class owns only which moves exist.
		class GameStateMachine
		{
		public:
			EGameState state() const { return state_; }

			bool is(EGameState state) const { return state_ == state; }

			// Seconds spent in the current state -- for HUD pulses and fades.
			f32 timeInState() const { return time_; }

			void update(f32 deltaSeconds) { time_ += deltaSeconds; }

			// True if the move is legal and was taken; the clock restarts on entry.
			bool transition(EGameState next)
			{
				if (!allowed(state_, next))
					return false;

				state_ = next;
				time_ = 0.0f;

				return true;
			}

		private:
			static bool allowed(EGameState from, EGameState to)
			{
				if (from == to)
					return false;

				switch (from)
				{
				case EGameState::eTitle:   return to == EGameState::ePlaying;
				case EGameState::ePlaying: return to == EGameState::ePaused || to == EGameState::eTitle;
				case EGameState::ePaused:  return to == EGameState::ePlaying || to == EGameState::eTitle;
				}

				return false;
			}

			EGameState state_ = EGameState::eTitle;
			f32 time_ = 0.0f;
		};
	}
}

#endif
