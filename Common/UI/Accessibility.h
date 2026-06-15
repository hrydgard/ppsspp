#pragma once

#include <cstdint>
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
	Radio,
	Tab,
	Slider,
	TextField,
	Progress,
	Heading,
	GamepadControl,
	Image,
};

struct AccessibilityElementInfo {
	int id = -1;
	std::string label;
	Bounds bounds;
	AccessibilityRole role = AccessibilityRole::StaticText;
	bool enabled = true;
	bool checked = false;
	bool selected = false;
	bool clickable = false;
	bool longClickable = false;
	float touchX = 0.0f;
	float touchY = 0.0f;
};

std::vector<AccessibilityElementInfo> BuildAccessibilitySnapshot(ScreenManager *screenManager);
void UpdateCachedAccessibilitySnapshot(ScreenManager *screenManager);
std::vector<AccessibilityElementInfo> GetCachedAccessibilitySnapshot();
uint64_t GetCachedAccessibilitySnapshotVersion();
void ClearCachedAccessibilitySnapshot();
void SetAccessibilityEnabled(bool enabled);
bool IsAccessibilityEnabled();
bool FocusAccessibilityElement(ScreenManager *screenManager, int id);
bool PerformAccessibilityClick(int id, bool longClick);
void ReleaseAccessibilityInputs();

}  // namespace UI
