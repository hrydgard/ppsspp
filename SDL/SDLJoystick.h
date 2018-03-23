#pragma once
#ifdef _MSC_VER
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

class SDLJoystick{
public:
	SDLJoystick(bool init_SDL = false);
	~SDLJoystick();

	void registerEventHandler();
	void ProcessInput(SDL_Event &event);

private:
	void setUpController(int deviceIndex);
	void setUpControllers();
	keycode_t getKeycodeForButton(SDL_GameControllerButton button);
	int getDeviceIndex(int instanceId);
	bool registeredAsEventHandler;
	std::vector<SDL_GameController *> controllers;
	std::map<int, int> controllerDeviceMap;
};
