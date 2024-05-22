#pragma once

#import <GameController/GameController.h>

// Code extracted from ViewController.mm, in order to modularize
// and share it between multiple view controllers.

bool SetupController(GCController *controller);
void SendTouchEvent(float x, float y, int code, int pointerId);