#include "anim/animator.h"

#include <algorithm>
#include <cmath>

namespace mv::anim
{
	void Animator::bind(const asset::SkinnedModel* model)
	{
		model_ = model;
		clip_ = 0;
		time_ = 0.0f;

		const size_t joints = model_ ? model_->joints.size() : 0;

		translations_.resize(joints);
		rotations_.resize(joints);
		scales_.resize(joints);
		globals_.resize(joints);
		palette_.assign(joints, math::Mat4::identity());

		update(0.0f);
	}

	void Animator::play(u32 clipIndex, bool loop)
	{
		if (model_ == nullptr || clipIndex >= model_->clips.size())
			return;

		clip_ = clipIndex;
		loop_ = loop;
		time_ = 0.0f;
	}

	void Animator::update(f32 deltaSeconds)
	{
		if (!isReady())
			return;

		// The rest pose first: a channel only overwrites what it animates, and a
		// joint no channel mentions must still stand where the file put it.
		for (size_t j = 0; j < model_->joints.size(); j++)
		{
			translations_[j] = model_->joints[j].baseTranslation;
			rotations_[j] = model_->joints[j].baseRotation;
			scales_[j] = model_->joints[j].baseScale;
		}

		if (clip_ < model_->clips.size())
		{
			const asset::AnimClip& clip = model_->clips[clip_];

			time_ += deltaSeconds;

			if (clip.duration > 0.0f)
			{
				if (loop_)
					time_ = std::fmod(time_, clip.duration);
				else
					time_ = (std::min)(time_, clip.duration);
			}

			for (const asset::AnimChannel& channel : clip.channels)
			{
				if (channel.times.empty())
					continue;

				// The keyframe pair around the current time, clamped at both ends.
				const auto after = std::upper_bound(channel.times.begin(), channel.times.end(), time_);

				const size_t next = (size_t)(after - channel.times.begin());
				const size_t base = next > 0 ? next - 1 : 0;
				const size_t ahead = (std::min)(next, channel.times.size() - 1);

				f32 blend = 0.0f;

				if (ahead > base)
				{
					const f32 span = channel.times[ahead] - channel.times[base];
					blend = span > 0.0f ? (time_ - channel.times[base]) / span : 0.0f;
				}

				const u32 stride = channel.path == asset::EAnimPath::eRotation ? 4 : 3;
				const f32* a = &channel.values[base * stride];
				const f32* b = &channel.values[ahead * stride];

				switch (channel.path)
				{
				case asset::EAnimPath::eTranslation:
					translations_[channel.joint] = {
						a[0] + (b[0] - a[0]) * blend,
						a[1] + (b[1] - a[1]) * blend,
						a[2] + (b[2] - a[2]) * blend };
					break;

				case asset::EAnimPath::eRotation:
					rotations_[channel.joint] = math::nlerp(
						{ a[0], a[1], a[2], a[3] },
						{ b[0], b[1], b[2], b[3] },
						blend);
					break;

				case asset::EAnimPath::eScale:
					scales_[channel.joint] = {
						a[0] + (b[0] - a[0]) * blend,
						a[1] + (b[1] - a[1]) * blend,
						a[2] + (b[2] - a[2]) * blend };
					break;
				}
			}
		}

		// Locals to globals, parents first -- which is exactly what jointOrder holds.
		// Row-vector: the local applies before the parent.
		for (const u32 j : model_->jointOrder)
		{
			const math::Mat4 local = math::composeTRS(translations_[j], rotations_[j], scales_[j]);
			const s32 parent = model_->joints[j].parent;

			globals_[j] = parent >= 0 ? local * globals_[parent] : local;
		}

		// What the vertex shader multiplies by: out of bind pose, into the animated
		// one, in a single matrix per joint.
		for (size_t j = 0; j < model_->joints.size(); j++)
		{
			palette_[j] = model_->joints[j].inverseBind * globals_[j];
		}
	}
}
