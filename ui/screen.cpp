#include "base/logging.h"
#include "input/input_state.h"
#include "ui/screen.h"
#include "ui/ui.h"

Screen::Screen(bool isUiScreen) : screenManager_(0), isUiScreen_(isUiScreen) { }
Screen::~Screen() { }

ScreenManager::ScreenManager() {
	nextScreen_ = 0;
	uiContext_ = 0;
	dialogFinished_ = 0;
}

ScreenManager::~ScreenManager() {
	shutdown();
}

void ScreenManager::switchScreen(Screen *screen) {
	// Note that if a dialog is found, this will be a silent background switch that
	// will only become apparent if the dialog is closed. The previous screen will stick around
	// until that switch.
	// TODO: is this still true?
	if (nextScreen_ != 0) {
		FLOG("WTF? Already had a nextScreen_");
	}
	if (stack_.empty() || screen != stack_.back().screen) {
		nextScreen_ = screen;
		nextScreen_->setScreenManager(this);
	}
}

void ScreenManager::update(InputState &input) {
	if (nextScreen_) {
		ILOG("Screen switch! Top of the stack switches.");
		switchToNext();
	}

	if (stack_.size()) {
		stack_.back().screen->update(input);
	}
}

void ScreenManager::switchToNext()
{
	Layer temp = {0, 0};
	if (!stack_.empty())
	{
		temp = stack_.back();
		stack_.pop_back();
	}
	Layer newLayer = {nextScreen_, 0};
	stack_.push_back(newLayer);
	delete temp.screen;
	nextScreen_ = 0;
}

void ScreenManager::touch(int pointer, float x, float y, double time, TouchEvent event)
{
	if (stack_.size()) {
		stack_.back().screen->touch(pointer, x, y, time, event);
		return;
	}
}

void ScreenManager::render() {
	if (stack_.size()) {
		switch (stack_.back().flags)
		{
		case LAYER_SIDEMENU:
			if (stack_.size() == 1)
			{
				ELOG("Can't have sidemenu over nothing");
				break;
			}
			else
			{
				auto iter = stack_.end();
				iter--;
				iter--;
				Layer backback = *iter;
				UIDisableBegin();
				// Also shift to the right somehow...
				backback.screen->render();
				UIDisableEnd();
				stack_.back().screen->render();
				break;
			}
		default:
			stack_.back().screen->render();
			break;
		}
	} else {
		ELOG("No current screen!");
	}

	processFinishDialog();
}

void ScreenManager::sendMessage(const char *msg, const char *value)
{
	if (stack_.size())
		stack_.back().screen->sendMessage(msg, value);
}

void ScreenManager::deviceLost()
{
	if (stack_.size())
		stack_.back().screen->deviceLost();
	// Dialogs too? Nah, they should only use the standard UI texture anyway.
	// TODO: Change this when it becomes necessary.
}

Screen *ScreenManager::topScreen() {
	if (stack_.size())
		return stack_.back().screen;
	else
		return 0;
}

void ScreenManager::shutdown() {
	for (auto x = stack_.begin(); x != stack_.end(); x++)
	{
		delete x->screen;
	}
	stack_.clear();
	delete nextScreen_;
	nextScreen_ = 0;
}

void ScreenManager::push(Screen *screen, int layerFlags) {
	if (nextScreen_ && stack_.empty()) {
		// we're during init, this is OK
		switchToNext();
	}
	screen->setScreenManager(this);
	Layer layer = {screen, layerFlags};
	stack_.push_back(layer);
}

void ScreenManager::pop() {
	if (stack_.size()) {
		delete stack_.back().screen;
		stack_.pop_back();
	} else {
		ELOG("Can't pop when stack empty");
	}
}

void ScreenManager::finishDialog(const Screen *dialog, DialogResult result)
{
	if (dialog != stack_.back().screen)
	{
		ELOG("Wrong dialog being finished!");
		return;
	}
	if (!stack_.size()) {
		ELOG("Must be in a dialog to finishDialog");
		return;
	}
	dialogFinished_ = dialog;
	dialogResult_ = result;
}

void ScreenManager::processFinishDialog()
{
	if (dialogFinished_)
	{
		if (stack_.size()) {
			stack_.pop_back();
		}

		Screen *caller = topScreen();
		caller->dialogFinished(dialogFinished_, dialogResult_);
		delete dialogFinished_;
		dialogFinished_ = 0;
	}
}
