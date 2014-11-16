#pragma once

#ifdef _WIN32
#include "SDL/SDL.h"
#include "SDL/SDL_joystick.h"
#include "SDL/SDL_thread.h"
#else
#include "SDL.h"
#include "SDL_joystick.h"
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
	void fillMapping()
	{
		// This is just a standard mapping that matches the X360 controller on MacOSX. Names will probably be all wrong
		// on other controllers.

		//TODO: C++11 aggregate initialization
		//would remove runtime overhead completely
#ifdef _WIN32
		SDLJoyButtonMap[0] = NKCODE_BUTTON_2;
		SDLJoyButtonMap[1] = NKCODE_BUTTON_3;
		SDLJoyButtonMap[2] = NKCODE_BUTTON_4;
		SDLJoyButtonMap[3] = NKCODE_BUTTON_1;
		SDLJoyButtonMap[4] = NKCODE_BUTTON_7;
		SDLJoyButtonMap[5] = NKCODE_BUTTON_8;
		SDLJoyButtonMap[6] = NKCODE_BUTTON_9;
		SDLJoyButtonMap[7] = NKCODE_BUTTON_10;
		SDLJoyButtonMap[8] = NKCODE_BUTTON_11;
		SDLJoyButtonMap[9] = NKCODE_BUTTON_12;
		SDLJoyButtonMap[10] = NKCODE_BUTTON_5;
		SDLJoyButtonMap[11] = NKCODE_BUTTON_6;
		SDLJoyButtonMap[12] = NKCODE_BUTTON_7;
		SDLJoyButtonMap[13] = NKCODE_BUTTON_8;
		SDLJoyButtonMap[14] = NKCODE_BUTTON_9;

		SDLJoyAxisMap[0] = JOYSTICK_AXIS_X;
		SDLJoyAxisMap[1] = JOYSTICK_AXIS_Y;
		SDLJoyAxisMap[2] = JOYSTICK_AXIS_Z;
		SDLJoyAxisMap[3] = JOYSTICK_AXIS_RZ;
		SDLJoyAxisMap[4] = JOYSTICK_AXIS_LTRIGGER;
		SDLJoyAxisMap[5] = JOYSTICK_AXIS_RTRIGGER;
#else
		SDLJoyButtonMap[0] = NKCODE_DPAD_UP;
		SDLJoyButtonMap[1] = NKCODE_DPAD_DOWN;
		SDLJoyButtonMap[2] = NKCODE_DPAD_LEFT;
		SDLJoyButtonMap[3] = NKCODE_DPAD_RIGHT;
		SDLJoyButtonMap[4] = NKCODE_BUTTON_10;
		SDLJoyButtonMap[5] = NKCODE_BUTTON_9;
		SDLJoyButtonMap[6] = NKCODE_BUTTON_5;
		SDLJoyButtonMap[7] = NKCODE_BUTTON_6;
		SDLJoyButtonMap[8] = NKCODE_BUTTON_7;
		SDLJoyButtonMap[9] = NKCODE_BUTTON_8;
		SDLJoyButtonMap[10] = NKCODE_BUTTON_SELECT;
		SDLJoyButtonMap[11] = NKCODE_BUTTON_2;
		SDLJoyButtonMap[12] = NKCODE_BUTTON_3;
		SDLJoyButtonMap[13] = NKCODE_BUTTON_4;
		SDLJoyButtonMap[14] = NKCODE_BUTTON_1;
		SDLJoyButtonMap[15] = NKCODE_BUTTON_11;

		SDLJoyAxisMap[0] = JOYSTICK_AXIS_X;
		SDLJoyAxisMap[1] = JOYSTICK_AXIS_Y;
		SDLJoyAxisMap[2] = JOYSTICK_AXIS_Z;
		SDLJoyAxisMap[3] = JOYSTICK_AXIS_RZ;
		SDLJoyAxisMap[4] = JOYSTICK_AXIS_LTRIGGER;
		SDLJoyAxisMap[5] = JOYSTICK_AXIS_RTRIGGER;
#endif
	}
	std::map<int, int> SDLJoyButtonMap;
	std::map<int, int> SDLJoyAxisMap;

	std::vector<SDL_Joystick *> joys;
	SDL_Thread *thread ;
	bool running ;

};
