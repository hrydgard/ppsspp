#include <deque>

#include "ppsspp_config.h"

#include "Common/Input/InputState.h"
#include "Common/Log.h"
#include "Common/TimeUtil.h"
#include "Common/UI/Root.h"
#include "Common/UI/ViewGroup.h"

namespace UI {

static std::vector<int> focusMoves;
extern bool focusForced;

static View *focusedView;
static bool focusMovementEnabled;
bool focusForced;

static std::function<void(UISound, float)> soundCallback;
static bool soundEnabled = true;

struct DispatchQueueItem {
	Event *e;
	EventParams params;
};

std::deque<DispatchQueueItem> g_dispatchQueue;

void EventTriggered(Event *e, EventParams params) {
	DispatchQueueItem item{ e, params };
	g_dispatchQueue.push_front(item);
}

void DispatchEvents() {
	while (!g_dispatchQueue.empty()) {
		DispatchQueueItem item;
		if (g_dispatchQueue.empty())
			break;
		item = g_dispatchQueue.back();
		g_dispatchQueue.pop_back();
		if (item.e) {
			item.e->Dispatch(item.params);
		}
	}
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

void SetFocusedView(View *view, bool force) {
	if (focusedView) {
		focusedView->FocusChanged(FF_LOSTFOCUS);
	}
	focusedView = view;
	if (focusedView) {
		focusedView->FocusChanged(FF_GOTFOCUS);
		if (force) {
			focusForced = true;
		}
	}
}

void EnableFocusMovement(bool enable) {
	focusMovementEnabled = enable;
	if (!enable) {
		if (focusedView) {
			focusedView->FocusChanged(FF_LOSTFOCUS);
		}
		focusedView = 0;
	}
}

bool IsFocusMovementEnabled() {
	return focusMovementEnabled;
}

void LayoutViewHierarchy(const UIContext &dc, ViewGroup *root, bool ignoreInsets) {
	if (!root) {
		ERROR_LOG(Log::System, "Tried to layout a view hierarchy from a zero pointer root");
		return;
	}

	Bounds rootBounds = ignoreInsets ? dc.GetBounds() : dc.GetLayoutBounds();

	MeasureSpec horiz(EXACTLY, rootBounds.w);
	MeasureSpec vert(EXACTLY, rootBounds.h);

	// Two phases - measure contents, layout.
	root->Measure(dc, horiz, vert);
	// Root has a specified size. Set it, then let root layout all its children.
	root->SetBounds(rootBounds);
	root->Layout();
}

static void MoveFocus(ViewGroup *root, FocusDirection direction) {
	View *focusedView = GetFocusedView();
	if (!focusedView) {
		// Nothing was focused when we got in here. Focus the first non-group in the hierarchy.
		root->SetFocus();
		return;
	}

	NeighborResult neigh = root->FindNeighbor(focusedView, direction, NeighborResult());
	if (neigh.view) {
		neigh.view->SetFocus();
		root->SubviewFocused(neigh.view);

		PlayUISound(UISound::SELECT);
	}
}

void SetSoundEnabled(bool enabled) {
	soundEnabled = enabled;
}

void SetSoundCallback(std::function<void(UISound, float)> func) {
	soundCallback = func;
}

void PlayUISound(UISound sound, float volume) {
	if (soundEnabled && soundCallback) {
		soundCallback(sound, volume);
	}
}

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

bool IsScrollKey(const KeyInput &input) {
	switch (input.keyCode) {
	case NKCODE_PAGE_UP:
	case NKCODE_PAGE_DOWN:
	case NKCODE_MOVE_HOME:
	case NKCODE_MOVE_END:
		return true;
	default:
		return false;
	}
}

static KeyEventResult KeyEventToFocusMoves(const KeyInput &key) {
	KeyEventResult retval = KeyEventResult::PASS_THROUGH;
	// Ignore repeats for focus moves.
	if ((key.flags & (KEY_DOWN | KEY_IS_REPEAT)) == KEY_DOWN) {
		if (IsDPadKey(key) || IsScrollKey(key)) {
			// Let's only repeat DPAD initially.
			HeldKey hk;
			hk.key = key.keyCode;
			hk.deviceId = key.deviceId;
			hk.triggerTime = time_now_d() + repeatDelay;

			// Check if the key is already held. If it is, ignore it. This is to avoid
			// multiple key repeat mechanisms colliding.
			if (heldKeys.find(hk) != heldKeys.end()) {
				return KeyEventResult::IGNORE_KEY;
			}

			heldKeys.insert(hk);
			focusMoves.push_back(key.keyCode);
			retval = KeyEventResult::ACCEPT;
		}
	}
	if (key.flags & KEY_UP) {
		// We ignore the device ID here (in the comparator for HeldKey), due to the Ouya quirk mentioned above.
		if (!heldKeys.empty()) {
			HeldKey hk;
			hk.key = key.keyCode;
			hk.deviceId = key.deviceId;
			hk.triggerTime = 0.0; // irrelevant
			if (heldKeys.find(hk) != heldKeys.end()) {
				heldKeys.erase(hk);
				retval = KeyEventResult::ACCEPT;
			}
		}
	}
	return retval;
}

KeyEventResult UnsyncKeyEvent(const KeyInput &key, ViewGroup *root) {
	KeyEventResult retval = KeyEventToFocusMoves(key);

	// Ignore volume keys and stuff here. Not elegant but need to propagate bools through the view hierarchy as well...
	switch (key.keyCode) {
	case NKCODE_VOLUME_DOWN:
	case NKCODE_VOLUME_UP:
	case NKCODE_VOLUME_MUTE:
		retval = KeyEventResult::PASS_THROUGH;
		break;
	default:
		if (!(key.flags & KEY_IS_REPEAT)) {
			// If a repeat, we follow what KeyEventToFocusMoves set it to.
			// Otherwise we signal that we used the key, always.
			retval = KeyEventResult::ACCEPT;
		}
		break;
	}
	return retval;
}

bool KeyEvent(const KeyInput &key, ViewGroup *root) {
	return root->Key(key);
}

void TouchEvent(const TouchInput &touch, ViewGroup *root) {
	focusForced = false;
	root->Touch(touch);
	if ((touch.flags & TOUCH_DOWN) && !focusForced) {
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
			FakeKeyEvent(KeyInput{ DEVICE_ID_KEYBOARD, pos_key, KEY_UP }, root);
		} else if (old == DirState::NEG) {
			FakeKeyEvent(KeyInput{ DEVICE_ID_KEYBOARD, neg_key, KEY_UP }, root);
		}
		if (cur == DirState::POS) {
			FakeKeyEvent(KeyInput{ DEVICE_ID_KEYBOARD, pos_key, KEY_DOWN }, root);
		} else if (cur == DirState::NEG) {
			FakeKeyEvent(KeyInput{ DEVICE_ID_KEYBOARD, neg_key, KEY_DOWN }, root);
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
			key.flags = KEY_DOWN;
			KeyEvent(key, root);

			focusMoves.push_back(key.keyCode);

			// Cannot modify the current item when looping over a set, so let's do this instead.
			HeldKey hk = *iter;
			heldKeys.erase(hk);
			hk.triggerTime = now + repeatInterval;
			heldKeys.insert(hk);
			goto restart;
		}
	}
}

void UpdateViewHierarchy(ViewGroup *root) {
	ProcessHeldKeys(root);
	frameCount++;

	if (!root) {
		ERROR_LOG(Log::System, "Tried to update a view hierarchy from a zero pointer root");
		return;
	}

	if (focusMoves.size()) {
		EnableFocusMovement(true);
		if (!GetFocusedView()) {
			// Find a view to focus.
			View *defaultView = root->GetDefaultFocusView();
			// Can't focus what you can't see.
			if (defaultView && defaultView->GetVisibility() == V_VISIBLE) {
				root->GetDefaultFocusView()->SetFocus();
			} else {
				root->SetFocus();
			}
			root->SubviewFocused(GetFocusedView());
		} else {
			for (size_t i = 0; i < focusMoves.size(); i++) {
				switch (focusMoves[i]) {
				case NKCODE_DPAD_LEFT: MoveFocus(root, FOCUS_LEFT); break;
				case NKCODE_DPAD_RIGHT: MoveFocus(root, FOCUS_RIGHT); break;
				case NKCODE_DPAD_UP: MoveFocus(root, FOCUS_UP); break;
				case NKCODE_DPAD_DOWN: MoveFocus(root, FOCUS_DOWN); break;
				case NKCODE_PAGE_UP: MoveFocus(root, FOCUS_PREV_PAGE); break;
				case NKCODE_PAGE_DOWN: MoveFocus(root, FOCUS_NEXT_PAGE); break;
				case NKCODE_MOVE_HOME: MoveFocus(root, FOCUS_FIRST); break;
				case NKCODE_MOVE_END: MoveFocus(root, FOCUS_LAST); break;
				}
			}
		}
		focusMoves.clear();
	}

	root->Update();
	DispatchEvents();
}

}  // namespace UI
