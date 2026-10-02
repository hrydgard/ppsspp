#pragma once
#include <SDL3/SDL.h>
#include <map>
#include <set>
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
	void UpdateRumble();

private:
	void setUpController(SDL_JoystickID deviceID);
	void setUpControllers();
	InputKeyCode getKeycodeForButton(SDL_GamepadButton button);
	int getDeviceIndex(int instanceId);
	void releaseAllKeys();
	SDL_Gamepad *findController(SDL_JoystickID instanceId) const;
	bool shouldRumble(const InputMapping &mapping) const;
	void updateRumble(SDL_JoystickID instanceId, int inputId, bool down, const InputMapping &mapping);
	void stopRumble(SDL_JoystickID instanceId);
	void stopAllRumble();

	bool registeredAsEventHandler;
	std::vector<SDL_Gamepad *> controllers;
	std::map<int, int> controllerDeviceMap;
	std::map<SDL_JoystickID, std::set<int>> activeRumbleInputs_;
	std::map<SDL_JoystickID, Uint64> rumbleRefreshTicks_;

	// Deduplicate axis events. Pair is device, axis.
	std::map<std::pair<InputDeviceID, InputAxis>, float> prevAxisValue_;
};
