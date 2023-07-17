#pragma once

#include <functional>

#include "Common/UI/Context.h"
#include "Common/Input/InputState.h"

namespace UI {

// The ONLY global is the currently focused item.
// Can be and often is null.
void EnableFocusMovement(bool enable);
bool IsFocusMovementEnabled();
View *GetFocusedView();
void SetFocusedView(View *view, bool force = false);
void RemoveQueuedEventsByEvent(Event *e);
void RemoveQueuedEventsByView(View * v);

void EventTriggered(Event *e, EventParams params);
void DispatchEvents();

class ViewGroup;

void LayoutViewHierarchy(const UIContext &dc, ViewGroup *root, bool ignoreInsets);
void UpdateViewHierarchy(ViewGroup *root);

enum class KeyEventResult {
	IGNORE_KEY,  // Don't let it be processed.
	PASS_THROUGH,  // Let it be processed, but return false.
	ACCEPT,  // Let it be processed, but return true.
};

// Hooks arrow keys for navigation
KeyEventResult UnsyncKeyEvent(const KeyInput &key, ViewGroup *root);

bool KeyEvent(const KeyInput &key, ViewGroup *root);
void TouchEvent(const TouchInput &touch, ViewGroup *root);
void AxisEvent(const AxisInput &axis, ViewGroup *root);

enum class UISound {
	SELECT = 0,
	BACK,
	CONFIRM,
	TOGGLE_ON,
	TOGGLE_OFF,
	ACHIEVEMENT_UNLOCKED,
	LEADERBOARD_SUBMITTED,
	COUNT,
};

void SetSoundEnabled(bool enabled);
void SetSoundCallback(std::function<void(UISound, float)> func);

// This is only meant for actual UI navigation sound, not achievements.
// Call directly into the player for other UI effects.
void PlayUISound(UISound sound, float volume = 0.25f);

}  // namespace UI
