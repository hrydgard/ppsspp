#include "Common/System/Display.h"
#include "Common/System/Request.h"
#include "Common/Input/InputState.h"
#include "Common/UI/Root.h"
#include "Common/UI/Screen.h"
#include "Common/UI/ScrollView.h"
#include "Common/UI/UI.h"
#include "Common/UI/View.h"
#include "Common/UI/ViewGroup.h"

#include "Common/Log.h"
#include "Common/TimeUtil.h"

#include "Core/KeyMap.h"

void Screen::focusChanged(ScreenFocusChange focusChange) {
	const char *eventName = "";
	switch (focusChange) {
	case ScreenFocusChange::FOCUS_LOST_TOP: eventName = "FOCUS_LOST_TOP"; break;
	case ScreenFocusChange::FOCUS_BECAME_TOP: eventName = "FOCUS_BECAME_TOP"; break;
	}
	DEBUG_LOG(SYSTEM, "Screen %s got %s", this->tag(), eventName);
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

ScreenManager::~ScreenManager() {
	shutdown();
}

void ScreenManager::switchScreen(Screen *screen) {
	// TODO: inputLock_ ?

	if (!nextStack_.empty() && screen == nextStack_.front().screen) {
		ERROR_LOG(SYSTEM, "Already switching to this screen");
		return;
	}
	// Note that if a dialog is found, this will be a silent background switch that
	// will only become apparent if the dialog is closed. The previous screen will stick around
	// until that switch.
	// TODO: is this still true?
	if (!nextStack_.empty()) {
		ERROR_LOG(SYSTEM, "Already had a nextStack_! Asynchronous open while doing something? Deleting the new screen.");
		delete screen;
		return;
	}
	if (screen == nullptr) {
		WARN_LOG(SYSTEM, "Switching to a zero screen, this can't be good");
	}
	if (stack_.empty() || screen != stack_.back().screen) {
		screen->setScreenManager(this);
		nextStack_.push_back({ screen, 0 });
	}
}

void ScreenManager::update() {
	std::lock_guard<std::recursive_mutex> guard(inputLock_);
	if (!nextStack_.empty()) {
		switchToNext();
	}

	if (overlayScreen_) {
		// NOTE: This is not a full UIScreen update, to avoid double global event processing.
		overlayScreen_->update();
	}
	// The background screen doesn't need updating.
	if (stack_.size()) {
		stack_.back().screen->update();
	}
}

void ScreenManager::switchToNext() {
	std::lock_guard<std::recursive_mutex> guard(inputLock_);
	if (nextStack_.empty()) {
		ERROR_LOG(SYSTEM, "switchToNext: No nextStack_!");
	}

	Layer temp = {nullptr, 0};
	if (!stack_.empty()) {
		temp = stack_.back();
		temp.screen->focusChanged(ScreenFocusChange::FOCUS_LOST_TOP);
		stack_.pop_back();
	}
	stack_.push_back(nextStack_.front());
	nextStack_.front().screen->focusChanged(ScreenFocusChange::FOCUS_BECAME_TOP);
	if (temp.screen) {
		delete temp.screen;
	}
	UI::SetFocusedView(nullptr);

	// When will this ever happen? Should handle focus here too?
	for (size_t i = 1; i < nextStack_.size(); ++i) {
		stack_.push_back(nextStack_[i]);
	}
	nextStack_.clear();
}

void ScreenManager::touch(const TouchInput &touch) {
	std::lock_guard<std::recursive_mutex> guard(inputLock_);
	// Send release all events to every screen layer.
	if (touch.flags & TOUCH_RELEASE_ALL) {
		for (auto &layer : stack_) {
			Screen *screen = layer.screen;
			layer.screen->UnsyncTouch(screen->transformTouch(touch));
		}
	} else if (!stack_.empty()) {
		// Let the overlay know about touch-downs, to be able to dismiss popups.
		bool skip = false;
		if (overlayScreen_ && (touch.flags & TOUCH_DOWN)) {
			skip = overlayScreen_->UnsyncTouch(overlayScreen_->transformTouch(touch));
		}
		if (!skip) {
			Screen *screen = stack_.back().screen;
			stack_.back().screen->UnsyncTouch(screen->transformTouch(touch));
		}
	}
}

bool ScreenManager::key(const KeyInput &key) {
	std::lock_guard<std::recursive_mutex> guard(inputLock_);
	bool result = false;
	// Send key up to every screen layer, to avoid stuck keys.
	if (key.flags & KEY_UP) {
		for (auto &layer : stack_) {
			result = layer.screen->UnsyncKey(key);
		}
	} else if (!stack_.empty()) {
		result = stack_.back().screen->UnsyncKey(key);
	}
	return result;
}

void ScreenManager::axis(const AxisInput *axes, size_t count) {
	std::lock_guard<std::recursive_mutex> guard(inputLock_);
	if (!stack_.empty()) {
		stack_.back().screen->UnsyncAxis(axes, count);
	}
}

void ScreenManager::deviceLost() {
	for (auto &iter : stack_)
		iter.screen->deviceLost();
}

void ScreenManager::deviceRestored() {
	for (auto &iter : stack_)
		iter.screen->deviceRestored();
}

void ScreenManager::resized() {
	INFO_LOG(SYSTEM, "ScreenManager::resized(dp: %dx%d)", g_display.dp_xres, g_display.dp_yres);
	std::lock_guard<std::recursive_mutex> guard(inputLock_);
	// Have to notify the whole stack, otherwise there will be problems when going back
	// to non-top screens.
	for (auto iter = stack_.begin(); iter != stack_.end(); ++iter) {
		iter->screen->resized();
	}
}

ScreenRenderFlags ScreenManager::render() {
	ScreenRenderFlags flags = ScreenRenderFlags::NONE;
	if (!stack_.empty()) {
		// Collect the screens to render
		TinySet<Screen *, 6> layers;

		// Start at the end, collect screens to form the transparency stack.
		// Then we'll iterate them in reverse order.
		// Note that we skip the overlay screen, we handle it separately.
		// Additionally, we pick up a "background" screen. Normally it will be either
		// the EmuScreen or the actual global background screen.
		auto iter = stack_.end();
		Screen *coveringScreen = nullptr;
		Screen *foundBackgroundScreen = nullptr;
		bool first = true;
		do {
			--iter;
			if (!foundBackgroundScreen && iter->screen->canBeBackground(first)) {
				// There still might be a screen that wants to be background - generally the EmuScreen if present.
				layers.push_back(iter->screen);
				foundBackgroundScreen = iter->screen;
			} else if (!coveringScreen) {
				layers.push_back(iter->screen);
			}
			if (iter->flags != LAYER_TRANSPARENT) {
				coveringScreen = iter->screen;
			}
			first = false;
		} while (iter != stack_.begin());

		if (backgroundScreen_ && !foundBackgroundScreen) {
			layers.push_back(backgroundScreen_);
			foundBackgroundScreen = backgroundScreen_;
		}

		// OK, now we iterate backwards over our little pile of collected screens.
		for (int i = (int)layers.size() - 1; i >= 0; i--) {
			ScreenRenderMode mode = ScreenRenderMode::DEFAULT;
			if (i == (int)layers.size() - 1) {
				// Bottom.
				mode = ScreenRenderMode::FIRST;
				if (i == 0) {
					mode |= ScreenRenderMode::TOP;
				}
			} else if (i == 0) {
				mode = ScreenRenderMode::TOP;
			} else {
				mode = ScreenRenderMode::BEHIND;
			}
			flags |= layers[i]->render(mode);
		}

		if (overlayScreen_) {
			// It doesn't care about mode.
			flags |= overlayScreen_->render(ScreenRenderMode::TOP);
		}

		getUIContext()->Flush();

		if (postRenderCb_) {
			// Really can't render anything after this! Will crash the screenshot mechanism if we do.
			postRenderCb_(getUIContext(), postRenderUserdata_);
		}
	} else {
		ERROR_LOG(SYSTEM, "No current screen!");
	}

	processFinishDialog();
	return flags;
}

void ScreenManager::getFocusPosition(float &x, float &y, float &z) {
	UI::ScrollView::GetLastScrollPosition(x, y);

	UI::View *v = UI::GetFocusedView();
	x += v ? v->GetBounds().x : 0;
	y += v ? v->GetBounds().y : 0;
	z = stack_.size();
}

void ScreenManager::sendMessage(UIMessage message, const char *value) {
	if (message == UIMessage::RECREATE_VIEWS) {
		RecreateAllViews();
	} else if (message == UIMessage::LOST_FOCUS) {
		TouchInput input{};
		input.x = -50000.0f;
		input.y = -50000.0f;
		input.flags = TOUCH_RELEASE_ALL;
		input.timestamp = time_now_d();
		input.id = 0;
		touch(input);
	}

	if (backgroundScreen_) {
		backgroundScreen_->sendMessage(message, value);
	}

	if (!stack_.empty()) {
		stack_.back().screen->sendMessage(message, value);
	}
}

void ScreenManager::shutdown() {
	std::lock_guard<std::recursive_mutex> guard(inputLock_);
	for (const auto &layer : stack_)
		delete layer.screen;
	stack_.clear();
	for (const auto &layer : nextStack_)
		delete layer.screen;
	nextStack_.clear();
	delete overlayScreen_;
	overlayScreen_ = nullptr;
	delete backgroundScreen_;
	backgroundScreen_ = nullptr;
}

void ScreenManager::push(Screen *screen, int layerFlags) {
	std::lock_guard<std::recursive_mutex> guard(inputLock_);
	screen->setScreenManager(this);
	if (screen->isTransparent()) {
		layerFlags |= LAYER_TRANSPARENT;
	}

	// Release touches and unfocus.
	UI::SetFocusedView(nullptr);
	TouchInput input{};
	input.x = -50000.0f;
	input.y = -50000.0f;
	input.flags = TOUCH_RELEASE_ALL;
	input.timestamp = time_now_d();
	input.id = 0;
	touch(input);

	Layer layer = {screen, layerFlags};

	if (!stack_.empty()) {
		stack_.back().screen->focusChanged(ScreenFocusChange::FOCUS_LOST_TOP);
	}

	if (nextStack_.empty()) {
		layer.screen->focusChanged(ScreenFocusChange::FOCUS_BECAME_TOP);
		stack_.push_back(layer);
	} else {
		nextStack_.push_back(layer);
	}
}

void ScreenManager::pop() {
	std::lock_guard<std::recursive_mutex> guard(inputLock_);
	if (!stack_.empty()) {
		stack_.back().screen->focusChanged(ScreenFocusChange::FOCUS_LOST_TOP);

		delete stack_.back().screen;
		stack_.pop_back();

		if (!stack_.empty()) {
			stack_.back().screen->focusChanged(ScreenFocusChange::FOCUS_LOST_TOP);
		}
	} else {
		ERROR_LOG(SYSTEM, "Can't pop when stack empty");
	}
}

void ScreenManager::RecreateAllViews() {
	std::lock_guard<std::recursive_mutex> guard(inputLock_);
	for (auto it = stack_.begin(); it != stack_.end(); ++it) {
		it->screen->RecreateViews();
	}
}

void ScreenManager::finishDialog(Screen *dialog, DialogResult result) {
	if (stack_.empty()) {
		ERROR_LOG(SYSTEM, "Must be in a dialog to finishDialog");
		return;
	}
	if (dialog != stack_.back().screen) {
		ERROR_LOG(SYSTEM, "Wrong dialog being finished!");
		return;
	}
	dialog->onFinish(result);
	dialogFinished_ = dialog;
	dialogResult_ = result;
}

Screen *ScreenManager::dialogParent(const Screen *dialog) const {
	for (size_t i = 1; i < stack_.size(); ++i) {
		if (stack_[i].screen == dialog) {
			// The previous screen was the caller (not necessarily the topmost.)
			return stack_[i - 1].screen;
		}
	}

	return nullptr;
}

void ScreenManager::processFinishDialog() {
	if (dialogFinished_) {
		{
			std::lock_guard<std::recursive_mutex> guard(inputLock_);
			// Another dialog may have been pushed before the render, so search for it.
			Screen *caller = dialogParent(dialogFinished_);
			bool erased = false;
			for (size_t i = 0; i < stack_.size(); ++i) {
				if (stack_[i].screen == dialogFinished_) {
					stack_[i].screen->focusChanged(ScreenFocusChange::FOCUS_LOST_TOP);
					stack_.erase(stack_.begin() + i);
					erased = true;
				}
			}

			if (erased && !stack_.empty()) {
				stack_.back().screen->focusChanged(ScreenFocusChange::FOCUS_BECAME_TOP);
			}

			if (!caller) {
				ERROR_LOG(SYSTEM, "ERROR: no top screen when finishing dialog");
			} else if (caller != topScreen()) {
				// The caller may get confused if we call dialogFinished() now.
				WARN_LOG(SYSTEM, "Skipping non-top dialog when finishing dialog.");
			} else {
				caller->dialogFinished(dialogFinished_, dialogResult_);
			}
		}
		delete dialogFinished_;
		dialogFinished_ = nullptr;
	}
}

void ScreenManager::SetBackgroundOverlayScreens(Screen *backgroundScreen, Screen *overlayScreen) {
	if (backgroundScreen_) {
		delete backgroundScreen_;
	}
	backgroundScreen_ = backgroundScreen;
	backgroundScreen_->setScreenManager(this);

	if (overlayScreen_) {
		delete overlayScreen_;
	}
	overlayScreen_ = overlayScreen;
	overlayScreen_->setScreenManager(this);
}
