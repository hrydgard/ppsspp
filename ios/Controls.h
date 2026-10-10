#pragma once

#include <string_view>

#import <GameController/GameController.h>
#import <CoreMotion/CoreMotion.h>

#include "Common/Input/InputState.h"

// Code extracted from ViewController.mm, in order to modularize
// and share it between multiple view controllers.

bool InitController(GCController *controller);
void ShutdownController(GCController *controller);

struct TouchTracker {
public:
	void Began(NSSet *touches, UIView *view);
	void Moved(NSSet *touches, UIView *view);
	void Ended(NSSet *touches, UIView *view);
	void Cancelled(NSSet *touches, UIView *view);
private:
	void SendTouchEvent(CGPoint point, UIView *view, int code, int pointerId);
	int ToTouchID(UITouch *uiTouch, bool allowAllocate);
	UITouch *touches_[10]{};
};

void ProcessAccelerometerData(CMAccelerometerData *accData);
InputKeyCode HIDUsageToInputKeyCode(UIKeyboardHIDUsage usage);

void KeyboardPressesBegan(NSSet<UIPress *> *presses, UIPressesEvent *event);
void KeyboardPressesEnded(NSSet<UIPress *> *presses, UIPressesEvent *event);
void SendKeyboardChars(std::string_view str);