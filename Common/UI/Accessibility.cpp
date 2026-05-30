#include "Common/UI/Accessibility.h"

#include <exception>
#include <mutex>

#include "Common/Log.h"
#include "Common/UI/Screen.h"
#include "Common/UI/UIScreen.h"

namespace UI {

static std::mutex g_accessibilitySnapshotLock;
static std::vector<AccessibilityElementInfo> g_accessibilitySnapshot;
static uint64_t g_accessibilitySnapshotVersion;

static bool BoundsEqual(const Bounds &a, const Bounds &b) {
	return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h;
}

static bool AccessibilitySnapshotsEqual(const std::vector<AccessibilityElementInfo> &a, const std::vector<AccessibilityElementInfo> &b) {
	if (a.size() != b.size()) {
		return false;
	}
	for (size_t i = 0; i < a.size(); ++i) {
		if (a[i].label != b[i].label || !BoundsEqual(a[i].bounds, b[i].bounds) ||
			a[i].role != b[i].role || a[i].enabled != b[i].enabled) {
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
	std::lock_guard<std::mutex> guard(g_accessibilitySnapshotLock);
	if (!AccessibilitySnapshotsEqual(g_accessibilitySnapshot, snapshot)) {
		g_accessibilitySnapshot = std::move(snapshot);
		++g_accessibilitySnapshotVersion;
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
	std::lock_guard<std::mutex> guard(g_accessibilitySnapshotLock);
	if (!g_accessibilitySnapshot.empty()) {
		g_accessibilitySnapshot.clear();
		++g_accessibilitySnapshotVersion;
	}
}

}  // namespace UI
