#include "Common/UI/Accessibility.h"

#include "Common/UI/Screen.h"
#include "Common/UI/UIScreen.h"

namespace UI {

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

}  // namespace UI
