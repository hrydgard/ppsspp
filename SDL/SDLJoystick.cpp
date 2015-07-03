#include "SDL/SDLJoystick.h"

#include <iostream>

extern "C" {
	int SDLJoystickThreadWrapper(void *SDLJoy){
		SDLJoystick *stick = static_cast<SDLJoystick *>(SDLJoy);
		stick->runLoop();
		return 0;
	}
}

SDLJoystick::SDLJoystick(bool init_SDL ): thread(NULL), running(true) {
	if (init_SDL)
	{
		SDL_setenv("SDL_JOYSTICK_ALLOW_BACKGROUND_EVENTS", "1", 0);
		SDL_Init(SDL_INIT_JOYSTICK | SDL_INIT_VIDEO);
	}
	fillMapping();

	int numjoys = SDL_NumJoysticks();
	SDL_JoystickEventState(SDL_ENABLE);
	for (int i = 0; i < numjoys; i++) {
		joys.push_back(SDL_JoystickOpen(i));
	}
}

SDLJoystick::~SDLJoystick(){
	if (thread)
	{
		running = false;
		SDL_Event evt;
		evt.type = SDL_USEREVENT;
		SDL_PushEvent(&evt);
		SDL_WaitThread(thread,0);
	}
	for (SDL_Joystick *joy : joys)
	{
		SDL_JoystickClose(joy);
	}
}

void SDLJoystick::startEventLoop(){
	thread = SDL_CreateThread(SDLJoystickThreadWrapper, "joystick",static_cast<void *>(this));
}

void SDLJoystick::ProcessInput(SDL_Event &event){
	switch (event.type) {
	case SDL_JOYAXISMOTION:
		{
			std::map<int, int>::const_iterator i = SDLJoyAxisMap.find(event.jaxis.axis);
			int deviceIndex = getDeviceIndex(event.jaxis.which);
			if (i != SDLJoyAxisMap.end() && deviceIndex >= 0) {
				AxisInput axis;
				axis.axisId = i->second;
				// 1.2 to try to approximate the PSP's clamped rectangular range.
				axis.value = 1.2 * event.jaxis.value / 32767.0f;
				if (axis.value > 1.0f) axis.value = 1.0f;
				if (axis.value < -1.0f) axis.value = -1.0f;
				axis.deviceId = DEVICE_ID_PAD_0 + deviceIndex;
				axis.flags = 0;
				NativeAxis(axis);
			}
			break;
		}

	case SDL_JOYBUTTONDOWN:
		{
			std::map<int, int>::const_iterator i = SDLJoyButtonMap.find(event.jbutton.button);
			int deviceIndex = getDeviceIndex(event.jbutton.which);
			if (i != SDLJoyButtonMap.end() && deviceIndex >= 0) {
				KeyInput key;
				key.flags = KEY_DOWN;
				key.keyCode = i->second;
				key.deviceId = DEVICE_ID_PAD_0 + deviceIndex;
				NativeKey(key);
			}
			break;
		}

	case SDL_JOYBUTTONUP:
		{
			std::map<int, int>::const_iterator i = SDLJoyButtonMap.find(event.jbutton.button);
			int deviceIndex = getDeviceIndex(event.jbutton.which);
			if (i != SDLJoyButtonMap.end() && deviceIndex >= 0) {
				KeyInput key;
				key.flags = KEY_UP;
				key.keyCode = i->second;
				key.deviceId = DEVICE_ID_PAD_0 + deviceIndex;
				NativeKey(key);
			}
			break;
		}

	case SDL_JOYHATMOTION:
		{
			int deviceIndex = getDeviceIndex(event.jhat.which);
			if (deviceIndex < 0) {
				break;
			}
#ifdef _WIN32
			KeyInput key;
			key.deviceId = DEVICE_ID_PAD_0 + deviceIndex;

			key.flags = (event.jhat.value & SDL_HAT_UP)?KEY_DOWN:KEY_UP;
			key.keyCode = NKCODE_DPAD_UP;
			NativeKey(key);
			key.flags = (event.jhat.value & SDL_HAT_LEFT)?KEY_DOWN:KEY_UP;
			key.keyCode = NKCODE_DPAD_LEFT;
			NativeKey(key);
			key.flags = (event.jhat.value & SDL_HAT_DOWN)?KEY_DOWN:KEY_UP;
			key.keyCode = NKCODE_DPAD_DOWN;
			NativeKey(key);
			key.flags = (event.jhat.value & SDL_HAT_RIGHT)?KEY_DOWN:KEY_UP;
			key.keyCode = NKCODE_DPAD_RIGHT;
			NativeKey(key);
#else
			AxisInput axisX;
			AxisInput axisY;
			axisX.axisId = JOYSTICK_AXIS_HAT_X;
			axisY.axisId = JOYSTICK_AXIS_HAT_Y;
			axisX.deviceId = DEVICE_ID_PAD_0 + deviceIndex;
			axisY.deviceId = DEVICE_ID_PAD_0 + deviceIndex;
			axisX.value = 0.0f;
			axisY.value = 0.0f;
			if (event.jhat.value & SDL_HAT_LEFT) axisX.value = -1.0f;
			if (event.jhat.value & SDL_HAT_RIGHT) axisX.value = 1.0f;
			if (event.jhat.value & SDL_HAT_DOWN) axisY.value = 1.0f;
			if (event.jhat.value & SDL_HAT_UP) axisY.value = -1.0f;
			NativeAxis(axisX);
			NativeAxis(axisY);
#endif
			break;
		}

	case SDL_JOYDEVICEADDED:
		{
			int deviceIndex = event.jdevice.which;
			if (deviceIndex >= joys.size()) {
				joys.resize(deviceIndex+1);
			}
			joys[deviceIndex] = SDL_JoystickOpen(deviceIndex);
			SDL_JoystickEventState(SDL_ENABLE);
			break;
		}

	case SDL_JOYDEVICEREMOVED:
		{
			int deviceIndex = getDeviceIndex(event.jdevice.which);
			if (deviceIndex >= 0) {
				SDL_JoystickClose(joys[deviceIndex]);
				joys[deviceIndex] = 0;
			}
			break;
		}
	}
}

int SDLJoystick::getDeviceIndex(int instanceId) {
	for (int i = 0; i < joys.size(); i++) {
		SDL_Joystick *joy = joys[i];
		if (SDL_JoystickInstanceID(joy) == instanceId) {
			return i;
		}
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
