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

#include <list>
#include "base/display.h"

struct InputState;

class Screen {
public:
	Screen();
	virtual ~Screen();
	virtual void update(const InputState &input) = 0;
	virtual void render() {}
  virtual void dialogFinished(const Screen &dialog) {}
};

class Transition {
public:
	Transition() {}
};

class ScreenManager {
public:
	ScreenManager();
	virtual ~ScreenManager();

	void switchScreen(Screen *screen);
	void update(const InputState &input);
	void render();
  void shutdown();

  // Push a dialog box in front. Currently 1-level only.
  void push(Screen *screen);

  // Pops the dialog away.
  void pop();
  
private:
  // Base screen. These don't "stack" and you can move in any order between them.
	Screen *currentScreen_;
	Screen *nextScreen_;

  // Dialog stack. These are shown "on top" of base screens and back works as expected.
  // Used for options, in-game menus and other things you expect to be able to back out from onto something.
  std::list<Screen *> dialog_;
};

// Yeah, an old school non enforced singleton. Good enough.
extern ScreenManager screenManager;
