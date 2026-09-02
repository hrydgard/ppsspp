#pragma once
#include <SDL3/SDL.h>
#include <map>
#include <vector>

#include "Common/Input/InputState.h"
#include "Common/Input/KeyCodes.h"
#include "Common/Net/Resolve.h"

class SDLJoystick{
public:
	SDLJoystick(bool init_SDL = false);
	~SDLJoystick();

	void registerEventHandler();
	void ProcessInput(const SDL_Event &event);
	static void Rumble(int padIndex, bool start);
	void syncControllerMirror();

private:
	void setUpController(SDL_JoystickID deviceID);
	void setUpControllers();
	InputKeyCode getKeycodeForButton(SDL_GamepadButton button);
	int getDeviceIndex(int instanceId);

	bool registeredAsEventHandler;
	std::vector<SDL_Gamepad *> controllers;
	static std::vector<SDL_Gamepad *> g_sdlJoystickControllers;
	std::map<int, int> controllerDeviceMap;

	// Deduplicate axis events. Pair is device, axis.
	std::map<std::pair<InputDeviceID, InputAxis>, float> prevAxisValue_;
};
