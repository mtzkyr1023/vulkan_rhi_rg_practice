#include "game/audio.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include <Windows.h>
#include <xaudio2.h>

#pragma comment(lib, "xaudio2.lib")
#pragma comment(lib, "ole32.lib")

namespace mv::game
{
	namespace
	{
		constexpr u32 kSampleRate = 44100;
		constexpr u32 kMaxVoices = 24;

		// Distance at which a sound has fallen to roughly half, and where it stops
		// mattering at all. Tuned to this world's scale, where the props land tens of
		// metres away.
		constexpr f32 kHalfDistance = 14.0f;
		constexpr f32 kMaxDistance = 160.0f;
	}

	// Everything XAudio2 lives here, where the header promised it would.
	struct AudioSystem::Impl
	{
		IXAudio2* xaudio = nullptr;
		IXAudio2MasteringVoice* master = nullptr;

		// Whether this Impl's CoInitializeEx succeeded and owes a CoUninitialize.
		bool comInitialized = false;

		// Registered sample buffers. Voices reference this memory while playing, so
		// a sound lives until deinitialize -- there is no unregister on purpose.
		std::vector<std::vector<f32>> sounds;

		// The voice pool. All sounds share one format, so any voice can play any
		// sound; a voice is free again once its buffer queue has drained.
		struct Voice
		{
			IXAudio2SourceVoice* voice = nullptr;
		};

		std::vector<Voice> voices;

		math::Vec3 listenerPosition{ 0.0f, 0.0f, 0.0f };
		math::Vec3 listenerRight{ 1.0f, 0.0f, 0.0f };

		f32 masterVolume = 0.8f;
		u32 totalPlays = 0;

		IXAudio2SourceVoice* acquireVoice()
		{
			for (Voice& v : voices)
			{
				XAUDIO2_VOICE_STATE state{};
				v.voice->GetState(&state, XAUDIO2_VOICE_NOSAMPLESPLAYED);

				if (state.BuffersQueued == 0)
					return v.voice;
			}

			if (voices.size() >= kMaxVoices)
				return nullptr;

			WAVEFORMATEX format{};
			format.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
			format.nChannels = 1;
			format.nSamplesPerSec = kSampleRate;
			format.wBitsPerSample = 32;
			format.nBlockAlign = 4;
			format.nAvgBytesPerSec = kSampleRate * 4;

			IXAudio2SourceVoice* voice = nullptr;

			if (FAILED(xaudio->CreateSourceVoice(&voice, &format)))
				return nullptr;

			voice->Start(0);
			voices.push_back({ voice });

			return voice;
		}

		void start(SoundHandle sound, f32 volume, f32 pitch, f32 pan)
		{
			if (sound >= sounds.size() || sounds[sound].empty())
				return;

			IXAudio2SourceVoice* voice = acquireVoice();

			if (voice == nullptr)
				return;

			voice->SetVolume((std::max)(volume, 0.0f) * masterVolume);
			voice->SetFrequencyRatio((std::min)((std::max)(pitch, 0.5f), 2.0f));

			// Constant-power pan into the stereo master; a mono master just sums it.
			const f32 clamped = (std::min)((std::max)(pan, -1.0f), 1.0f);
			const f32 matrix[2] = {
				std::sqrt(0.5f * (1.0f - clamped)),
				std::sqrt(0.5f * (1.0f + clamped)) };

			XAUDIO2_VOICE_DETAILS details{};
			master->GetVoiceDetails(&details);

			if (details.InputChannels == 2)
				voice->SetOutputMatrix(nullptr, 1, 2, matrix);

			XAUDIO2_BUFFER buffer{};
			buffer.Flags = XAUDIO2_END_OF_STREAM;
			buffer.AudioBytes = (UINT32)(sounds[sound].size() * sizeof(f32));
			buffer.pAudioData = (const BYTE*)sounds[sound].data();

			if (SUCCEEDED(voice->SubmitSourceBuffer(&buffer)))
				totalPlays++;
		}
	};

