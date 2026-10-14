#pragma once

#include <string_view>

#include "ppsspp_config.h"

#import <GameController/GameController.h>
#if !PPSSPP_PLATFORM(TVOS)
#import <CoreMotion/CoreMotion.h>
#endif

#include "Common/Input/InputState.h"

// Code extracted from ViewController.mm, in order to modularize
// and share it between multiple view controllers.

bool InitController(GCController *controller);
void ShutdownController(GCController *controller);

// Returns false unless the controller is a Siri Remote (a micro gamepad profile and no extended one).
bool InitSiriRemote(GCController *controller);

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

#if !PPSSPP_PLATFORM(TVOS)
void ProcessAccelerometerData(CMAccelerometerData *accData);
#endif
InputKeyCode HIDUsageToInputKeyCode(UIKeyboardHIDUsage usage);

void KeyboardPressesBegan(NSSet<UIPress *> *presses, UIPressesEvent *event);
void KeyboardPressesEnded(NSSet<UIPress *> *presses, UIPressesEvent *event);
void SendKeyboardChars(std::string_view str);