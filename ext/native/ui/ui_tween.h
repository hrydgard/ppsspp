#pragma once

#include <algorithm>
#include <cstdint>
#include "base/timeutil.h"
#include "ui/view.h"

namespace UI {

// This is the class to use in Update().
class Tween {
public:
	Tween(float duration, float (*curve)(float)) : duration_(duration), curve_(curve) {
		start_ = time_now();
	}
	virtual ~Tween() {
	}

	// Actually apply the tween to a view.
	void Apply(View *view);

	bool Finished() {
		return finishApplied_ && time_now() >= start_ + duration_;
	}

	virtual void PersistData(PersistStatus status, std::string anonId, PersistMap &storage) = 0;

protected:
	float DurationOffset() {
		return time_now() - start_;
	}

	float Position() {
		return curve_(std::min(1.0f, DurationOffset() / duration_));
	}

	virtual void DoApply(View *view, float pos) = 0;

	float start_;
	float duration_;
	bool finishApplied_ = false;
	float (*curve_)(float);
};

// This is the class all tweens inherit from.  Shouldn't be used directly, see below.
// Note: Value cannot safely be a pointer (without overriding PersistData.)
template <typename Value>
class TweenBase: public Tween {
public:
	TweenBase(Value from, Value to, float duration, float (*curve)(float) = [](float f) { return f; })
		: Tween(duration, curve), from_(from), to_(to) {
	}

	// Use this to change the destination value.
	// Useful when a state flips while the tween is half-way through.
	void Divert(const Value &newTo, float newDuration = -1.0f) {
		const Value newFrom = Current(Position());

		// Are we already part way through another transition?
		if (time_now() < start_ + duration_) {
			if (newTo == to_) {
				// Already on course.  Don't change.
			} else if (newTo == from_) {
				// Reversing, adjust start_ to be smooth from the current value.
				float newOffset = duration_ - DurationOffset();
				if (newDuration >= 0.0f && duration_ > 0.0f) {
					newOffset *= newDuration / duration_;
				}
				start_ = time_now() - newOffset;
			} else {
				// Otherwise, start over.
				start_ = time_now();
			}
		} else {
			// Already finished, so restart.
			start_ = time_now();
			finishApplied_ = false;
		}

		from_ = newFrom;
		to_ = newTo;
		if (newDuration >= 0.0f) {
			duration_ = newDuration;
		}
	}

	// Stop animating the value.
	void Stop() {
		Reset(Current(Position()));
	}

	// Use when the value is explicitly reset.  Implicitly stops the tween.
	void Reset(const Value &newFrom) {
		from_ = newFrom;
		to_ = newFrom;
	}

	const Value &FromValue() const {
		return from_;
	}
	const Value &ToValue() const {
		return to_;
	}
	Value CurrentValue() {
		return Current(Position());
	}

	void PersistData(PersistStatus status, std::string anonId, PersistMap &storage) override;

protected:
	virtual Value Current(float pos) = 0;

	Value from_;
	Value to_;
};

// Generic - subclass this for specific color handling.
class ColorTween : public TweenBase<uint32_t> {
public:
	using TweenBase::TweenBase;

protected:
	uint32_t Current(float pos) override;
};

class TextColorTween : public ColorTween {
public:
	using ColorTween::ColorTween;

protected:
	void DoApply(View *view, float pos) override;
};

class VisibilityTween : public TweenBase<Visibility> {
public:
	using TweenBase::TweenBase;

protected:
	void DoApply(View *view, float pos) override;

	Visibility Current(float pos) override;
};

}  // namespace
