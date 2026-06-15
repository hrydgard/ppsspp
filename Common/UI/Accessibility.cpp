#include "Common/UI/Accessibility.h"

#include <algorithm>
#include <exception>
#include <map>
#include <mutex>

#include "Common/Input/InputState.h"
#include "Common/Log.h"
#include "Common/System/NativeApp.h"
#include "Common/UI/Screen.h"
#include "Common/UI/UIScreen.h"

namespace UI {

static std::mutex g_accessibilitySnapshotLock;
static std::mutex g_accessibilityInputLock;
static std::vector<AccessibilityElementInfo> g_accessibilitySnapshot;
static uint64_t g_accessibilitySnapshotVersion;
static bool g_accessibilityEnabled;
static std::map<int, int> g_heldAccessibilityPointers;

static constexpr int ACCESSIBILITY_TAP_POINTER = 7;
static constexpr int ACCESSIBILITY_FIRST_HOLD_POINTER = 8;
static constexpr int ACCESSIBILITY_LAST_HOLD_POINTER = TOUCH_MAX_POINTERS - 1;

static bool BoundsEqual(const Bounds &a, const Bounds &b) {
	return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h;
}

static bool AccessibilitySnapshotsEqual(const std::vector<AccessibilityElementInfo> &a, const std::vector<AccessibilityElementInfo> &b) {
	if (a.size() != b.size()) {
		return false;
	}
	for (size_t i = 0; i < a.size(); ++i) {
		if (a[i].id != b[i].id || a[i].label != b[i].label || !BoundsEqual(a[i].bounds, b[i].bounds) ||
			a[i].role != b[i].role || a[i].enabled != b[i].enabled || a[i].checked != b[i].checked || a[i].selected != b[i].selected ||
			a[i].clickable != b[i].clickable || a[i].longClickable != b[i].longClickable ||
			a[i].touchX != b[i].touchX || a[i].touchY != b[i].touchY) {
			return false;
		}
	}
	return true;
}

std::vector<AccessibilityElementInfo> BuildAccessibilitySnapshot(ScreenManager *screenManager) {
	std::vector<AccessibilityElementInfo> elements;
	if (!screenManager) {
		return elements;
	}

	Screen *screen = screenManager->topScreen();
	UIScreen *uiScreen = dynamic_cast<UIScreen *>(screen);
	if (!uiScreen) {
		return elements;
	}

	uiScreen->GetAccessibilityElements(elements);
	return elements;
}

void UpdateCachedAccessibilitySnapshot(ScreenManager *screenManager) {
	std::vector<AccessibilityElementInfo> snapshot;
	try {
		snapshot = BuildAccessibilitySnapshot(screenManager);
	} catch (const std::exception &e) {
		WARN_LOG(Log::UI, "Accessibility snapshot failed: %s", e.what());
	} catch (...) {
		WARN_LOG(Log::UI, "Accessibility snapshot failed with unknown exception");
	}
	bool releaseInputs = false;
	{
		std::lock_guard<std::mutex> guard(g_accessibilitySnapshotLock);
		if (!AccessibilitySnapshotsEqual(g_accessibilitySnapshot, snapshot)) {
			const bool hadGamepadControls = std::any_of(g_accessibilitySnapshot.begin(), g_accessibilitySnapshot.end(), [](const AccessibilityElementInfo &info) {
				return info.role == AccessibilityRole::GamepadControl;
			});
			const bool hasGamepadControls = std::any_of(snapshot.begin(), snapshot.end(), [](const AccessibilityElementInfo &info) {
				return info.role == AccessibilityRole::GamepadControl;
			});
			releaseInputs = hadGamepadControls && !hasGamepadControls;
			g_accessibilitySnapshot = std::move(snapshot);
			++g_accessibilitySnapshotVersion;
			NOTICE_LOG(Log::UI, "Accessibility snapshot updated: version=%llu elements=%zu",
				(unsigned long long)g_accessibilitySnapshotVersion, g_accessibilitySnapshot.size());
		}
	}
	if (releaseInputs) {
		ReleaseAccessibilityInputs();
	}
}

std::vector<AccessibilityElementInfo> GetCachedAccessibilitySnapshot() {
	std::lock_guard<std::mutex> guard(g_accessibilitySnapshotLock);
	return g_accessibilitySnapshot;
}

uint64_t GetCachedAccessibilitySnapshotVersion() {
	std::lock_guard<std::mutex> guard(g_accessibilitySnapshotLock);
	return g_accessibilitySnapshotVersion;
}

void ClearCachedAccessibilitySnapshot() {
	ReleaseAccessibilityInputs();
	std::lock_guard<std::mutex> guard(g_accessibilitySnapshotLock);
	if (!g_accessibilitySnapshot.empty()) {
		g_accessibilitySnapshot.clear();
		++g_accessibilitySnapshotVersion;
	}
}

void SetAccessibilityEnabled(bool enabled) {
	if (!enabled) {
		ClearCachedAccessibilitySnapshot();
	}
	std::lock_guard<std::mutex> guard(g_accessibilitySnapshotLock);
	g_accessibilityEnabled = enabled;
}

bool IsAccessibilityEnabled() {
	std::lock_guard<std::mutex> guard(g_accessibilitySnapshotLock);
	return g_accessibilityEnabled;
}

bool FocusAccessibilityElement(ScreenManager *screenManager, int id) {
	if (!screenManager) {
		return false;
	}
	UIScreen *screen = dynamic_cast<UIScreen *>(screenManager->topScreen());
	return screen && screen->FocusAccessibilityElement(id);
}

static void SendAccessibilityTouch(const AccessibilityElementInfo &info, int pointerId, TouchInputFlags flags) {
	TouchInput touch{};
	touch.id = pointerId;
	touch.x = info.touchX;
	touch.y = info.touchY;
	touch.flags = flags;
	NativeTouch(touch);
}

bool PerformAccessibilityClick(int id, bool longClick) {
	AccessibilityElementInfo info;
	{
		std::lock_guard<std::mutex> guard(g_accessibilitySnapshotLock);
		auto iter = std::find_if(g_accessibilitySnapshot.begin(), g_accessibilitySnapshot.end(), [id](const AccessibilityElementInfo &candidate) {
			return candidate.id == id;
		});
		if (iter == g_accessibilitySnapshot.end() || !iter->enabled || !iter->clickable) {
			return false;
		}
		info = *iter;
	}

	{
		std::lock_guard<std::mutex> guard(g_accessibilityInputLock);
		auto held = g_heldAccessibilityPointers.find(id);
		if (held != g_heldAccessibilityPointers.end()) {
			SendAccessibilityTouch(info, held->second, TouchInputFlags::UP);
			g_heldAccessibilityPointers.erase(held);
			return true;
		}
		if (!longClick || !info.longClickable) {
			if (info.role == AccessibilityRole::Tab) {
				return NativeAccessibilityClick(id);
			}
			SendAccessibilityTouch(info, ACCESSIBILITY_TAP_POINTER, TouchInputFlags::DOWN);
			SendAccessibilityTouch(info, ACCESSIBILITY_TAP_POINTER, TouchInputFlags::UP);
			return true;
		}

		for (int pointerId = ACCESSIBILITY_FIRST_HOLD_POINTER; pointerId <= ACCESSIBILITY_LAST_HOLD_POINTER; ++pointerId) {
			const bool used = std::any_of(g_heldAccessibilityPointers.begin(), g_heldAccessibilityPointers.end(), [pointerId](const auto &entry) {
				return entry.second == pointerId;
			});
			if (!used) {
				SendAccessibilityTouch(info, pointerId, TouchInputFlags::DOWN);
				g_heldAccessibilityPointers[id] = pointerId;
				return true;
			}
		}
	}
	return false;
}

void ReleaseAccessibilityInputs() {
	{
		std::lock_guard<std::mutex> guard(g_accessibilityInputLock);
		g_heldAccessibilityPointers.clear();
	}
	TouchInput touch{};
	touch.flags = TouchInputFlags::RELEASE_ALL;
	NativeTouch(touch);
}

}  // namespace UI