	AudioSystem::AudioSystem() = default;
	AudioSystem::~AudioSystem() = default;

	bool AudioSystem::initialize()
	{
		impl_ = std::make_unique<Impl>();

		// S_FALSE means COM was already up on this thread, which is fine and still
		// owes a matching CoUninitialize; a changed-mode failure means someone else
		// owns COM here and this system leaves it alone.
		const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
		impl_->comInitialized = SUCCEEDED(com);

		if (FAILED(XAudio2Create(&impl_->xaudio, 0, XAUDIO2_DEFAULT_PROCESSOR)))
		{
			deinitialize();
			return false;
		}

		if (FAILED(impl_->xaudio->CreateMasteringVoice(&impl_->master)))
		{
			deinitialize();
			return false;
		}

		return true;
	}

	void AudioSystem::deinitialize()
	{
		if (!impl_)
			return;

		for (Impl::Voice& v : impl_->voices)
		{
			if (v.voice)
				v.voice->DestroyVoice();
		}

		impl_->voices.clear();

		if (impl_->master)
			impl_->master->DestroyVoice();

		if (impl_->xaudio)
			impl_->xaudio->Release();

		if (impl_->comInitialized)
			CoUninitialize();

		impl_.reset();
	}

	bool AudioSystem::isReady() const
	{
		return impl_ && impl_->master != nullptr;
	}

	SoundHandle AudioSystem::create(const f32* frames, u32 frameCount)
	{
		if (!isReady() || frames == nullptr || frameCount == 0)
			return kInvalidSound;

		impl_->sounds.emplace_back(frames, frames + frameCount);

		return (SoundHandle)(impl_->sounds.size() - 1);
	}

	void AudioSystem::setListener(const math::Vec3& position, const math::Vec3& forward)
	{
		if (!impl_)
			return;

		impl_->listenerPosition = position;

		// The right-hand direction on the horizontal plane, which is all the pan uses.
		const f32 length = std::sqrt(forward.x * forward.x + forward.z * forward.z);

		if (length > 0.001f)
			impl_->listenerRight = { -forward.z / length, 0.0f, forward.x / length };
	}

	void AudioSystem::setMasterVolume(f32 volume)
	{
		if (impl_)
			impl_->masterVolume = (std::min)((std::max)(volume, 0.0f), 1.0f);
	}

	f32 AudioSystem::masterVolume() const
	{
		return impl_ ? impl_->masterVolume : 0.0f;
	}

	void AudioSystem::play(SoundHandle sound, f32 volume, f32 pitch)
	{
		if (isReady())
			impl_->start(sound, volume, pitch, 0.0f);
	}

	void AudioSystem::play3d(SoundHandle sound, const math::Vec3& position, f32 volume, f32 pitch)
	{
		if (!isReady())
			return;

		const math::Vec3 to = position - impl_->listenerPosition;
		const f32 distance = std::sqrt(math::dot(to, to));

		if (distance > kMaxDistance)
			return;

		// Inverse-distance attenuation with a knee at kHalfDistance, faded fully out
		// by kMaxDistance so distant sounds end rather than whisper forever.
		const f32 attenuation =
			(kHalfDistance / (kHalfDistance + distance)) *
			(1.0f - distance / kMaxDistance);

		f32 pan = 0.0f;

		if (distance > 1.0f)
			pan = 0.7f * (to.x * impl_->listenerRight.x + to.z * impl_->listenerRight.z) / distance;

		impl_->start(sound, volume * attenuation, pitch, pan);
	}

	u32 AudioSystem::activeVoices() const
	{
		if (!isReady())
			return 0;

		u32 active = 0;

		for (Impl::Voice& v : impl_->voices)
		{
			XAUDIO2_VOICE_STATE state{};
			v.voice->GetState(&state, XAUDIO2_VOICE_NOSAMPLESPLAYED);

			if (state.BuffersQueued > 0)
				active++;
		}

		return active;
	}

	u32 AudioSystem::totalPlays() const
	{
		return impl_ ? impl_->totalPlays : 0;
	}
}
