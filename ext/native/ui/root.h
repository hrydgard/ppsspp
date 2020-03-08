#pragma once

#include "ui/ui_context.h"
#include "input/input_state.h"

namespace UI {

class ViewGroup;

void LayoutViewHierarchy(const UIContext &dc, ViewGroup *root);
void UpdateViewHierarchy(ViewGroup *root);
// Hooks arrow keys for navigation
bool KeyEvent(const KeyInput &key, ViewGroup *root);
bool TouchEvent(const TouchInput &touch, ViewGroup *root);
bool AxisEvent(const AxisInput &axis, ViewGroup *root);

}
