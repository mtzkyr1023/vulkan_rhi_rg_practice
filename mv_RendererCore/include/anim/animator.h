#ifndef _MV_ANIMATOR_H_
#define _MV_ANIMATOR_H_

#include <vector>

#include "asset/gltf_loader.h"

#include "util/math.h"
#include "util/types.h"

namespace mv
{
	namespace anim
	{
		using namespace types;

		// Plays one clip of one skinned model and keeps the joint palette current.
		//
		// Pure CPU and pure math: sample the channels at the clip time, compose each
		// joint's TRS on top of the rest pose, walk the skeleton parents-first for
		// globals, and fold in the inverse bind. The palette is what a vertex wants
		// to multiply by -- nothing else leaves this class.
		class Animator
		{
		public:
			// Points the animator at a model and starts its first clip (if any).
			void bind(const asset::SkinnedModel* model);

			bool isReady() const { return model_ != nullptr && !model_->joints.empty(); }

			void play(u32 clipIndex, bool loop = true);

			u32 clipCount() const { return model_ ? (u32)model_->clips.size() : 0; }

			const char* clipName(u32 index) const
			{
				return model_ && index < model_->clips.size() ? model_->clips[index].name.c_str() : "";
			}

			u32 currentClip() const { return clip_; }

			// Advances the clip time and rebuilds the palette.
			void update(f32 deltaSeconds);

			// inverseBind * global, one per joint, in joint order 0..N-1.
			const std::vector<math::Mat4>& palette() const { return palette_; }

		private:
			const asset::SkinnedModel* model_ = nullptr;

			u32 clip_ = 0;
			bool loop_ = true;
			f32 time_ = 0.0f;

			// Scratch, sized to the skeleton at bind: the sampled local pose and the
			// globals it becomes.
			std::vector<math::Vec3> translations_;
			std::vector<math::Quat> rotations_;
			std::vector<math::Vec3> scales_;
			std::vector<math::Mat4> globals_;

			std::vector<math::Mat4> palette_;
		};
	}
}

#endif
