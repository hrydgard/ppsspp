#include <iostream>
#include <string>

#include "Common/File/FileUtil.h"
#include "Common/File/VFS/VFS.h"
#include "Common/StringUtils.h"
#include "Common/System/NativeApp.h"
#include "Common/System/System.h"

#include "Core/Config.h"
#include "Core/KeyMap.h"
#include "SDL/SDLJoystick.h"

using namespace std;

static int SDLJoystickEventHandlerWrapper(void* userdata, SDL_Event* event)
{
	static_cast<SDLJoystick *>(userdata)->ProcessInput(*event);
	return 0;
}

SDLJoystick::SDLJoystick(bool init_SDL ) : registeredAsEventHandler(false) {
	SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
	if (init_SDL) {
		SDL_Init(SDL_INIT_JOYSTICK | SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER);
	}

	const char *dbPath = "gamecontrollerdb.txt";
	cout << "loading control pad mappings from " << dbPath << ": ";

	size_t size;
	u8 *mappingData = g_VFS.ReadFile(dbPath, &size);
	if (mappingData) {
		SDL_RWops *rw = SDL_RWFromConstMem(mappingData, size);
		// 1 to free the rw after use
		if (SDL_GameControllerAddMappingsFromRW(rw, 1) == -1) {
			cout << "Failed to read mapping data - corrupt?" << endl;
		}
		delete[] mappingData;
	} else {
		cout << "gamecontrollerdb.txt missing" << endl;
	}
	cout << "SUCCESS!" << endl;
	setUpControllers();
}

void SDLJoystick::setUpControllers() {
	int numjoys = SDL_NumJoysticks();
	for (int i = 0; i < numjoys; i++) {
		setUpController(i);
	}
	if (controllers.size() > 0) {
		cout << "pad 1 has been assigned to control pad: " << SDL_GameControllerName(controllers.front()) << endl;
	}
}

void SDLJoystick::setUpController(int deviceIndex) {
	static constexpr int cbGUID = 33;
	char pszGUID[cbGUID];

	if (!SDL_IsGameController(deviceIndex)) {
		cout << "Control pad device " << deviceIndex << " not supported by SDL game controller database, attempting to create default mapping..." << endl;
		SDL_Joystick *joystick = SDL_JoystickOpen(deviceIndex);
		if (joystick) {
			SDL_JoystickGetGUIDString(SDL_JoystickGetGUID(joystick), pszGUID, cbGUID);
			// create default mapping - this is the PS3 dual shock mapping
			const std::string safeName = ReplaceAll(SDL_JoystickName(joystick), ",", "");
			std::string mapping = std::string(pszGUID) + "," + safeName + ",x:b3,a:b0,b:b1,y:b2,back:b8,guide:b10,start:b9,dpleft:b15,dpdown:b14,dpright:b16,dpup:b13,leftshoulder:b4,lefttrigger:a2,rightshoulder:b6,rightshoulder:b5,righttrigger:a5,leftstick:b7,leftstick:b11,rightstick:b12,leftx:a0,lefty:a1,rightx:a3,righty:a4";
			if (SDL_GameControllerAddMapping(mapping.c_str()) == 1){
				cout << "Added default mapping ok" << endl;
			} else {
				cout << "Failed to add default mapping" << endl;
			}
			SDL_JoystickClose(joystick);
		} else {
			cout << "Failed to get joystick identifier. Read-only device? Control pad device " + std::to_string(deviceIndex) << endl;
		}
	} else {
		SDL_Joystick *joystick = SDL_JoystickOpen(deviceIndex);
		if (joystick) {
			SDL_JoystickGetGUIDString(SDL_JoystickGetGUID(joystick), pszGUID, cbGUID);
			SDL_JoystickClose(joystick);
		}
	}
	SDL_GameController *controller = SDL_GameControllerOpen(deviceIndex);
	if (controller) {
		if (SDL_GameControllerGetAttached(controller)) {
			controllers.push_back(controller);
			controllerDeviceMap[SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(controller))] = deviceIndex;
			cout << "found control pad: " << SDL_GameControllerName(controller) << ", loading mapping: ";
			// NOTE: The case to InputDeviceID here is wrong, we should do some kind of lookup.
			KeyMap::NotifyPadConnected((InputDeviceID)deviceIndex, std::string(pszGUID) + ": " + SDL_GameControllerName(controller));
			auto mapping = SDL_GameControllerMapping(controller);
			if (mapping == NULL) {
				//cout << "FAILED" << endl;
				cout << "Could not find mapping in SDL2 controller database" << endl;
			} else {
				cout << "SUCCESS, mapping is:" << endl << mapping << endl;
			}
		} else {
			SDL_GameControllerClose(controller);
		}
	}
}

SDLJoystick::~SDLJoystick() {
	if (registeredAsEventHandler) {
		SDL_DelEventWatch(SDLJoystickEventHandlerWrapper, this);
	}
	for (auto & controller : controllers) {
		SDL_GameControllerClose(controller);
	}
}

void SDLJoystick::registerEventHandler() {
	SDL_AddEventWatch(SDLJoystickEventHandlerWrapper, this);
	registeredAsEventHandler = true;
}

