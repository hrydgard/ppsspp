#include "SDL/SDLJoystick.h"
#include "Core/Config.h"

#include <iostream>

extern "C" {
	int SDLJoystickThreadWrapper(void *SDLJoy){
		SDLJoystick *stick = static_cast<SDLJoystick *>(SDLJoy);
		stick->runLoop();
		return 0;
	}
}

SDLJoystick::SDLJoystick(bool init_SDL ): thread(NULL), running(true), player1Controller(NULL), player1JoyInstanceID(-1) {
	if (init_SDL)
	{
		SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
		SDL_Init(SDL_INIT_JOYSTICK | SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER);
	}
	
	if (SDL_GameControllerAddMappingsFromFile("assets/gamecontrollerdb.txt") == -1)
	{
			printf("Failed to load control pad mappings, please place gamecontrollerdb.txt in your assets directory\n");
	}
	else {
		printf("Loaded game controller mappings for SDL2\n");
		setUpPlayer1();
	}
}

void SDLJoystick::setUpPlayer1()
{
	int numjoys = SDL_NumJoysticks();
	int joyIndex = -1;
	for (int i = 0; i < numjoys; i++) {
		if (joyIndex == -1 && SDL_IsGameController(i))
		{
			joyIndex = i;
			break;
		}
	}
	if (joyIndex != -1)
	{
		printf("Player 1 will be using joystick %d, %s\n", joyIndex, SDL_JoystickNameForIndex(joyIndex));
		player1Controller = SDL_GameControllerOpen(joyIndex);
		if (player1Controller && SDL_GameControllerGetAttached(player1Controller))
		{
			player1JoyInstanceID = SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(player1Controller));
			char *mapping = SDL_GameControllerMapping(player1Controller);
			if (mapping == NULL)
			{
				printf("control pad %s is not in the game controller database, unable to set mappings!\n", SDL_JoystickNameForIndex(joyIndex));
			}
			else
			{
				printf("control pad mapping: %s\n", SDL_GameControllerMapping(player1Controller));
			}
		}
		else
		{
			printf("Error! Could not open controller\n");
		}
	}
}

SDLJoystick::~SDLJoystick(){
	if (thread)
	{
		running = false;
		if (player1Controller)
		{
			SDL_GameControllerClose(player1Controller);
		}
		SDL_Event evt;
		evt.type = SDL_USEREVENT;
		SDL_PushEvent(&evt);
		SDL_WaitThread(thread,0);
	}
}

void SDLJoystick::startEventLoop(){
	thread = SDL_CreateThread(SDLJoystickThreadWrapper, "joystick",static_cast<void *>(this));
}

keycode_t SDLJoystick::getKeycodeForButton(SDL_GameControllerButton button)
{
	switch (button)
	{
		case SDL_CONTROLLER_BUTTON_DPAD_UP: return NKCODE_DPAD_UP;
		case SDL_CONTROLLER_BUTTON_DPAD_DOWN: return NKCODE_DPAD_DOWN;
		case SDL_CONTROLLER_BUTTON_DPAD_LEFT: return NKCODE_DPAD_LEFT;
		case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: return NKCODE_DPAD_RIGHT;
		case SDL_CONTROLLER_BUTTON_A: return NKCODE_BUTTON_2;
		case SDL_CONTROLLER_BUTTON_B: return NKCODE_BUTTON_3;
		case SDL_CONTROLLER_BUTTON_X: return NKCODE_BUTTON_4;
		case SDL_CONTROLLER_BUTTON_Y: return NKCODE_BUTTON_1;
		case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: return NKCODE_BUTTON_5;
		case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: return NKCODE_BUTTON_6;
		case SDL_CONTROLLER_BUTTON_START: return NKCODE_BUTTON_10;
		case SDL_CONTROLLER_BUTTON_BACK: return NKCODE_BUTTON_9; // select button
		case SDL_CONTROLLER_BUTTON_GUIDE: return NKCODE_BACK; // pause menu
	}
	return NKCODE_UNKNOWN;
}

