#pragma once

#include "ui/ui_context.h"
#include "input/input_state.h"

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

void LayoutViewHierarchy(const UIContext &dc, ViewGroup *root);
void UpdateViewHierarchy(ViewGroup *root);
// Hooks arrow keys for navigation
bool KeyEvent(const KeyInput &key, ViewGroup *root);
bool TouchEvent(const TouchInput &touch, ViewGroup *root);
bool AxisEvent(const AxisInput &axis, ViewGroup *root);

}  // namespace UI
