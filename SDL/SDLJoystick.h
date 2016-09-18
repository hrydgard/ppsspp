#pragma once

#ifdef _WIN32
#include "SDL/SDL.h"
#include "SDL/SDL_thread.h"
#else
#include "SDL.h"
#include "SDL_thread.h"
#endif

#include "input/input_state.h"
#include "input/keycodes.h"
#include "net/resolve.h"
#include "base/NativeApp.h"

extern "C" {
	int SDLJoystickThreadWrapper(void *SDLJoy);
}

class SDLJoystick{
	friend int ::SDLJoystickThreadWrapper(void *);
public:
	SDLJoystick(bool init_SDL = false);
	~SDLJoystick();

	void startEventLoop();
	void ProcessInput(SDL_Event &event);

private:

	void runLoop();
	void setUpPlayer1();
	keycode_t getKeycodeForButton(SDL_GameControllerButton button);
	int getDeviceIndex(int instanceId);
	SDL_Thread *thread ;
	bool running ;
	SDL_GameController *player1Controller;
	SDL_JoystickID player1JoyInstanceID;
	
};