void SDLJoystick::ProcessInput(SDL_Event &event){
	switch (event.type) {
		case SDL_CONTROLLERBUTTONDOWN:
		{
			if (event.cbutton.which == player1JoyInstanceID && event.cbutton.state == SDL_PRESSED)
			{
				keycode_t code = getKeycodeForButton((SDL_GameControllerButton)event.cbutton.button);
				if (code != NKCODE_UNKNOWN)
				{
					KeyInput key;
					key.flags = KEY_DOWN;
					key.keyCode = code;
					key.deviceId = DEVICE_ID_PAD_0 + getDeviceIndex(event.cbutton.which);
					NativeKey(key);
				}
			}
			break;
		}
		case SDL_CONTROLLERBUTTONUP:
		{
			if (event.cbutton.which == player1JoyInstanceID && event.cbutton.state == SDL_RELEASED)
			{
				keycode_t code = getKeycodeForButton((SDL_GameControllerButton)event.cbutton.button);
				if (code != NKCODE_UNKNOWN)
				{
					KeyInput key;
					key.flags = KEY_UP;
					key.keyCode = code;
					key.deviceId = DEVICE_ID_PAD_0 + getDeviceIndex(event.cbutton.which);
					NativeKey(key);
				}
			}
			break;
		}
		case SDL_CONTROLLERAXISMOTION:
		{
			if (event.caxis.which == player1JoyInstanceID)
			{
				AxisInput axis;
				axis.axisId = event.caxis.axis;
				// 1.2 to try to approximate the PSP's clamped rectangular range.
				axis.value = 1.2 * event.caxis.value / 32767.0f;
				if (axis.value > 1.0f) axis.value = 1.0f;
				if (axis.value < -1.0f) axis.value = -1.0f;
				axis.deviceId = DEVICE_ID_PAD_0 + getDeviceIndex(event.caxis.which);
				axis.flags = 0;
				NativeAxis(axis);
				break;
			}
		}
		case SDL_CONTROLLERDEVICEREMOVED:
		{
			// for removal events, "which" is the instance ID for SDL_CONTROLLERDEVICEREMOVED
			if (event.cdevice.which == player1JoyInstanceID)
			{
				printf("control pad removed for player 1\n");
				if (player1Controller){
						SDL_GameControllerClose(player1Controller);
						player1Controller = NULL;
				}
				// are there any other control pads we could use?
				setUpPlayer1();
			}
			break;
		}
		case SDL_CONTROLLERDEVICEADDED:
		{
			// for add events, "which" is the device index!
			if (!player1Controller)
			{
				if (SDL_IsGameController(event.cdevice.which))
				{
					player1Controller = SDL_GameControllerOpen(event.cdevice.which);
					SDL_GameControllerEventState(SDL_ENABLE);
					if (player1Controller && SDL_GameControllerGetAttached(player1Controller))
					{
						player1JoyInstanceID = SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(player1Controller));
						printf("Player 1 will be using control pad: %s, instance ID: %d\n", SDL_GameControllerName(player1Controller), player1JoyInstanceID);
						char *mapping = SDL_GameControllerMapping(player1Controller);
						if (mapping == NULL)
						{
							printf("control pad %s is not in the game controller database, unable to set mappings!\n", SDL_GameControllerName(player1Controller));
						}
						else
						{
							printf("control pad mapping: %s\n", SDL_GameControllerMapping(player1Controller));
						}
					}
					else
					{
						printf("Error! Could not open controller\n");
					}
				}
			}
			break;
		}
	}
}

int SDLJoystick::getDeviceIndex(int instanceId) {
	int numjoys = SDL_NumJoysticks();
	SDL_Joystick *joy;
	for (int i = 0; i < numjoys; i++)
	{
		joy = SDL_JoystickOpen(i);
		if (SDL_JoystickInstanceID(joy) == instanceId)
		{
			SDL_JoystickClose(joy);
			return i;
		}
		SDL_JoystickClose(joy);
	}
	return -1;
}

void SDLJoystick::runLoop(){
	while (running){
		SDL_Event evt;
		int res = SDL_WaitEvent(&evt);
		if (res){
			ProcessInput(evt);
		}
	}
}
