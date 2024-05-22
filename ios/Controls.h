#pragma once

#import <GameController/GameController.h>

// Code extracted from ViewController.mm, in order to modularize
// and share it between multiple view controllers.

bool SetupController(GCController *controller);

struct TouchTracker {
	void Began(NSSet *touches, UIView *view);
	void Moved(NSSet *touches, UIView *view);
	void Ended(NSSet *touches, UIView *view);
	void Cancelled(NSSet *touches, UIView *view);
private:
	void SendTouchEvent(float x, float y, int code, int pointerId);
	int ToTouchID(UITouch *uiTouch, bool allowAllocate);
	UITouch *touches_[10]{};
};