#include <algorithm>

#include "Common/Log.h"
#include "Common/System/Display.h"
#include "Common/System/System.h"
#include "Common/System/Request.h"
#include "Common/Input/InputState.h"
#include "Common/Input/KeyCodes.h"
#include "Common/Math/curves.h"
#include "Common/UI/UIScreen.h"
#include "Common/UI/Context.h"
#include "Common/UI/Screen.h"
#include "Common/UI/Root.h"
#include "Common/Data/Text/I18n.h"
#include "Common/Render/DrawBuffer.h"

static const bool ClickDebug = false;

UIScreen::UIScreen()
	: Screen() {
	lastVertical_ = UseVerticalLayout();
}

UIScreen::~UIScreen() {
	delete root_;
}

bool UIScreen::UseVerticalLayout() const {
	return g_display.dp_yres > g_display.dp_xres * 1.1f;
}

void UIScreen::DoRecreateViews() {
	if (recreateViews_) {
		std::lock_guard<std::recursive_mutex> guard(screenManager()->inputLock_);

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
			defaultView->SetFocus();
		}
		recreateViews_ = false;

		if (persisting && root_ != nullptr) {
			root_->PersistData(UI::PERSIST_RESTORE, "root", persisted);

			// Update layout and refocus so things scroll into view.
			// This is for resizing down, when focused on something now offscreen.
			UI::LayoutViewHierarchy(*screenManager()->getUIContext(), root_, ignoreInsets_);
			UI::View *focused = UI::GetFocusedView();
			if (focused) {
				root_->SubviewFocused(focused);
			}
		}
	}
}

