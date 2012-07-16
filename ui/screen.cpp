#include "base/logging.h"
#include "input/input_state.h"
#include "ui/screen.h"

ScreenManager screenManager;

Screen::Screen() { }
Screen::~Screen() { }

ScreenManager::ScreenManager() {
	currentScreen_=0;
	nextScreen_=0;
}

ScreenManager::~ScreenManager() {
	delete currentScreen_;
}

void ScreenManager::switchScreen(Screen *screen) {
  // Note that if a dialog is found, this will be a silent background switch that
  // will only become apparent if the dialog is closed. The previous screen will stick around
  // until that switch.
  if (nextScreen_ != 0) {
    FLOG("WTF? Already had a nextScreen_");
  }
  if (screen != currentScreen_) {
    nextScreen_ = screen;
  }
}

void ScreenManager::update(const InputState &input) {
  if (dialog_.size()) {
    dialog_.back()->update(input);
    return;
  }

	if (nextScreen_) {
    Screen *temp = currentScreen_;
		currentScreen_ = nextScreen_;
    delete temp;
    temp = 0;
		nextScreen_ = 0;
	}
	if (currentScreen_) {
		currentScreen_->update(input);
  }
}

void ScreenManager::render() {
  if (dialog_.size()) {
    dialog_.back()->render();
    return;
  }
	if (currentScreen_) {
		currentScreen_->render();
	}
	else {
		ELOG("No current screen!");
	}
}

void ScreenManager::shutdown() {
  if (nextScreen_) {
    delete nextScreen_;
    nextScreen_ = 0;
  }
  if (currentScreen_) {
    delete currentScreen_;
    currentScreen_ = 0;
  }
}

void ScreenManager::push(Screen *screen) {
  dialog_.push_back(screen);
}

void ScreenManager::pop() {
  if (dialog_.size()) {
    delete dialog_.back();
    dialog_.pop_back();
  } else {
    ELOG("Can't push when no dialog is shown");
  }
}