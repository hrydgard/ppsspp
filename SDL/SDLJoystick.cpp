#include <string>

#include "Common/File/FileUtil.h"
#include "Common/File/VFS/VFS.h"
#include "Common/StringUtils.h"
#include "Common/System/NativeApp.h"
#include "Common/System/System.h"
#include "Common/Log.h"

#include "Core/Config.h"
#include "Core/KeyMap.h"
#include "SDL/SDLJoystick.h"

using namespace std;

static bool SDLJoystickEventHandlerWrapper(void* userdata, SDL_Event* event) {
	static_cast<SDLJoystick *>(userdata)->ProcessInput(*event);
	return true;
}

SDLJoystick::SDLJoystick(bool init_SDL ) : registeredAsEventHandler(false) {
	SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
	if (init_SDL) {
		SDL_Init(SDL_INIT_JOYSTICK | SDL_INIT_VIDEO | SDL_INIT_GAMEPAD);
	}

	const char *dbPath = "gamecontrollerdb.txt";
	INFO_LOG(Log::System, "loading control pad mappings from %s:", dbPath);

	size_t size;
	u8 *mappingData = g_VFS.ReadFile(dbPath, &size);
	if (mappingData) {
		SDL_IOStream *io = SDL_IOFromConstMem(mappingData, size);
		if (SDL_AddGamepadMappingsFromIO(io, true) == -1) {
			ERROR_LOG(Log::System, "Failed to read mapping data - corrupt?");
		}
		delete[] mappingData;
	} else {
		WARN_LOG(Log::System, "gamecontrollerdb.txt missing?");
	}
	setUpControllers();
}

void SDLJoystick::setUpControllers() {
	int numjoys = 0;
	SDL_JoystickID *joysticks = SDL_GetJoysticks(&numjoys);
	for (int i = 0; joysticks && i < numjoys; i++) {
		setUpController(joysticks[i]);
	}
	if (joysticks) {
		SDL_free(joysticks);
	}
	if (controllers.size() > 0) {
		INFO_LOG(Log::System, "pad 1 has been assigned to control pad: %s", SDL_GetGamepadName(controllers.front()));
	}
}

void SDLJoystick::setUpController(SDL_JoystickID deviceID) {
	static constexpr int cbGUID = 33;
	char pszGUID[cbGUID];

	if (!SDL_IsGamepad(deviceID)) {
		WARN_LOG(Log::System, "Control pad device %d not supported by SDL game controller database, attempting to create default mapping...", deviceID);
		SDL_Joystick *joystick = SDL_OpenJoystick(deviceID);
		if (joystick) {
			SDL_GUIDToString(SDL_GetJoystickGUID(joystick), pszGUID, cbGUID);
			// create default mapping - this is the PS3 dual shock mapping
			const std::string safeName = ReplaceAll(SDL_GetJoystickName(joystick), ",", "");
			std::string mapping = std::string(pszGUID) + "," + safeName + ",x:b3,a:b0,b:b1,y:b2,back:b8,guide:b10,start:b9,dpleft:b15,dpdown:b14,dpright:b16,dpup:b13,leftshoulder:b4,lefttrigger:a2,rightshoulder:b6,rightshoulder:b5,righttrigger:a5,leftstick:b7,leftstick:b11,rightstick:b12,leftx:a0,lefty:a1,rightx:a3,righty:a4";
			if (SDL_AddGamepadMapping(mapping.c_str()) == 1){
				INFO_LOG(Log::System, "Added default mapping ok");
			} else {
				ERROR_LOG(Log::System, "Failed to add default mapping");
			}
			SDL_CloseJoystick(joystick);
		} else {
			ERROR_LOG(Log::System, "Failed to get joystick identifier. Read-only device? Control pad device %d", deviceID);
		}
	} else {
		SDL_Joystick *joystick = SDL_OpenJoystick(deviceID);
		if (joystick) {
			SDL_GUIDToString(SDL_GetJoystickGUID(joystick), pszGUID, cbGUID);
			SDL_CloseJoystick(joystick);
		}
	}
	SDL_Gamepad *controller = SDL_OpenGamepad(deviceID);
	if (controller) {
		if (SDL_GamepadConnected(controller)) {
			controllers.push_back(controller);
			controllerDeviceMap[SDL_GetJoystickID(SDL_GetGamepadJoystick(controller))] = deviceID;
			INFO_LOG(Log::System, "found control pad: %s, loading mapping", SDL_GetGamepadName(controller));
			// NOTE: The case to InputDeviceID here is wrong, we should do some kind of lookup.
			KeyMap::NotifyPadConnected((InputDeviceID)deviceID, std::string(pszGUID) + ": " + SDL_GetGamepadName(controller));
			char *mapping = SDL_GetGamepadMapping(controller);
			if (!mapping) {
				WARN_LOG(Log::System, "Could not find mapping in SDL2 controller database");
			} else {
				INFO_LOG(Log::System, "SUCCESS, mapping is: %s", mapping);
				SDL_free(mapping);
			}
		} else {
			SDL_CloseGamepad(controller);
		}
	}
}

