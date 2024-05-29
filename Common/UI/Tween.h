#pragma once

#include <algorithm>
#include <cstdint>
#include <functional>
#include "Common/TimeUtil.h"
#include "Common/UI/View.h"

namespace UI {

// This is the class to use in Update().
class Tween {
public:
	explicit Tween(float duration, float (*curve)(float)) : duration_(duration), curve_(curve) {
		start_ = time_now_d();
	}
	virtual ~Tween() {
	}

	// Actually apply the tween to a view.
	void Apply(View *view);

	bool Finished() {
		return finishApplied_ && time_now_d() >= start_ + delay_ + duration_;
	}

	void Persist() {
		persists_ = true;
	}
	bool Persists() {
		return persists_;
	}

	void Delay(float s) {
		delay_ = s;
	}

	virtual void PersistData(PersistStatus status, std::string anonId, PersistMap &storage) = 0;

	Event Finish;

protected:
	float DurationOffset() {
		return (time_now_d() - start_) - delay_;
	}

	float Position() {
		return curve_(std::min(1.0f, DurationOffset() / duration_));
	}

	virtual void DoApply(View *view, float pos) = 0;

	double start_;
	float duration_;
	float delay_ = 0.0f;
	bool finishApplied_ = false;
	bool persists_ = false;
	bool valid_ = false;
	float (*curve_)(float);
};

// This is the class all tweens inherit from.  Shouldn't be used directly, see below.
// Note: Value cannot safely be a pointer (without overriding PersistData.)
template <typename Value>
class TweenBase: public Tween {
public:
	TweenBase(float duration, float (*curve)(float) = [](float f) { return f; })
		: Tween(duration, curve) {
	}
	TweenBase(Value from, Value to, float duration, float (*curve)(float) = [](float f) { return f; })
		: Tween(duration, curve), from_(from), to_(to) {
		valid_ = true;
	}

	// Use this to change the destination value.
	// Useful when a state flips while the tween is half-way through.
	void Divert(const Value &newTo, float newDuration = -1.0f) {
		const Value newFrom = valid_ ? Current(Position()) : newTo;

		// Are we already part way through another transition?
		if (time_now_d() < start_ + delay_ + duration_ && valid_) {
			if (newTo == to_) {
				// Already on course.  Don't change.
				return;
			} else if (newTo == from_ && duration_ > 0.0f) {
				// Reversing, adjust start_ to be smooth from the current value.
				float newOffset = duration_ - std::max(0.0f, DurationOffset());
				if (newDuration >= 0.0f) {
					newOffset *= newDuration / duration_;
				}
				start_ = time_now_d() - newOffset - delay_;
			} else if (time_now_d() <= start_ + delay_) {
				// Start the delay over again.
				start_ = time_now_d();
			} else {
				// Since we've partially animated to the other value, skip delay.
				start_ = time_now_d() - delay_;
			}
		} else {
			// Already finished, so restart.
			start_ = time_now_d();
			finishApplied_ = false;
		}

		from_ = newFrom;
		to_ = newTo;
		valid_ = true;
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
		valid_ = true;
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

class CallbackColorTween : public ColorTween {
public:
	using ColorTween::ColorTween;

	void SetCallback(const std::function<void(View *v, uint32_t c)> &cb) {
		callback_ = cb;
	}

protected:
	void DoApply(View *view, float pos) override;

	std::function<void(View *v, uint32_t c)> callback_;
};

class VisibilityTween : public TweenBase<Visibility> {
public:
	using TweenBase::TweenBase;

protected:
	void DoApply(View *view, float pos) override;

	Visibility Current(float pos) override;
};

class AnchorTranslateTween : public TweenBase<Point2D> {
public:
	using TweenBase::TweenBase;

protected:
	void DoApply(View *view, float pos) override;

	Point2D Current(float pos) override;
};

}  // namespace
