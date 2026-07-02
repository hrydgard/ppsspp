#include <algorithm>

#include "Common/Log.h"
#include "Common/System/Display.h"
#include "Common/System/System.h"
#include "Common/System/Request.h"
#include "Common/Input/InputState.h"
#include "Common/Input/KeyCodes.h"
#include "Common/UI/UIScreen.h"
#include "Common/UI/Context.h"
#include "Common/UI/Screen.h"
#include "Common/UI/Root.h"
#include "Common/Render/DrawBuffer.h"

static constexpr bool ClickDebug = false;

UIScreen::UIScreen() : Screen() {
	lastOrientation_ = GetDeviceOrientation();
}

UIScreen::~UIScreen() {
	delete root_;
}

// This is the source of truth for orientation for configuration and rendering.
DeviceOrientation UIScreen::GetDeviceOrientation() const {
	// TODO: On some platforms, we can do a more sophisticated check.
	return g_display.GetDeviceOrientation();
}

void UIScreen::DoRecreateViews() {
	if (!recreateViews_) {
		return;
	}

	UI::PersistMap persisted;
	bool persisting = root_ != nullptr;
	if (persisting) {
		root_->PersistData(UI::PERSIST_SAVE, "root", persisted);
	}

	delete root_;
	root_ = nullptr;
	CreateViews();
	UI::View *defaultView = root_ ? root_->GetDefaultFocusView() : nullptr;
	if (defaultView && defaultView->GetVisibility() == UI::V_VISIBLE) {
		defaultView->SetFocus(UI::FocusFlags::CAUSE_OTHER);
	}
	recreateViews_ = false;

	if (persisting && root_ != nullptr) {
		root_->PersistData(UI::PERSIST_RESTORE, "root", persisted);

		// Update layout and refocus so things scroll into view.
		// This is for resizing down, when focused on something now offscreen.
		UI::LayoutViewHierarchy(*screenManager()->getUIContext(), RootMargins(), root_, LayoutMode(), UseImmersiveMode());
		UI::View *focused = UI::GetFocusedView();
		if (focused) {
			root_->SubviewFocused(focused);
		}
	}

	// NOTE: We also wipe the requester token. It's possible that views were created with the old token, so any pending requests from them must be invalidated.
	WipeRequesterToken();
}

bool UIScreen::touch(const TouchInput &touch) {
	if (ClickDebug && root_ && (touch.flags & TouchInputFlags::DOWN)) {
		INFO_LOG(Log::System, "Touch down!");
		std::vector<UI::View *> views;
		root_->Query(touch.x, touch.y, views);
		for (auto view : views) {
			INFO_LOG(Log::System, "%s", view->DescribeLog().c_str());
		}
	}

	if (!ignoreInput_ && root_) {
		UI::TouchEvent(touch, root_);
	}
	return true;
}

void UIScreen::axis(const AxisInput &axis) {
	if (!ignoreInput_ && root_) {
		UI::AxisEvent(axis, root_);
	}
}

bool UIScreen::key(const KeyInput &key) {
	if (!ignoreInput_ && root_) {
		return UI::KeyEvent(key, root_);
	} else {
		return false;
	}
}

void UIScreen::update() {
	DeviceOrientation orientation = GetDeviceOrientation();
	if (orientation != lastOrientation_) {
		RecreateViews();
		lastOrientation_ = orientation;
	}

	DoRecreateViews();

	if (root_) {
		DialogResult result = UpdateViewHierarchy(root_);
		if (result != DR_NONE) {
			TriggerFinish(result);
		}
	}
}

void UIScreen::deviceLost() {
	if (root_)
		root_->DeviceLost();
}

void UIScreen::deviceRestored(Draw::DrawContext *draw) {
	if (root_)
		root_->DeviceRestored(draw);
}

Bounds UIScreen::GetLayoutBounds(UIContext &dc) const {
	return dc.GetLayoutBounds(LayoutMode(), UseImmersiveMode());
}

ScreenRenderFlags UIScreen::render(ScreenRenderMode mode) {
	DoRecreateViews();

	UIContext &uiContext = *screenManager()->getUIContext();
	if (root_) {
		UI::LayoutViewHierarchy(uiContext, RootMargins(), root_, LayoutMode(), UseImmersiveMode());
	}

	uiContext.PushTransform({translation_, scale_, alpha_});

	uiContext.Begin();
	DrawBackground(uiContext);
	if (root_) {
		root_->Draw(uiContext);
	}
	uiContext.Flush();
	DrawForeground(uiContext);
	uiContext.Flush();

	uiContext.PopTransform();

	return ScreenRenderFlags::NONE;
}

TouchInput UIScreen::transformTouch(const TouchInput &touch) {
	TouchInput updated = touch;

	float x = touch.x - translation_.x;
	float y = touch.y - translation_.y;
	// Scale around the center as the origin.
	updated.x = (x - g_display.dp_xres * 0.5f) / scale_.x + g_display.dp_xres * 0.5f;
	updated.y = (y - g_display.dp_yres * 0.5f) / scale_.y + g_display.dp_yres * 0.5f;

	return updated;
}

void UIScreen::TriggerFinish(DialogResult result) {
	// From here on, this dialog cannot receive input.
	ignoreInput_ = true;
	screenManager()->finishDialog(this, result);
}

bool UIDialogScreen::key(const KeyInput &key) {
	bool retval = UIScreen::key(key);
	if (!retval && (key.flags & KeyInputFlags::DOWN) && UI::IsEscapeKey(key)) {
		if (finished_) {
			ERROR_LOG(Log::System, "Screen already finished");
		} else if (!firstFrame_) {
			finished_ = true;
			TriggerFinish(DR_BACK);
			UI::PlayUISound(UI::UISound::BACK);
		}
		return true;
	}
	return retval;
}

void UIDialogScreen::sendMessage(UIMessage message, const char *value) {
	Screen *screen = screenManager()->dialogParent(this);
	if (screen) {
		screen->sendMessage(message, value);
	}
}

UIDialogScreen::~UIDialogScreen() {
	System_NotifyUIEvent(UIEventNotification::DIALOG_CLOSED);
}

bool UIScreen::IsOnTop() const {
	return screenManager()->topScreen() == this;
}

void UIScreen::OnBack(UI::EventParams &e) {
	TriggerFinish(DR_BACK);
}

void UIScreen::OnOK(UI::EventParams &e) {
	TriggerFinish(DR_OK);
}

void UIScreen::OnCancel(UI::EventParams &e) {
	TriggerFinish(DR_CANCEL);
}