SDLJoystick::~SDLJoystick() {
	if (registeredAsEventHandler) {
		SDL_RemoveEventWatch(SDLJoystickEventHandlerWrapper, this);
	}
	for (auto & controller : controllers) {
		SDL_CloseGamepad(controller);
	}
}

void SDLJoystick::registerEventHandler() {
	SDL_AddEventWatch(SDLJoystickEventHandlerWrapper, this);
	registeredAsEventHandler = true;
}

InputKeyCode SDLJoystick::getKeycodeForButton(SDL_GamepadButton button) {
	switch (button) {
	case SDL_GAMEPAD_BUTTON_DPAD_UP:
		return NKCODE_DPAD_UP;
	case SDL_GAMEPAD_BUTTON_DPAD_DOWN:
		return NKCODE_DPAD_DOWN;
	case SDL_GAMEPAD_BUTTON_DPAD_LEFT:
		return NKCODE_DPAD_LEFT;
	case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:
		return NKCODE_DPAD_RIGHT;
	case SDL_GAMEPAD_BUTTON_SOUTH:
		return NKCODE_BUTTON_2;
	case SDL_GAMEPAD_BUTTON_EAST:
		return NKCODE_BUTTON_3;
	case SDL_GAMEPAD_BUTTON_WEST:
		return NKCODE_BUTTON_4;
	case SDL_GAMEPAD_BUTTON_NORTH:
		return NKCODE_BUTTON_1;
	case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER:
		return NKCODE_BUTTON_5;
	case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER:
		return NKCODE_BUTTON_6;
	case SDL_GAMEPAD_BUTTON_START:
		return NKCODE_BUTTON_10;
	case SDL_GAMEPAD_BUTTON_BACK:
		return NKCODE_BUTTON_9; // select button
	case SDL_GAMEPAD_BUTTON_GUIDE:
		return NKCODE_BACK; // pause menu
	case SDL_GAMEPAD_BUTTON_LEFT_STICK:
		return NKCODE_BUTTON_THUMBL;
	case SDL_GAMEPAD_BUTTON_RIGHT_STICK:
		return NKCODE_BUTTON_THUMBR;
	case SDL_GAMEPAD_BUTTON_MISC1:
		return NKCODE_BUTTON_11;
	case SDL_GAMEPAD_BUTTON_RIGHT_PADDLE1:
		return NKCODE_BUTTON_12;
	case SDL_GAMEPAD_BUTTON_RIGHT_PADDLE2:
		return NKCODE_BUTTON_13;
	case SDL_GAMEPAD_BUTTON_LEFT_PADDLE1:
		return NKCODE_BUTTON_14;
	case SDL_GAMEPAD_BUTTON_LEFT_PADDLE2:
		return NKCODE_BUTTON_15;
	case SDL_GAMEPAD_BUTTON_TOUCHPAD:
		return NKCODE_BUTTON_16;
	case SDL_GAMEPAD_BUTTON_INVALID:
	default:
		return NKCODE_UNKNOWN;
	}
}

void SDLJoystick::ProcessInput(const SDL_Event &event){
	switch (event.type) {
	case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
	{
		auto code = getKeycodeForButton((SDL_GamepadButton)event.gbutton.button);
		if (code != NKCODE_UNKNOWN) {
			KeyInput key;
			key.flags = KeyInputFlags::DOWN;
			key.keyCode = code;
			key.deviceId = DEVICE_ID_PAD_0 + getDeviceIndex(event.gbutton.which);
			NativeKey(key);
		}
		break;
	}
	case SDL_EVENT_GAMEPAD_BUTTON_UP:
	{
		auto code = getKeycodeForButton((SDL_GamepadButton)event.gbutton.button);
		if (code != NKCODE_UNKNOWN) {
			KeyInput key;
			key.flags = KeyInputFlags::UP;
			key.keyCode = code;
			key.deviceId = DEVICE_ID_PAD_0 + getDeviceIndex(event.gbutton.which);
			NativeKey(key);
		}
		break;
	}
	case SDL_EVENT_GAMEPAD_AXIS_MOTION:
	{
		InputDeviceID deviceId = DEVICE_ID_PAD_0 + getDeviceIndex(event.gaxis.which);
		InputAxis axisId = (InputAxis)event.gaxis.axis;
		float value = event.gaxis.value * (1.f / 32767.f);
		if (value > 1.0f) value = 1.0f;
		if (value < -1.0f) value = -1.0f;
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
		}
		break;
	}
	case SDL_EVENT_GAMEPAD_REMOVED:
		for (auto it = controllers.begin(); it != controllers.end(); ++it) {
			if (SDL_GetJoystickID(SDL_GetGamepadJoystick(*it)) == event.gdevice.which) {
				SDL_CloseGamepad(*it);
				controllerDeviceMap.erase(event.gdevice.which);
				controllers.erase(it);
				break;
			}
		}
		break;
	case SDL_EVENT_GAMEPAD_ADDED:
	{
		int prevNumControllers = controllers.size();
		setUpController(event.gdevice.which);
		if (prevNumControllers == 0 && controllers.size() > 0) {
			INFO_LOG(Log::System, "pad 1 has been assigned to control pad: %s", SDL_GetGamepadName(controllers.front()));
		}
		break;
	}
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
