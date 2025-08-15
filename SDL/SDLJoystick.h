#pragma once
#ifdef _MSC_VER
#include "SDL/SDL.h"
#else
#if PPSSPP_PLATFORM(MAC)
#include "SDL2/SDL.h"
#else
#include "SDL.h"
#endif
#endif
#include <map>

#include "Common/Input/InputState.h"
#include "Common/Input/KeyCodes.h"
#include "Common/Net/Resolve.h"

class SDLJoystick{
public:
	SDLJoystick(bool init_SDL = false);
	~SDLJoystick();

	void registerEventHandler();
	void ProcessInput(const SDL_Event &event);

private:
	void setUpController(int deviceIndex);
	void setUpControllers();
	InputKeyCode getKeycodeForButton(SDL_GameControllerButton button);
	int getDeviceIndex(int instanceId);

	bool registeredAsEventHandler;
	std::vector<SDL_GameController *> controllers;
	std::map<int, int> controllerDeviceMap;

	// Deduplicate axis events. Pair is device, axis.
	std::map<std::pair<InputDeviceID, InputAxis>, float> prevAxisValue_;
};
