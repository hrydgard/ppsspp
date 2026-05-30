#pragma once

#include <string>
#include <vector>

#include "Common/Math/geom2d.h"

class ScreenManager;

namespace UI {

enum class AccessibilityRole {
	StaticText,
	Button,
	Choice,
	Checkbox,
	Slider,
	TextField,
	Progress,
	Heading,
	GamepadControl,
};

struct AccessibilityElementInfo {
	std::string label;
	Bounds bounds;
	AccessibilityRole role = AccessibilityRole::StaticText;
	bool enabled = true;
};

std::vector<AccessibilityElementInfo> BuildAccessibilitySnapshot(ScreenManager *screenManager);
void UpdateCachedAccessibilitySnapshot(ScreenManager *screenManager);
std::vector<AccessibilityElementInfo> GetCachedAccessibilitySnapshot();
void ClearCachedAccessibilitySnapshot();

}  // namespace UI
