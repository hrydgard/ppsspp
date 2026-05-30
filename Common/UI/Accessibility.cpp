#include "Common/UI/Accessibility.h"

#include <mutex>

#include "Common/UI/Screen.h"
#include "Common/UI/UIScreen.h"

namespace UI {

static std::mutex g_accessibilitySnapshotLock;
static std::vector<AccessibilityElementInfo> g_accessibilitySnapshot;

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
	std::vector<AccessibilityElementInfo> snapshot = BuildAccessibilitySnapshot(screenManager);
	std::lock_guard<std::mutex> guard(g_accessibilitySnapshotLock);
	g_accessibilitySnapshot = std::move(snapshot);
}

std::vector<AccessibilityElementInfo> GetCachedAccessibilitySnapshot() {
	std::lock_guard<std::mutex> guard(g_accessibilitySnapshotLock);
	return g_accessibilitySnapshot;
}

void ClearCachedAccessibilitySnapshot() {
	std::lock_guard<std::mutex> guard(g_accessibilitySnapshotLock);
	g_accessibilitySnapshot.clear();
}

}  // namespace UI
