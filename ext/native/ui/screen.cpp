#include "base/display.h"
#include "base/logging.h"
#include "base/timeutil.h"
#include "input/input_state.h"
#include "ui/root.h"
#include "ui/screen.h"
#include "ui/ui.h"
#include "ui/view.h"

ScreenManager::ScreenManager() {
	nextScreen_ = 0;
	uiContext_ = 0;
	dialogFinished_ = 0;
}

ScreenManager::~ScreenManager() {
	shutdown();
}

void ScreenManager::switchScreen(Screen *screen) {
	if (screen == nextScreen_) {
		ELOG("Already switching to this screen");
		return;
	}
	// Note that if a dialog is found, this will be a silent background switch that
	// will only become apparent if the dialog is closed. The previous screen will stick around
	// until that switch.
	// TODO: is this still true?
	if (nextScreen_ != 0) {
		ELOG("Already had a nextScreen_! Asynchronous open while doing something? Deleting the new screen.");
		delete screen;
		return;
	}
	if (screen == 0) {
		WLOG("Swiching to a zero screen, this can't be good");
	}
	if (stack_.empty() || screen != stack_.back().screen) {
		nextScreen_ = screen;
		nextScreen_->setScreenManager(this);
	}
}

void ScreenManager::update() {
	std::lock_guard<std::recursive_mutex> guard(inputLock_);
	if (nextScreen_) {
		switchToNext();
	}

	if (stack_.size()) {
		stack_.back().screen->update();
	}
}

void ScreenManager::switchToNext() {
	std::lock_guard<std::recursive_mutex> guard(inputLock_);
	if (!nextScreen_) {
		ELOG("switchToNext: No nextScreen_!");
	}

	Layer temp = {0, 0};
	if (!stack_.empty()) {
		temp = stack_.back();
		stack_.pop_back();
	}
	Layer newLayer = {nextScreen_, 0};
	stack_.push_back(newLayer);
	if (temp.screen) {
		delete temp.screen;
	}
	nextScreen_ = 0;
	UI::SetFocusedView(0);
}

bool ScreenManager::touch(const TouchInput &touch) {
	std::lock_guard<std::recursive_mutex> guard(inputLock_);
	bool result = false;
	// Send release all events to every screen layer.
	if (touch.flags & TOUCH_RELEASE_ALL) {
		for (auto &layer : stack_) {
			Screen *screen = layer.screen;
			result = layer.screen->touch(screen->transformTouch(touch));
		}
	} else if (!stack_.empty()) {
		Screen *screen = stack_.back().screen;
		result = stack_.back().screen->touch(screen->transformTouch(touch));
	}
	return result;
}

bool ScreenManager::key(const KeyInput &key) {
	std::lock_guard<std::recursive_mutex> guard(inputLock_);
	bool result = false;
	// Send key up to every screen layer.
	if (key.flags & KEY_UP) {
		for (auto &layer : stack_) {
			result = layer.screen->key(key);
		}
	} else if (!stack_.empty()) {
		result = stack_.back().screen->key(key);
	}
	return result;
}

bool ScreenManager::axis(const AxisInput &axis) {
	std::lock_guard<std::recursive_mutex> guard(inputLock_);
	bool result = false;
	// Send center axis to every screen layer.
	if (axis.value == 0) {
		for (auto &layer : stack_) {
			result = layer.screen->axis(axis);
		}
	} else if (!stack_.empty()) {
		result = stack_.back().screen->axis(axis);
	}
	return result;
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
	ILOG("ScreenManager::resized(dp: %dx%d)", dp_xres, dp_yres);
	std::lock_guard<std::recursive_mutex> guard(inputLock_);
	// Have to notify the whole stack, otherwise there will be problems when going back
	// to non-top screens.
	for (auto iter = stack_.begin(); iter != stack_.end(); ++iter) {
		iter->screen->resized();
	}
}