InputKeyCode SDLJoystick::getKeycodeForButton(SDL_GameControllerButton button) {
	switch (button) {
	case SDL_CONTROLLER_BUTTON_DPAD_UP:
		return NKCODE_DPAD_UP;
	case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
		return NKCODE_DPAD_DOWN;
	case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
		return NKCODE_DPAD_LEFT;
	case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
		return NKCODE_DPAD_RIGHT;
	case SDL_CONTROLLER_BUTTON_A:
		return NKCODE_BUTTON_2;
	case SDL_CONTROLLER_BUTTON_B:
		return NKCODE_BUTTON_3;
	case SDL_CONTROLLER_BUTTON_X:
		return NKCODE_BUTTON_4;
	case SDL_CONTROLLER_BUTTON_Y:
		return NKCODE_BUTTON_1;
	case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER:
		return NKCODE_BUTTON_5;
	case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:
		return NKCODE_BUTTON_6;
	case SDL_CONTROLLER_BUTTON_START:
		return NKCODE_BUTTON_10;
	case SDL_CONTROLLER_BUTTON_BACK:
		return NKCODE_BUTTON_9; // select button
	case SDL_CONTROLLER_BUTTON_GUIDE:
		return NKCODE_BACK; // pause menu
	case SDL_CONTROLLER_BUTTON_LEFTSTICK:
		return NKCODE_BUTTON_THUMBL;
	case SDL_CONTROLLER_BUTTON_RIGHTSTICK:
		return NKCODE_BUTTON_THUMBR;

	// Found these limits by checking out the SDL2 branch of the SDL repo, doing git blame, then `git tag --contains (commit)` etc.
#if SDL_VERSION_ATLEAST(2, 0, 16)
	case SDL_CONTROLLER_BUTTON_MISC1:
		return NKCODE_BUTTON_11;
#endif
#if SDL_VERSION_ATLEAST(2, 0, 28)
	case SDL_CONTROLLER_BUTTON_PADDLE1:
		return NKCODE_BUTTON_12;
	case SDL_CONTROLLER_BUTTON_PADDLE2:
		return NKCODE_BUTTON_13;
	case SDL_CONTROLLER_BUTTON_PADDLE3:
		return NKCODE_BUTTON_14;
	case SDL_CONTROLLER_BUTTON_PADDLE4:
		return NKCODE_BUTTON_15;
#endif
#if SDL_VERSION_ATLEAST(2, 0, 14)
	case SDL_CONTROLLER_BUTTON_TOUCHPAD:
		return NKCODE_BUTTON_16;
#endif
	case SDL_CONTROLLER_BUTTON_INVALID:
	default:
		return NKCODE_UNKNOWN;
	}
}

void SDLJoystick::ProcessInput(const SDL_Event &event){
	switch (event.type) {
	case SDL_CONTROLLERBUTTONDOWN:
		if (event.cbutton.state == SDL_PRESSED) {
			auto code = getKeycodeForButton((SDL_GameControllerButton)event.cbutton.button);
			if (code != NKCODE_UNKNOWN) {
				KeyInput key;
				key.flags = KEY_DOWN;
				key.keyCode = code;
				key.deviceId = DEVICE_ID_PAD_0 + getDeviceIndex(event.cbutton.which);
				NativeKey(key);
			}
		}
		break;
	case SDL_CONTROLLERBUTTONUP:
		if (event.cbutton.state == SDL_RELEASED) {
			auto code = getKeycodeForButton((SDL_GameControllerButton)event.cbutton.button);
			if (code != NKCODE_UNKNOWN) {
				KeyInput key;
				key.flags = KEY_UP;
				key.keyCode = code;
				key.deviceId = DEVICE_ID_PAD_0 + getDeviceIndex(event.cbutton.which);
				NativeKey(key);
			}
		}
		break;
	case SDL_CONTROLLERAXISMOTION:
	{
		InputDeviceID deviceId = DEVICE_ID_PAD_0 + getDeviceIndex(event.caxis.which);
		// TODO: Can we really cast axis IDs like that? Do they match?
		InputAxis axisId = (InputAxis)event.caxis.axis;
		float value = event.caxis.value * (1.f / 32767.f);
		if (value > 1.0f) value = 1.0f;
		if (value < -1.0f) value = -1.0f;
		// Filter duplicate axis values.
		auto key = std::pair<InputDeviceID, InputAxis>(deviceId, axisId);
		auto iter = prevAxisValue_.find(key);
		if (iter == prevAxisValue_.end()) {
			prevAxisValue_[key] = value;
		} else if (iter->second != value) {
			iter->second = value;
			AxisInput axis;
			axis.axisId = axisId;
			axis.value = value;
			axis.deviceId = deviceId;
			NativeAxis(&axis, 1);
		}  // else ignore event.
		break;
	}
	case SDL_CONTROLLERDEVICEREMOVED:
		// for removal events, "which" is the instance ID for SDL_CONTROLLERDEVICEREMOVED
		for (auto it = controllers.begin(); it != controllers.end(); ++it) {
			if (SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(*it)) == event.cdevice.which) {
				SDL_GameControllerClose(*it);
				controllers.erase(it);
				break;
			}
		}
		break;
	case SDL_CONTROLLERDEVICEADDED:
		// for add events, "which" is the device index!
		int prevNumControllers = controllers.size();
		setUpController(event.cdevice.which);
		if (prevNumControllers == 0 && controllers.size() > 0) {
			cout << "pad 1 has been assigned to control pad: " << SDL_GameControllerName(controllers.front()) << endl;
		}
		break;
	}
}

int SDLJoystick::getDeviceIndex(int instanceId) {
	auto it = controllerDeviceMap.find(instanceId);
	if (it == controllerDeviceMap.end()) {
			// could not find device
			return -1;
	}
	return it->second;
}
