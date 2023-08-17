#include "Common/System/Display.h"
#include "Common/Input/InputState.h"
#include "Common/UI/Root.h"
#include "Common/UI/Screen.h"
#include "Common/UI/ScrollView.h"
#include "Common/UI/UI.h"
#include "Common/UI/View.h"
#include "Common/UI/ViewGroup.h"
#include "Common/UI/IconCache.h"

#include "Common/Log.h"
#include "Common/TimeUtil.h"

#include "Core/KeyMap.h"

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

	if (stack_.size()) {
		stack_.back().screen->update();
	}

	// NOTE: We should not update the OverlayScreen. In fact, we must never update more than one
	// UIScreen in here, because we might end up double-processing the stuff in Root.cpp.

	g_iconCache.FrameUpdate();
}

void ScreenManager::switchToNext() {
	std::lock_guard<std::recursive_mutex> guard(inputLock_);
	if (nextStack_.empty()) {
		ERROR_LOG(SYSTEM, "switchToNext: No nextStack_!");
	}

	Layer temp = {nullptr, 0};
	if (!stack_.empty()) {
		temp = stack_.back();
		stack_.pop_back();
	}
	stack_.push_back(nextStack_.front());
	if (temp.screen) {
		delete temp.screen;
	}
	UI::SetFocusedView(nullptr);

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
		Screen *screen = stack_.back().screen;
		stack_.back().screen->UnsyncTouch(screen->transformTouch(touch));
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

void ScreenManager::axis(const AxisInput &axis) {
	std::lock_guard<std::recursive_mutex> guard(inputLock_);

	// Ignore duplicate values to prevent axis values overwriting each other.
	uint64_t key = ((uint64_t)axis.axisId << 32) | axis.deviceId;
	// Center value far from zero just to ensure we send the first zero.
	// PSP games can't see higher resolution than this.
	int value = 128 + ceilf(axis.value * 127.5f + 127.5f);
	if (lastAxis_[key] == value) {
		return;
	}
	lastAxis_[key] = value;

	// Send center axis to every screen layer.
	if (axis.value == 0) {
		for (auto &layer : stack_) {
			layer.screen->UnsyncAxis(axis);
		}
	} else if (!stack_.empty()) {
		stack_.back().screen->UnsyncAxis(axis);
	}
}

void ScreenManager::deviceLost() {
	for (auto &iter : stack_)
		iter.screen->deviceLost();
	g_iconCache.ClearTextures();
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

void ScreenManager::render() {
	if (!stack_.empty()) {
		switch (stack_.back().flags) {
		case LAYER_TRANSPARENT:
			if (stack_.size() == 1) {
				ERROR_LOG(SYSTEM, "Can't have sidemenu over nothing");
				break;
			} else {
				auto last = stack_.end();
				auto iter = last;
				iter--;
				while (iter->flags == LAYER_TRANSPARENT) {
					iter--;
				}
				auto first = iter;
				_assert_(iter->screen);

				// TODO: Make really sure that this "mismatched" pre/post only happens
				// when screens are "compatible" (both are UIScreens, for example).
				first->screen->preRender();
				while (iter < last) {
					iter->screen->render();
					iter++;
				}
				stack_.back().screen->render();
				if (postRenderCb_) {
					postRenderCb_(getUIContext(), postRenderUserdata_);
				}
				if (overlayScreen_) {
					overlayScreen_->render();
				}
				first->screen->postRender();
				break;
			}
		default:
			_assert_(stack_.back().screen);
			stack_.back().screen->preRender();
			stack_.back().screen->render();
			if (postRenderCb_)
				postRenderCb_(getUIContext(), postRenderUserdata_);
			if (overlayScreen_) {
				overlayScreen_->render();
			}
			stack_.back().screen->postRender();
			break;
		}
	} else {
		ERROR_LOG(SYSTEM, "No current screen!");
	}

	processFinishDialog();
}

void ScreenManager::getFocusPosition(float &x, float &y, float &z) {
	UI::ScrollView::GetLastScrollPosition(x, y);

	UI::View *v = UI::GetFocusedView();
	x += v ? v->GetBounds().x : 0;
	y += v ? v->GetBounds().y : 0;
	z = stack_.size();
}

void ScreenManager::sendMessage(const char *msg, const char *value) {
	if (!strcmp(msg, "recreateviews"))
		RecreateAllViews();
	if (!strcmp(msg, "lost_focus")) {
		TouchInput input{};
		input.x = -50000.0f;
		input.y = -50000.0f;
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
	for (auto layer : stack_)
		delete layer.screen;
	stack_.clear();
	for (auto layer : nextStack_)
		delete layer.screen;
	nextStack_.clear();
	delete overlayScreen_;
	overlayScreen_ = nullptr;
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
	if (nextStack_.empty())
		stack_.push_back(layer);
	else
		nextStack_.push_back(layer);
}

void ScreenManager::pop() {
	std::lock_guard<std::recursive_mutex> guard(inputLock_);
	if (stack_.size()) {
		delete stack_.back().screen;
		stack_.pop_back();
	} else {
		ERROR_LOG(SYSTEM, "Can't pop when stack empty");
	}
}

void ScreenManager::RecreateAllViews() {
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
			for (size_t i = 0; i < stack_.size(); ++i) {
				if (stack_[i].screen == dialogFinished_) {
					stack_.erase(stack_.begin() + i);
				}
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

void ScreenManager::SetOverlayScreen(Screen *screen) {
	if (overlayScreen_) {
		delete overlayScreen_;
	}
	overlayScreen_ = screen;
	overlayScreen_->setScreenManager(this);
}
