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
	virtual void Apply(View *view) = 0;

	bool Finished() {
		return time_now() >= start_ + duration_;
	}

protected:
	float DurationOffset() {
		return time_now() - start_;
	}

	float Position() {
		return curve_(std::min(1.0f, DurationOffset() / duration_));
	}

	float start_;
	float duration_;
	float (*curve_)(float);
};

// This is the class all tweens inherit from.  Shouldn't be used directly, see below.
template <typename Value>
class TweenBase: public Tween {
public:
	TweenBase(Value from, Value to, float duration, float (*curve)(float) = [](float f) { return f; })
		: Tween(duration, curve), from_(from), to_(to) {
	}

	// Use this to change the destination value.
	// Useful when a state flips while the tween is half-way through.
	void Divert(const Value &newTo) {
		const Value newFrom = Current();

		// Are we already part way through another transition?
		if (!Finished()) {
			if (newTo == to_) {
				// Already on course.  Don't change.
			} else if (newTo == from_) {
				// Reversing, adjust start_ to be smooth from the current value.
				float newOffset = duration_ - DurationOffset();
				start_ = time_now() - newOffset;
			} else {
				// Otherwise, start over.
				start_ = time_now();
			}
		} else {
			// Already finished, so restart.
			start_ = time_now();
		}

		from_ = newFrom;
		to_ = newTo;
	}

	// Stop animating the value.
	void Stop() {
		Reset(Current());
	}

	// Use when the value is explicitly reset.  Implicitly stops the tween.
	void Reset(const Value &newFrom) {
		from_ = newFrom;
		to_ = newFrom;
	}

protected:
	virtual Value Current() = 0;

	Value from_;
	Value to_;
};

// Generic - subclass this for specific color handling.
class ColorTween : public TweenBase<uint32_t> {
public:
	using TweenBase::TweenBase;

protected:
	uint32_t Current() override;
};

class TextColorTween : public ColorTween {
public:
	using ColorTween::ColorTween;

	void Apply(View *view) override;
};

class VisibilityTween : public TweenBase<Visibility> {
public:
	using TweenBase::TweenBase;

	void Apply(View *view) override;

protected:
	Visibility Current() override;
};

}  // namespace
