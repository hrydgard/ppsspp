#pragma once

#ifdef _WIN32
#include "SDL/SDL.h"
#include "SDL/SDL_joystick.h"
#include "SDL/SDL_gamecontroller.h"
#include "SDL/SDL_thread.h"
#else
#include "SDL.h"
#include "SDL_joystick.h"
#include "SDL_gamecontroller.h"
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
	void fillMapping(int joyIndex);
	void axisMappingHelper(SDL_GameController* controller, SDL_GameControllerAxis axis, keycode_t buttonKeyCode, AndroidJoystickAxis axisCode);
	void buttonMappingHelper(SDL_GameController* controller, SDL_GameControllerButton button, keycode_t buttonKeyCode);
	
	std::map<int, int> SDLJoyButtonMap;
	std::map<int, int> SDLJoyAxisMap;

	std::vector<SDL_Joystick *> joys;
	SDL_Thread *thread ;
	bool running ;

	int getDeviceIndex(int instanceId);
};
