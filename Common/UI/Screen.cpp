#include "Common/System/Request.h"
#include "Common/UI/Screen.h"
#include "Common/UI/UI.h"
#include "Common/Log.h"

void Screen::focusChanged(ScreenFocusChange focusChange) {
	const char *eventName = "";
	switch (focusChange) {
	case ScreenFocusChange::FOCUS_LOST_TOP:
	#if !defined(MOBILE_DEVICE)
		if (WantsTextInput()) {
			System_NotifyUIEvent(UIEventNotification::TEXT_LOSTFOCUS);
		}
	#endif
		eventName = "FOCUS_LOST_TOP";
		break;
	case ScreenFocusChange::FOCUS_BECAME_TOP:
	#if !defined(MOBILE_DEVICE)
		if (WantsTextInput()) {
			System_NotifyUIEvent(UIEventNotification::TEXT_GOTFOCUS);
		}
	#endif
		eventName = "FOCUS_BECAME_TOP";
		break;
	}
	DEBUG_LOG(Log::UI, "Screen %s got %s", this->tag(), eventName);
}

int Screen::GetRequesterToken() {
	if (token_ < 0) {
		token_ = g_requestManager.GenerateRequesterToken();
	}
	return token_;
}

Screen::~Screen() {
	screenManager_ = nullptr;
	if (token_ >= 0) {
		// To avoid expired callbacks getting called.
		g_requestManager.ForgetRequestsWithToken(token_);
	}
}

const char *DialogResultToString(DialogResult result) {
	switch (result) {
	case DR_NONE: return "DR_NONE";
	case DR_OK: return "DR_OK";
	case DR_CANCEL: return "DR_CANCEL";
	case DR_YES: return "DR_YES";
	case DR_NO: return "DR_NO";
	case DR_BACK: return "DR_BACK";
	default: return "(N/A)";
	}
}
