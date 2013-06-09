// Super basic screen manager. Let's you, well, switch between screens. Can also be used
// to pop one screen in front for a bit while keeping another one running, it's basically
// a native "activity stack". Well actually that part is still a TODO.
//
// Semantics
//
// switchScreen: When you call this, on a newed screen, the ScreenManager takes ownership.
// On the next update, it switches to the new screen and deletes the previous screen.
//
// TODO: A way to do smooth transitions between screens. Will probably involve screenshotting
// the previous screen and then animating it on top of the current screen with transparency
// and/or other similar effects.

#pragma once

#include <vector>

#include "base/basictypes.h"
#include "base/logging.h"
#include "base/display.h"
#include "base/NativeApp.h"

struct InputState;

enum DialogResult {
	DR_OK,
	DR_CANCEL,
	DR_YES,
	DR_NO,
};

class ScreenManager;
class UIContext;

class Screen {
public:
	Screen() : screenManager_(0) { }
	virtual ~Screen() {
		screenManager_ = 0;
	}

	virtual void update(InputState &input) = 0;
	virtual void render() {}
	virtual void deviceLost() {}
	virtual void dialogFinished(const Screen *dialog, DialogResult result) {}
	virtual void touch(const TouchInput &touch) {}
	virtual void sendMessage(const char *msg, const char *value) {}

	ScreenManager *screenManager() { return screenManager_; }
	void setScreenManager(ScreenManager *sm) { screenManager_ = sm; }

	virtual void *dialogData() { return 0; }

	virtual bool isTransparent() { return false; }

private:
	ScreenManager *screenManager_;
	DISALLOW_COPY_AND_ASSIGN(Screen);
};

class Transition {
public:
	Transition() {}
};

enum {
	LAYER_SIDEMENU = 1,
	LAYER_TRANSPARENT = 2,
};

class ScreenManager {
public:
	ScreenManager();
	virtual ~ScreenManager();

	void switchScreen(Screen *screen);
	void update(InputState &input);

	void setUIContext(UIContext *context) { uiContext_ = context; }
	UIContext *getUIContext() { return uiContext_; }

	void render();
	void deviceLost();
	void shutdown();

	// Push a dialog box in front. Currently 1-level only.
	void push(Screen *screen, int layerFlags = 0);

	// Pops the dialog away.
	void finishDialog(const Screen *dialog, DialogResult result = DR_OK);

	// Instant touch, separate from the update() mechanism.
	void touch(const TouchInput &touch);

	// Generic facility for gross hacks :P
	void sendMessage(const char *msg, const char *value);

private:
	void pop();
	void switchToNext();
	void processFinishDialog();
	Screen *topScreen();

	Screen *nextScreen_;
	UIContext *uiContext_;

	const Screen *dialogFinished_;
	DialogResult dialogResult_;

	struct Layer {
		Screen *screen;
		int flags;  // From LAYER_ enum above
	};

	// Dialog stack. These are shown "on top" of base screens and the Android back button works as expected.
	// Used for options, in-game menus and other things you expect to be able to back out from onto something.
	std::vector<Layer> stack_;
};
