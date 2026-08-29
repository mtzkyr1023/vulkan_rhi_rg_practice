#include "game/sound_synth.h"

#include <algorithm>
#include <cmath>

namespace mv::game::synth
{
	namespace
	{
		constexpr f32 kPi = 3.14159265358979f;

		// A fixed-seed xorshift: the same "recording" every run, which is what an
		// asset is supposed to be.
		struct Rng
		{
			u32 state = 0x9E3779B9u;

			f32 next()
			{
				state ^= state << 13;
				state ^= state >> 17;
				state ^= state << 5;

				return (f32)(state & 0xFFFFFF) / (f32)0xFFFFFF * 2.0f - 1.0f;
			}
		};

		// One-pole lowpass; cutoff as a 0..1 blend, higher is brighter.
		struct Lowpass
		{
			f32 value = 0.0f;

			f32 step(f32 input, f32 cutoff)
			{
				value += (input - value) * cutoff;
				return value;
			}
		};

		std::vector<f32> makeBuffer(f32 seconds)
		{
			return std::vector<f32>((size_t)(seconds * (f32)kSampleRate), 0.0f);
		}

		// Scale so the loudest sample sits at the given peak; synthesis stays honest
		// about shape and this stays in charge of level.
		void normalize(std::vector<f32>& buffer, f32 peak)
		{
			f32 loudest = 0.0f;

			for (f32 sample : buffer)
				loudest = (std::max)(loudest, std::abs(sample));

			if (loudest < 0.0001f)
				return;

			const f32 gain = peak / loudest;

			for (f32& sample : buffer)
				sample *= gain;
		}
	}

	std::vector<f32> impact()
	{
		std::vector<f32> buffer = makeBuffer(0.28f);

		Rng rng;
		Lowpass noiseFilter;

		f32 phase = 0.0f;

		for (size_t i = 0; i < buffer.size(); i++)
		{
			const f32 t = (f32)i / (f32)kSampleRate;

			// The thump: a sine dropping from 170 to 65 Hz, dying fast. The pitch
			// drop is what makes it read as weight rather than a beep.
			const f32 frequency = 65.0f + 105.0f * std::exp(-t * 22.0f);
			phase += 2.0f * kPi * frequency / (f32)kSampleRate;

			const f32 thump = std::sin(phase) * std::exp(-t * 16.0f);

			// The click: a burst of darkened noise over the first few milliseconds,
			// the "surface detail" of the hit.
			const f32 click = noiseFilter.step(rng.next(), 0.35f) * std::exp(-t * 70.0f);

			buffer[i] = thump + click * 0.6f;
		}

		normalize(buffer, 0.8f);

		return buffer;
	}

	std::vector<f32> splash()
	{
		std::vector<f32> buffer = makeBuffer(0.7f);

		Rng rng;
		Lowpass body;
		Lowpass sparkle;

		for (size_t i = 0; i < buffer.size(); i++)
		{
			const f32 t = (f32)i / (f32)kSampleRate;

			// Water reads as noise that darkens: bright at the moment of entry,
			// muffled as the thing sinks. Cutoff sweeps down over the length.
			const f32 cutoff = 0.05f + 0.5f * std::exp(-t * 7.0f);

			// A short attack rather than a click: water yields before it roars.
			const f32 envelope = (std::min)(t / 0.02f, 1.0f) * std::exp(-t * 6.0f);

			const f32 noise = rng.next();

			buffer[i] =
				body.step(noise, cutoff) * envelope +
				sparkle.step(noise, 0.8f) * envelope * 0.25f * std::exp(-t * 18.0f);
		}

		normalize(buffer, 0.7f);

		return buffer;
	}

	std::vector<f32> footstep()
	{
		std::vector<f32> buffer = makeBuffer(0.1f);

		Rng rng;
		Lowpass filter;

		for (size_t i = 0; i < buffer.size(); i++)
		{
			const f32 t = (f32)i / (f32)kSampleRate;

			// A dull tap: heavily darkened noise, over almost before it starts.
			buffer[i] = filter.step(rng.next(), 0.12f) *
				(std::min)(t / 0.004f, 1.0f) * std::exp(-t * 55.0f);
		}

		normalize(buffer, 0.5f);

		return buffer;
	}

	std::vector<f32> jump()
	{
		std::vector<f32> buffer = makeBuffer(0.16f);

		Rng rng;
		Lowpass filter;

		for (size_t i = 0; i < buffer.size(); i++)
		{
			const f32 t = (f32)i / (f32)kSampleRate;

			// A rising swish: noise brightening as it fades -- effort leaving the
			// ground, the reverse of a landing.
			const f32 cutoff = 0.08f + 0.45f * (t / 0.16f);

			buffer[i] = filter.step(rng.next(), cutoff) *
				(std::min)(t / 0.01f, 1.0f) * std::exp(-t * 20.0f);
		}

		normalize(buffer, 0.35f);

		return buffer;
	}

	std::vector<f32> land()
	{
		std::vector<f32> buffer = makeBuffer(0.2f);

		Rng rng;
		Lowpass noiseFilter;

		f32 phase = 0.0f;

		for (size_t i = 0; i < buffer.size(); i++)
		{
			const f32 t = (f32)i / (f32)kSampleRate;

			// The impact's softer cousin: lower, duller, no hard click -- boots on
			// dirt rather than a crate on rock.
			const f32 frequency = 55.0f + 60.0f * std::exp(-t * 25.0f);
			phase += 2.0f * kPi * frequency / (f32)kSampleRate;

			buffer[i] =
				std::sin(phase) * std::exp(-t * 20.0f) +
				noiseFilter.step(rng.next(), 0.15f) * std::exp(-t * 40.0f) * 0.5f;
		}

		normalize(buffer, 0.55f);

		return buffer;
	}
}