void ScreenManager::render() {
	if (!stack_.empty()) {
		switch (stack_.back().flags) {
		case LAYER_SIDEMENU:
		case LAYER_TRANSPARENT:
			if (stack_.size() == 1) {
				ELOG("Can't have sidemenu over nothing");
				break;
			} else {
				auto iter = stack_.end();
				iter--;
				iter--;
				Layer backback = *iter;

				// TODO: Make really sure that this "mismatched" pre/post only happens
				// when screens are "compatible" (both are UIScreens, for example).
				backback.screen->preRender();
				backback.screen->render();
				stack_.back().screen->render();
				if (postRenderCb_)
					postRenderCb_(getUIContext(), postRenderUserdata_);
				backback.screen->postRender();
				break;
			}
		default:
			stack_.back().screen->preRender();
			stack_.back().screen->render();
			if (postRenderCb_)
				postRenderCb_(getUIContext(), postRenderUserdata_);
			stack_.back().screen->postRender();
			break;
		}
	} else {
		ELOG("No current screen!");
	}

	processFinishDialog();
}

void ScreenManager::sendMessage(const char *msg, const char *value) {
	if (!strcmp(msg, "recreateviews"))
		RecreateAllViews();
	if (!strcmp(msg, "lost_focus")) {
		TouchInput input;
		input.flags = TOUCH_RELEASE_ALL;
		input.timestamp = time_now_d();
		input.id = 0;
		touch(input);
	}
	if (!stack_.empty())
		stack_.back().screen->sendMessage(msg, value);
}

Screen *ScreenManager::topScreen() const {
	if (!stack_.empty())
		return stack_.back().screen;
	else
		return 0;
}

void ScreenManager::shutdown() {
	std::lock_guard<std::recursive_mutex> guard(inputLock_);
	for (auto x = stack_.begin(); x != stack_.end(); x++)
		delete x->screen;
	stack_.clear();
	delete nextScreen_;
	nextScreen_ = nullptr;
}

void ScreenManager::push(Screen *screen, int layerFlags) {
	std::lock_guard<std::recursive_mutex> guard(inputLock_);
	if (nextScreen_ && stack_.empty()) {
		// we're during init, this is OK
		switchToNext();
	}
	screen->setScreenManager(this);
	if (screen->isTransparent()) {
		layerFlags |= LAYER_TRANSPARENT;
	}

	// Release touches and unfocus.
	UI::SetFocusedView(nullptr);
	TouchInput input;
	input.flags = TOUCH_RELEASE_ALL;
	input.timestamp = time_now_d();
	input.id = 0;
	touch(input);

	Layer layer = {screen, layerFlags};
	stack_.push_back(layer);
}

void ScreenManager::pop() {
	std::lock_guard<std::recursive_mutex> guard(inputLock_);
	if (stack_.size()) {
		delete stack_.back().screen;
		stack_.pop_back();
	} else {
		ELOG("Can't pop when stack empty");
	}
}

void ScreenManager::RecreateAllViews() {
	for (auto it = stack_.begin(); it != stack_.end(); ++it) {
		it->screen->RecreateViews();
	}
}

void ScreenManager::finishDialog(Screen *dialog, DialogResult result) {
	if (stack_.empty()) {
		ELOG("Must be in a dialog to finishDialog");
		return;
	}
	if (dialog != stack_.back().screen) {
		ELOG("Wrong dialog being finished!");
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
		std::lock_guard<std::recursive_mutex> guard(inputLock_);
		// Another dialog may have been pushed before the render, so search for it.
		Screen *caller = dialogParent(dialogFinished_);
		for (size_t i = 0; i < stack_.size(); ++i) {
			if (stack_[i].screen == dialogFinished_) {
				stack_.erase(stack_.begin() + i);
			}
		}

		if (!caller) {
			ELOG("ERROR: no top screen when finishing dialog");
		} else if (caller != topScreen()) {
			// The caller may get confused if we call dialogFinished() now.
			WLOG("Skipping non-top dialog when finishing dialog.");
		} else {
			caller->dialogFinished(dialogFinished_, dialogResult_);
		}
		delete dialogFinished_;
		dialogFinished_ = nullptr;
	}
}