void UIScreen::touch(const TouchInput &touch) {
	if (!ignoreInput_ && root_) {
		UI::TouchEvent(touch, root_);
	}
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

bool UIScreen::UnsyncTouch(const TouchInput &touch) {
	if (ClickDebug && root_ && (touch.flags & TOUCH_DOWN)) {
		INFO_LOG(SYSTEM, "Touch down!");
		std::vector<UI::View *> views;
		root_->Query(touch.x, touch.y, views);
		for (auto view : views) {
			INFO_LOG(SYSTEM, "%s", view->DescribeLog().c_str());
		}
	}

	QueuedEvent ev{};
	ev.type = QueuedEventType::TOUCH;
	ev.touch = touch;
	std::lock_guard<std::mutex> guard(eventQueueLock_);
	eventQueue_.push_back(ev);
	return false;
}

void UIScreen::UnsyncAxis(const AxisInput *axes, size_t count) {
	QueuedEvent ev{};
	ev.type = QueuedEventType::AXIS;
	std::lock_guard<std::mutex> guard(eventQueueLock_);
	for (size_t i = 0; i < count; i++) {
		ev.axis = axes[i];
		eventQueue_.push_back(ev);
	}
}

bool UIScreen::UnsyncKey(const KeyInput &key) {
	bool retval = false;
	if (root_) {
		// TODO: Make key events async too. The return value is troublesome, though.
		switch (UI::UnsyncKeyEvent(key, root_)) {
		case UI::KeyEventResult::ACCEPT:
			retval = true;
			break;
		case UI::KeyEventResult::PASS_THROUGH:
			retval = false;
			break;
		case UI::KeyEventResult::IGNORE_KEY:
			return false;
		}
	}

	QueuedEvent ev{};
	ev.type = QueuedEventType::KEY;
	ev.key = key;
	std::lock_guard<std::mutex> guard(eventQueueLock_);
	eventQueue_.push_back(ev);
	return retval;
}

void UIScreen::update() {
	bool vertical = UseVerticalLayout();
	if (vertical != lastVertical_) {
		RecreateViews();
		lastVertical_ = vertical;
	}

	DoRecreateViews();

	if (root_) {
		UpdateViewHierarchy(root_);
	}

	while (true) {
		QueuedEvent ev{};
		{
			std::lock_guard<std::mutex> guard(eventQueueLock_);
			if (!eventQueue_.empty()) {
				ev = eventQueue_.front();
				eventQueue_.pop_front();
			} else {
				break;
			}
		}
		if (ignoreInput_) {
			continue;
		}
		switch (ev.type) {
		case QueuedEventType::KEY:
			key(ev.key);
			break;
		case QueuedEventType::TOUCH:
			if (ClickDebug && (ev.touch.flags & TOUCH_DOWN)) {
				INFO_LOG(SYSTEM, "Touch down!");
				std::vector<UI::View *> views;
				root_->Query(ev.touch.x, ev.touch.y, views);
				for (auto view : views) {
					INFO_LOG(SYSTEM, "%s", view->DescribeLog().c_str());
				}
			}
			touch(ev.touch);
			break;
		case QueuedEventType::AXIS:
			axis(ev.axis);
			break;
		}
	}
}

void UIScreen::deviceLost() {
	if (root_)
		root_->DeviceLost();
}

void UIScreen::deviceRestored() {
	if (root_)
		root_->DeviceRestored(screenManager()->getDrawContext());
}

void UIScreen::SetupViewport() {
	using namespace Draw;
	Draw::DrawContext *draw = screenManager()->getDrawContext();
	_dbg_assert_(draw != nullptr);
	// Bind and clear the back buffer
	draw->BindFramebufferAsRenderTarget(nullptr, { RPAction::CLEAR, RPAction::CLEAR, RPAction::CLEAR, 0xFF000000 }, "UI");
	screenManager()->getUIContext()->BeginFrame();

	Draw::Viewport viewport;
	viewport.TopLeftX = 0;
	viewport.TopLeftY = 0;
	viewport.Width = g_display.pixel_xres;
	viewport.Height = g_display.pixel_yres;
	viewport.MaxDepth = 1.0;
	viewport.MinDepth = 0.0;
	draw->SetViewport(viewport);
	draw->SetTargetSize(g_display.pixel_xres, g_display.pixel_yres);
}

ScreenRenderFlags UIScreen::render(ScreenRenderMode mode) {
	if (mode & ScreenRenderMode::FIRST) {
		SetupViewport();
	}

	DoRecreateViews();

	UIContext &uiContext = *screenManager()->getUIContext();
	if (root_) {
		UI::LayoutViewHierarchy(uiContext, root_, ignoreInsets_);
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
	if (!retval && (key.flags & KEY_DOWN) && UI::IsEscapeKey(key)) {
		if (finished_) {
			ERROR_LOG(SYSTEM, "Screen already finished");
		} else {
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

UI::EventReturn UIScreen::OnBack(UI::EventParams &e) {
	TriggerFinish(DR_BACK);
	return UI::EVENT_DONE;
}

UI::EventReturn UIScreen::OnOK(UI::EventParams &e) {
	TriggerFinish(DR_OK);
	return UI::EVENT_DONE;
}

UI::EventReturn UIScreen::OnCancel(UI::EventParams &e) {
	TriggerFinish(DR_CANCEL);
	return UI::EVENT_DONE;
}

PopupScreen::PopupScreen(std::string_view title, std::string_view button1, std::string_view button2)
	: title_(title) {
	auto di = GetI18NCategory(I18NCat::DIALOG);
	if (!button1.empty())
		button1_ = di->T(button1);
	if (!button2.empty())
		button2_ = di->T(button2);
	alpha_ = 0.0f;  // inherited
}

void PopupScreen::touch(const TouchInput &touch) {
	if (!box_ || (touch.flags & TOUCH_DOWN) == 0) {
		// Handle down-presses here.
		UIDialogScreen::touch(touch);
		return;
	}

	// Extra bounds to avoid closing the dialog while trying to aim for something
	// near the edge. Now that we only close on actual down-events, we can shrink
	// this border a bit.
	if (!box_->GetBounds().Expand(30.0f, 30.0f).Contains(touch.x, touch.y)) {
		TriggerFinish(DR_CANCEL);
	}

	UIDialogScreen::touch(touch);
}

bool PopupScreen::key(const KeyInput &key) {
	if (key.flags & KEY_DOWN) {
		if ((key.keyCode == NKCODE_ENTER || key.keyCode == NKCODE_NUMPAD_ENTER) && defaultButton_) {
			UI::EventParams e{};
			defaultButton_->OnClick.Trigger(e);
			return true;
		}
	}

	return UIDialogScreen::key(key);
}

void PopupScreen::update() {
	UIDialogScreen::update();

	if (defaultButton_)
		defaultButton_->SetEnabled(CanComplete(DR_OK));

	float animatePos = 1.0f;

	++frames_;
	if (finishFrame_ >= 0) {
		float leadOut = bezierEaseInOut((frames_ - finishFrame_) * (1.0f / (float)FRAMES_LEAD_OUT));
		animatePos = 1.0f - leadOut;

		if (frames_ >= finishFrame_ + FRAMES_LEAD_OUT) {
			// Actual finish happens here.
			screenManager()->finishDialog(this, finishResult_);
		}
	} else if (frames_ < FRAMES_LEAD_IN) {
		float leadIn = bezierEaseInOut(frames_ * (1.0f / (float)FRAMES_LEAD_IN));
		animatePos = leadIn;
	}

	if (animatePos < 1.0f) {
		alpha_ = animatePos;
		scale_.x = 0.9f + animatePos * 0.1f;
		scale_.y =  0.9f + animatePos * 0.1f;

		if (hasPopupOrigin_) {
			float xoff = popupOrigin_.x - g_display.dp_xres / 2;
			float yoff = popupOrigin_.y - g_display.dp_yres / 2;

			// Pull toward the origin a bit.
			translation_.x = xoff * (1.0f - animatePos) * 0.2f;
			translation_.y = yoff * (1.0f - animatePos) * 0.2f;
		} else {
			translation_.y = -g_display.dp_yres * (1.0f - animatePos) * 0.2f;
		}
	} else {
		alpha_ = 1.0f;
		scale_.x = 1.0f;
		scale_.y = 1.0f;
		translation_.x = 0.0f;
		translation_.y = 0.0f;
	}
}

void PopupScreen::SetPopupOrigin(const UI::View *view) {
	hasPopupOrigin_ = true;
	popupOrigin_ = view->GetBounds().Center();
}

void PopupScreen::SetPopupOffset(float y) {
	offsetY_ = y;
}

void PopupScreen::TriggerFinish(DialogResult result) {
	if (CanComplete(result)) {
		ignoreInput_ = true;
		finishFrame_ = frames_;
		finishResult_ = result;

		OnCompleted(result);
	}
	// Inform UI that popup close to hide OSK (if visible)
	System_NotifyUIState("popup_closed");
}

void PopupScreen::CreateViews() {
	using namespace UI;
	UIContext &dc = *screenManager()->getUIContext();

	AnchorLayout *anchor = new AnchorLayout(new LayoutParams(FILL_PARENT, FILL_PARENT));
	anchor->Overflow(false);
	root_ = anchor;

	float yres = screenManager()->getUIContext()->GetBounds().h;

	box_ = new LinearLayout(ORIENT_VERTICAL,
		new AnchorLayoutParams(PopupWidth(), FillVertical() ? yres - 30 : WRAP_CONTENT, dc.GetBounds().centerX(), dc.GetBounds().centerY() + offsetY_, NONE, NONE, true));

	root_->Add(box_);
	box_->SetBG(dc.theme->popupStyle.background);
	box_->SetHasDropShadow(hasDropShadow_);
	// Since we scale a bit, make the dropshadow bleed past the edges.
	box_->SetDropShadowExpand(std::max(g_display.dp_xres, g_display.dp_yres));
	box_->SetSpacing(0.0f);

	if (HasTitleBar()) {
		View* title = new PopupHeader(title_);
		box_->Add(title);
	}

	CreatePopupContents(box_);
	root_->SetDefaultFocusView(box_);
	if (ShowButtons() && !button1_.empty()) {
		// And the two buttons at the bottom.
		LinearLayout *buttonRow = new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(200, WRAP_CONTENT));
		buttonRow->SetSpacing(0);
		Margins buttonMargins(5, 5);

		// Adjust button order to the platform default.
		if (System_GetPropertyBool(SYSPROP_OK_BUTTON_LEFT)) {
			defaultButton_ = buttonRow->Add(new Button(button1_, new LinearLayoutParams(1.0f, buttonMargins)));
			defaultButton_->OnClick.Handle<UIScreen>(this, &UIScreen::OnOK);
			if (!button2_.empty())
				buttonRow->Add(new Button(button2_, new LinearLayoutParams(1.0f, buttonMargins)))->OnClick.Handle<UIScreen>(this, &UIScreen::OnCancel);
		} else {
			if (!button2_.empty())
				buttonRow->Add(new Button(button2_, new LinearLayoutParams(1.0f)))->OnClick.Handle<UIScreen>(this, &UIScreen::OnCancel);
			defaultButton_ = buttonRow->Add(new Button(button1_, new LinearLayoutParams(1.0f)));
			defaultButton_->OnClick.Handle<UIScreen>(this, &UIScreen::OnOK);
		}

		box_->Add(buttonRow);
	}
}
