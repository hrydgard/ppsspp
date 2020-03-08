#include <mutex>
#include <deque>

#include "base/timeutil.h"
#include "ui/root.h"
#include "ui/viewgroup.h"

namespace UI {

static std::mutex focusLock;
static std::vector<int> focusMoves;
extern bool focusForced;

static View *focusedView;
static bool focusMovementEnabled;
bool focusForced;
static std::mutex eventMutex_;

struct DispatchQueueItem {
	Event *e;
	EventParams params;
};

std::deque<DispatchQueueItem> g_dispatchQueue;

void EventTriggered(Event *e, EventParams params) {
	DispatchQueueItem item;
	item.e = e;
	item.params = params;

	std::unique_lock<std::mutex> guard(eventMutex_);
	g_dispatchQueue.push_front(item);
}

void DispatchEvents() {
	while (true) {
		DispatchQueueItem item;
		{
			std::unique_lock<std::mutex> guard(eventMutex_);
			if (g_dispatchQueue.empty())
				break;
			item = g_dispatchQueue.back();
			g_dispatchQueue.pop_back();
		}
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

void LayoutViewHierarchy(const UIContext &dc, ViewGroup *root) {
	if (!root) {
		ELOG("Tried to layout a view hierarchy from a zero pointer root");
		return;
	}
	const Bounds &rootBounds = dc.GetBounds();

	MeasureSpec horiz(EXACTLY, rootBounds.w);
	MeasureSpec vert(EXACTLY, rootBounds.h);

	// Two phases - measure contents, layout.
	root->Measure(dc, horiz, vert);
	// Root has a specified size. Set it, then let root layout all its children.
	root->SetBounds(rootBounds);
	root->Layout();
}

void MoveFocus(ViewGroup *root, FocusDirection direction) {
	if (!GetFocusedView()) {
		// Nothing was focused when we got in here. Focus the first non-group in the hierarchy.
		root->SetFocus();
		return;
	}

	NeighborResult neigh(0, 0);
	neigh = root->FindNeighbor(GetFocusedView(), direction, neigh);

	if (neigh.view) {
		neigh.view->SetFocus();
		root->SubviewFocused(neigh.view);
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
	int key;
	int deviceId;
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

bool KeyEvent(const KeyInput &key, ViewGroup *root) {
	bool retval = false;
	// Ignore repeats for focus moves.
	if ((key.flags & (KEY_DOWN | KEY_IS_REPEAT)) == KEY_DOWN) {
		if (IsDPadKey(key)) {
			// Let's only repeat DPAD initially.
			HeldKey hk;
			hk.key = key.keyCode;
			hk.deviceId = key.deviceId;
			hk.triggerTime = time_now_d() + repeatDelay;

			// Check if the key is already held. If it is, ignore it. This is to avoid
			// multiple key repeat mechanisms colliding.
			if (heldKeys.find(hk) != heldKeys.end()) {
				return false;
			}

			heldKeys.insert(hk);
			std::lock_guard<std::mutex> lock(focusLock);
			focusMoves.push_back(key.keyCode);
			retval = true;
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
				retval = true;
			}
		}
	}

	retval = root->Key(key);

	// Ignore volume keys and stuff here. Not elegant but need to propagate bools through the view hierarchy as well...
	switch (key.keyCode) {
	case NKCODE_VOLUME_DOWN:
	case NKCODE_VOLUME_UP:
	case NKCODE_VOLUME_MUTE:
		retval = false;
		break;
	}

	return retval;
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

			std::lock_guard<std::mutex> lock(focusLock);
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

bool TouchEvent(const TouchInput &touch, ViewGroup *root) {
	focusForced = false;
	root->Touch(touch);
	if ((touch.flags & TOUCH_DOWN) && !focusForced) {
		EnableFocusMovement(false);
	}
	return true;
}

bool AxisEvent(const AxisInput &axis, ViewGroup *root) {
	enum {
		DIR_POS = 1,
		DIR_NEG = 2,
	};

	static uint32_t x_state = 0;
	static uint32_t y_state = 0;

	const float THRESHOLD = 0.75;

	// Cannot use the remapper since this is for the menu, so we provide our own
	// axis->button emulation here.
	auto GenerateKeyFromAxis = [&](uint32_t old, uint32_t cur, keycode_t neg_key, keycode_t pos_key) {
		if (old == cur)
			return;
		if (old == DIR_POS) {
			KeyEvent(KeyInput{ DEVICE_ID_KEYBOARD, pos_key, KEY_UP }, root);
		} else if (old == DIR_NEG) {
			KeyEvent(KeyInput{ DEVICE_ID_KEYBOARD, neg_key, KEY_UP }, root);
		}
		if (cur == DIR_POS) {
			KeyEvent(KeyInput{ DEVICE_ID_KEYBOARD, pos_key, KEY_DOWN }, root);
		} else if (cur == DIR_NEG) {
			KeyEvent(KeyInput{ DEVICE_ID_KEYBOARD, neg_key, KEY_DOWN }, root);
		}
	};

	switch (axis.deviceId) {
	case DEVICE_ID_PAD_0:
	case DEVICE_ID_PAD_1:
	case DEVICE_ID_PAD_2:
	case DEVICE_ID_PAD_3:
	case DEVICE_ID_X360_0:
	case DEVICE_ID_X360_1:
	case DEVICE_ID_X360_2:
	case DEVICE_ID_X360_3:
	{
		uint32_t dir = 0;
		if (axis.axisId == JOYSTICK_AXIS_X) {
			if (axis.value < -THRESHOLD)
				dir = DIR_NEG;
			else if (axis.value > THRESHOLD)
				dir = DIR_POS;
			GenerateKeyFromAxis(x_state, dir, NKCODE_DPAD_LEFT, NKCODE_DPAD_RIGHT);
			x_state = dir;
		}
		if (axis.axisId == JOYSTICK_AXIS_Y) {
			if (axis.value < -THRESHOLD)
				dir = DIR_NEG;
			else if (axis.value > THRESHOLD)
				dir = DIR_POS;
			// TODO: What do we do with devices that are reversed... ?
			GenerateKeyFromAxis(y_state, dir, NKCODE_DPAD_DOWN, NKCODE_DPAD_UP);
			y_state = dir;
		}
		break;
	}
	}

	root->Axis(axis);
	return true;
}

void UpdateViewHierarchy(ViewGroup *root) {
	ProcessHeldKeys(root);
	frameCount++;

	if (!root) {
		ELOG("Tried to update a view hierarchy from a zero pointer root");
		return;
	}

	if (focusMoves.size()) {
		std::lock_guard<std::mutex> lock(focusLock);
		EnableFocusMovement(true);
		if (!GetFocusedView()) {
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
				}
			}
		}
		focusMoves.clear();
	}

	root->Update();
	DispatchEvents();
}

}
