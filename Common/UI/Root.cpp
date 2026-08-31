#include <algorithm>
#include <deque>

#include "ppsspp_config.h"

#include "Common/Input/InputState.h"
#include "Common/Log.h"
#include "Common/TimeUtil.h"
#include "Common/UI/Root.h"
#include "Common/UI/ViewGroup.h"

namespace UI {

static std::vector<FocusMove> focusMoves;
extern bool focusForced;

static View *focusedView;
static bool focusMovementEnabled;
bool focusForced;

static std::function<void(UISound)> soundCallback;

// Repeat key handling below.
// TODO: Figure out where this should really live.
// Simple simulation of key repeat on platforms and for gamepads where we don't
// automatically get it.

static int frameCount;

// Ignore deviceId when checking for matches. Turns out that Ouya for example sends
// completely broken input where the original keypresses have deviceId = 10 and the repeats
// have deviceId = 0.
struct HeldKey {
	InputKeyCode key;
	InputDeviceID deviceId;
	double triggerTime;
	// What the key meant when it went down. Kept here because a repeat has to move the same way
	// the original press did, and modifiers (Shift+Tab) aren't part of the synthesized repeat.
	FocusMove move;

	// Ignores startTime
	bool operator <(const HeldKey &other) const {
		if (key < other.key) return true;
		return false;
	}
	bool operator ==(const HeldKey &other) const { return key == other.key; }
};

static std::set<HeldKey> heldKeys;

const double repeatDelay = 15 * (1.0 / 60.0f);  // 15 frames like before.
const double repeatInterval = 5 * (1.0 / 60.0f);  // 5 frames like before.

struct DispatchQueueItem {
	Event *e;
	EventParams params;
};

std::deque<DispatchQueueItem> g_dispatchQueue;

void EventTriggered(Event *e, EventParams params) {
	DispatchQueueItem item{ e, params };
	g_dispatchQueue.push_front(item);
}

DialogResult DispatchEvents() {
	DialogResult result = DR_NONE;
	while (!g_dispatchQueue.empty()) {
		DispatchQueueItem item;
		if (g_dispatchQueue.empty())
			break;
		item = g_dispatchQueue.back();
		g_dispatchQueue.pop_back();
		if (item.e) {
			item.e->Dispatch(item.params);
			if (item.params.bubbleResult != DR_NONE) {
				result = item.params.bubbleResult;
			}
		}
	}
	return result;
}

void RemoveQueuedEventsByView(View *view) {
	for (auto it = g_dispatchQueue.begin(); it != g_dispatchQueue.end(); ) {
		if (it->params.v == view) {
			it = g_dispatchQueue.erase(it);
		} else {
			++it;
		}
	}
}

void RemoveQueuedEventsByEvent(Event *event) {
	for (auto it = g_dispatchQueue.begin(); it != g_dispatchQueue.end(); ) {
		if (it->e == event) {
			it = g_dispatchQueue.erase(it);
		} else {
			++it;
		}
	}
}

View *GetFocusedView() {
	return focusedView;
}

void SetFocusedView(View *view, FocusFlags cause, bool force) {
	if (focusedView) {
		focusedView->FocusChanged(FocusFlags::LOST_FOCUS | cause);
	}
	focusedView = view;
	if (focusedView) {
		focusedView->FocusChanged(FocusFlags::GOT_FOCUS | cause);
		if (force) {
			focusForced = true;
		}
	}
}

void EnableFocusMovement(bool enable) {
	// INFO_LOG(Log::UI, "EnableFocusMovement: %s", enable ? "true" : "false");
	focusMovementEnabled = enable;
	if (!enable) {
		if (focusedView) {
			focusedView->FocusChanged(FocusFlags::LOST_FOCUS | FocusFlags::CAUSE_KB_FOCUS_DISABLED);
		}
		focusMoves.clear();
		heldKeys.clear();
		focusedView = nullptr;
	} else {
		enable = enable;
	}
}

bool IsFocusMovementEnabled() {
	return focusMovementEnabled;
}

void LayoutViewHierarchy(const UIContext &dc, const UI::Margins &rootMargins, ViewGroup *root, ViewLayoutMode layoutMode, bool immersiveMode) {
	Bounds rootBounds = dc.GetLayoutBounds(layoutMode, immersiveMode);

	MeasureSpec horiz(EXACTLY, rootBounds.w - (rootMargins.left + rootMargins.right));
	MeasureSpec vert(EXACTLY, rootBounds.h - (rootMargins.top + rootMargins.bottom));

	// Two phases - measure contents, layout.
	root->Measure(dc, horiz, vert);
	// Root has a specified size. Set it, then let root layout all its children.
	rootBounds.x += rootMargins.left;
	rootBounds.y += rootMargins.top;
	rootBounds.w -= rootMargins.left + rootMargins.right;
	rootBounds.h -= rootMargins.top + rootMargins.bottom;
	root->SetBounds(rootBounds);
	root->Layout();
}

// Tab order navigation. Unlike the directional moves, this doesn't look at where things ended up
// on screen at all - it just walks the hierarchy in order, which is why it behaves predictably in
// layouts where "what's to the right of this" has no good answer.
View *FindTabOrderNeighbor(ViewGroup *root, View *focusedView, FocusMove direction) {
	std::vector<View *> order;
	root->CollectTabOrder(&order);
	if (order.empty()) {
		return nullptr;
	}

	const int step = direction == FocusMove::NEXT ? 1 : (int)order.size() - 1;

	// The focused view not being in the list is normal - it can have been hidden or disabled
	// since it got focus. Start from one end rather than giving up.
	auto iter = std::find(order.begin(), order.end(), focusedView);
	if (iter == order.end()) {
		return direction == FocusMove::NEXT ? order.front() : order.back();
	}

	const size_t index = iter - order.begin();
	return order[(index + step) % order.size()];
}

static void MoveFocus(ViewGroup *root, FocusMove direction) {
	View *focusedView = GetFocusedView();
	if (!focusedView) {
		// Nothing was focused when we got in here. Focus the first non-group in the hierarchy.
		root->SetFocus(FocusFlags::CAUSE_FOCUS_MOVE);
		return;
	}

	if (!focusedView->CanMoveFocus(direction)) {
		return;
	}

	View *dest;
	if (direction == FocusMove::NEXT || direction == FocusMove::PREV) {
		dest = FindTabOrderNeighbor(root, focusedView, direction);
		if (dest == focusedView) {
			// Tabbing around a single stop. Don't re-fire focus events or play a sound for it.
			// The directional moves below deliberately can land on the origin - FindScrollNeighbor
			// considers it a candidate - so this only applies here.
			dest = nullptr;
		}
	} else {
		dest = root->FindNeighbor(focusedView, direction, NeighborResult()).view;
	}

	if (dest) {
		dest->SetFocus(FocusFlags::CAUSE_FOCUS_MOVE);
		root->SubviewFocused(dest);

		// INFO_LOG(Log::UI, "Focus moved from %s to %s", focusedView->DescribeText().c_str(), dest->DescribeText().c_str());

		PlayUISound(UISound::SELECT);
	}
}

void SetSoundCallback(std::function<void(UISound)> func) {
	soundCallback = func;
}

void PlayUISound(UISound sound) {
	if (soundCallback) {
		soundCallback(sound);
	}
}

// Which way, if any, this key moves the focus. DPad keys are remappable, so they're matched
// through IsDPadKey rather than by keycode here.
static bool KeyToFocusMove(const KeyInput &key, FocusMove *move) {
	if (IsDPadKey(key)) {
		switch (key.keyCode) {
		case NKCODE_DPAD_LEFT: *move = FocusMove::LEFT; return true;
		case NKCODE_DPAD_RIGHT: *move = FocusMove::RIGHT; return true;
		case NKCODE_DPAD_UP: *move = FocusMove::UP; return true;
		case NKCODE_DPAD_DOWN: *move = FocusMove::DOWN; return true;
		default: break;
		}
	}
	switch (key.keyCode) {
	case NKCODE_PAGE_UP: *move = FocusMove::PREV_PAGE; return true;
	case NKCODE_PAGE_DOWN: *move = FocusMove::NEXT_PAGE; return true;
	case NKCODE_MOVE_HOME: *move = FocusMove::FIRST; return true;
	case NKCODE_MOVE_END: *move = FocusMove::LAST; return true;
	case NKCODE_TAB:
		// Ctrl+Tab switches tabs rather than moving focus - see ChoiceStrip::Key.
		if (key.flags & KeyInputFlags::ModCtrl) {
			return false;
		}
		*move = (key.flags & KeyInputFlags::ModShift) ? FocusMove::PREV : FocusMove::NEXT;
		return true;
	default:
		return false;
	}
}

KeyEventResult KeyEventToFocusMoves(const KeyInput &key) {
	KeyEventResult retval = KeyEventResult::PASS_THROUGH;
	// Ignore repeats for focus moves.
	if ((key.flags & KeyInputFlags::DOWN) && !(key.flags & KeyInputFlags::IS_REPEAT)) {
		FocusMove move;
		if (KeyToFocusMove(key, &move)) {
			// Let's only repeat DPAD initially.
			HeldKey hk;
			hk.key = key.keyCode;
			hk.deviceId = key.deviceId;
			hk.triggerTime = time_now_d() + repeatDelay;
			hk.move = move;

			// Check if the key is already held. If it is, ignore it. This is to avoid
			// multiple key repeat mechanisms colliding.
			if (heldKeys.find(hk) != heldKeys.end()) {
				return KeyEventResult::IGNORE_KEY;
			}

			heldKeys.insert(hk);
			focusMoves.push_back(move);
			retval = KeyEventResult::ACCEPT;
		}
	}
	if (key.flags & KeyInputFlags::UP) {
		// We ignore the device ID here (in the comparator for HeldKey), due to the Ouya quirk mentioned above.
		if (!heldKeys.empty()) {
			HeldKey hk;
			hk.key = key.keyCode;
			hk.deviceId = key.deviceId;
			hk.triggerTime = 0.0; // irrelevant
			hk.move = FocusMove::NEXT;  // irrelevant, not part of the comparison
			if (heldKeys.find(hk) != heldKeys.end()) {
				heldKeys.erase(hk);
				retval = KeyEventResult::ACCEPT;
			}
		}
	}
	return retval;
}

bool KeyEvent(const KeyInput &key, ViewGroup *root) {
	return root->Key(key);
}

void TouchEvent(const TouchInput &touch, ViewGroup *root) {
	focusForced = false;
	root->Touch(touch);
	if ((touch.flags & TouchInputFlags::DOWN) && !focusForced) {
		EnableFocusMovement(false);
	}
}

static void FakeKeyEvent(const KeyInput &key, ViewGroup *root) {
	KeyEventToFocusMoves(key);
	KeyEvent(key, root);
}

void AxisEvent(const AxisInput &axis, ViewGroup *root) {
	enum class DirState {
		NONE = 0,
		POS = 1,
		NEG = 2,
	};
	struct PrevState {
		PrevState() : x(DirState::NONE), y(DirState::NONE) {}
		DirState x;
		DirState y;
	};
	struct StateKey {
		int deviceId;
		int axisId;

		bool operator <(const StateKey &other) const {
			return std::tie(deviceId, axisId) < std::tie(other.deviceId, other.axisId);
		}
	};
	static std::map<StateKey, PrevState> state;
	StateKey stateKey{ axis.deviceId, axis.axisId };

	const float THRESHOLD = 0.75;

	// Cannot use the remapper since this is for the menu, so we provide our own
	// axis->button emulation here.
	auto GenerateKeyFromAxis = [=](DirState old, DirState cur, InputKeyCode neg_key, InputKeyCode pos_key) {
		if (old == cur)
			return;
		if (old == DirState::POS) {
			FakeKeyEvent(KeyInput{ DEVICE_ID_KEYBOARD, pos_key, KeyInputFlags::UP }, root);
		} else if (old == DirState::NEG) {
			FakeKeyEvent(KeyInput{ DEVICE_ID_KEYBOARD, neg_key, KeyInputFlags::UP }, root);
		}
		if (cur == DirState::POS) {
			FakeKeyEvent(KeyInput{ DEVICE_ID_KEYBOARD, pos_key, KeyInputFlags::DOWN }, root);
		} else if (cur == DirState::NEG) {
			FakeKeyEvent(KeyInput{ DEVICE_ID_KEYBOARD, neg_key, KeyInputFlags::DOWN }, root);
		}
	};

	switch (axis.deviceId) {
	case DEVICE_ID_PAD_0:
	case DEVICE_ID_PAD_1:
	case DEVICE_ID_PAD_2:
	case DEVICE_ID_PAD_3:
	case DEVICE_ID_XINPUT_0:
	case DEVICE_ID_XINPUT_1:
	case DEVICE_ID_XINPUT_2:
	case DEVICE_ID_XINPUT_3:
	{
		PrevState &old = state[stateKey];
		DirState dir = DirState::NONE;
		if (axis.value < -THRESHOLD)
			dir = DirState::NEG;
		else if (axis.value > THRESHOLD)
			dir = DirState::POS;

		if (axis.axisId == JOYSTICK_AXIS_X || axis.axisId == JOYSTICK_AXIS_HAT_X) {
			GenerateKeyFromAxis(old.x, dir, NKCODE_DPAD_LEFT, NKCODE_DPAD_RIGHT);
			old.x = dir;
		}
		if (axis.axisId == JOYSTICK_AXIS_Y || axis.axisId == JOYSTICK_AXIS_HAT_Y) {
			int direction = GetAnalogYDirection(axis.deviceId);
			if (direction == 0) {
				// We stupidly interpret the joystick Y axis backwards on Android and Linux instead of reversing
				// it early (see keymaps...). Too late to fix without invalidating a lot of config files, so we
				// reverse it here too.
#if PPSSPP_PLATFORM(ANDROID) || PPSSPP_PLATFORM(LINUX) || PPSSPP_PLATFORM(SWITCH)
				GenerateKeyFromAxis(old.y, dir, NKCODE_DPAD_UP, NKCODE_DPAD_DOWN);
#else
				GenerateKeyFromAxis(old.y, dir, NKCODE_DPAD_DOWN, NKCODE_DPAD_UP);
#endif
			} else if (direction == -1) {
				GenerateKeyFromAxis(old.y, dir, NKCODE_DPAD_UP, NKCODE_DPAD_DOWN);
			} else {
				GenerateKeyFromAxis(old.y, dir, NKCODE_DPAD_DOWN, NKCODE_DPAD_UP);
			}
			old.y = dir;
		}
		break;
	}
	default:
		break;
	}

	root->Axis(axis);
}

static void ProcessHeldKeys(ViewGroup *root) {
	double now = time_now_d();

restart:
	for (std::set<HeldKey>::iterator iter = heldKeys.begin(); iter != heldKeys.end(); ++iter) {
		if (iter->triggerTime < now) {
			KeyInput key;
			key.keyCode = iter->key;
			key.deviceId = iter->deviceId;
			key.flags = KeyInputFlags::DOWN;
			KeyEvent(key, root);

			focusMoves.push_back(iter->move);

			// Cannot modify the current item when looping over a set, so let's do this instead.
			HeldKey hk = *iter;
			heldKeys.erase(hk);
			hk.triggerTime = now + repeatInterval;
			heldKeys.insert(hk);
			goto restart;
		}
	}
}

DialogResult UpdateViewHierarchy(ViewGroup *root, bool canEnableFocusMovement) {
	ProcessHeldKeys(root);
	frameCount++;

	if (!root) {
		ERROR_LOG(Log::UI, "Tried to update a view hierarchy from a zero pointer root");
		return DR_NONE;
	}

	if (focusMoves.size() && canEnableFocusMovement) {
		EnableFocusMovement(true);
		if (!GetFocusedView()) {
			// Find a view to focus.
			View *defaultView = root->GetDefaultFocusView();
			// Can't focus what you can't see.
			if (defaultView && defaultView->GetVisibility() == V_VISIBLE) {
				root->GetDefaultFocusView()->SetFocus(UI::FocusFlags::CAUSE_FOCUS_MOVE);
			} else {
				root->SetFocus(UI::FocusFlags::CAUSE_FOCUS_MOVE);
			}
			root->SubviewFocused(GetFocusedView());
		} else {
			for (FocusMove move : focusMoves) {
				MoveFocus(root, move);
			}
		}
	}
	focusMoves.clear();

	root->Update();
	return DispatchEvents();
}

}  // namespace UI
