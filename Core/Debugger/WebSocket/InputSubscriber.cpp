// Copyright (c) 2021- PPSSPP Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official git repository and contact information can be found at
// https://github.com/hrydgard/ppsspp and http://www.ppsspp.org/.

#include <algorithm>
#include <unordered_map>
#include "Common/StringUtils.h"
#include "Core/Debugger/WebSocket/InputSubscriber.h"
#include "Core/Debugger/WebSocket/WebSocketUtils.h"
#include "Core/HLE/sceCtrl.h"
#include "Core/HW/Display.h"

// This is also used in InputBroadcaster.
const std::unordered_map<std::string, uint32_t> buttonLookup = {
	{ "cross", CTRL_CROSS },
	{ "circle", CTRL_CIRCLE },
	{ "triangle", CTRL_TRIANGLE },
	{ "square", CTRL_SQUARE },
	{ "up", CTRL_UP },
	{ "down", CTRL_DOWN },
	{ "left", CTRL_LEFT },
	{ "right", CTRL_RIGHT },
	{ "start", CTRL_START },
	{ "select", CTRL_SELECT },
	{ "home", CTRL_HOME },
	{ "screen", CTRL_SCREEN },
	{ "note", CTRL_NOTE },
	{ "ltrigger", CTRL_LTRIGGER },
	{ "rtrigger", CTRL_RTRIGGER },
	{ "hold", CTRL_HOLD },
	{ "wlan", CTRL_WLAN },
	{ "remote_hold", CTRL_REMOTE_HOLD },
	{ "vol_up", CTRL_VOL_UP },
	{ "vol_down", CTRL_VOL_DOWN },
	{ "disc", CTRL_DISC },
	{ "memstick", CTRL_MEMSTICK },
	{ "forward", CTRL_FORWARD },
	{ "back", CTRL_BACK },
	{ "playpause", CTRL_PLAYPAUSE },
	// Obscure unmapped keys, see issue #17464
	{ "l2", CTRL_L2 },
	{ "l3", CTRL_L3 },
	{ "r2", CTRL_R2 },
	{ "r3", CTRL_R3 },
};

struct WebSocketInputState : public DebuggerSubscriber {
	void ButtonsSend(DebuggerRequest &req);
	void ButtonsPress(DebuggerRequest &req);
	void AnalogSend(DebuggerRequest &req);

	void Broadcast(net::WebSocketServer *ws) override;

protected:
	struct PressInfo {
		std::string ticket;
		uint32_t button;
		uint32_t duration;

		std::string Event();
	};

	std::vector<PressInfo> pressTickets_;
	int lastCounter_ = -1;
};

std::string WebSocketInputState::PressInfo::Event() {
	JsonWriter j;
	j.begin();
	j.writeString("event", "input.buttons.press");
	if (!ticket.empty()) {
		j.writeRaw("ticket", ticket);
	}
	j.end();
	return j.str();
}

const std::unordered_map<std::string, uint32_t> &WebSocketInputButtonLookup() {
	return buttonLookup;
}

DebuggerSubscriber *WebSocketInputInit(DebuggerEventHandlerMap &map) {
	auto p = new WebSocketInputState();
	map["input.buttons.send"] = std::bind(&WebSocketInputState::ButtonsSend, p, std::placeholders::_1);
	map["input.buttons.press"] = std::bind(&WebSocketInputState::ButtonsPress, p, std::placeholders::_1);
	map["input.analog.send"] = std::bind(&WebSocketInputState::AnalogSend, p, std::placeholders::_1);

	return p;
}

// Alter PSP button press flags (input.buttons.send)
//
// Parameters:
//  - buttons: object containing button names as string keys, boolean press state as value.
//
// Button names (some are not respected by PPSSPP):
//  - cross: button on bottom side of right pad.
//  - circle: button on right side of right pad.
//  - triangle: button on top side of right pad.
//  - square: button on left side of right pad.
//  - up: d-pad up button.
//  - down: d-pad down button.
//  - left: d-pad left button.
//  - right: d-pad right button.
//  - start: rightmost button at bottom of device.
//  - select: second to the right at bottom of device.
//  - home: leftmost button at bottom of device.
//  - screen: brightness control button at bottom of device.
//  - note: mute control button at bottom of device.
//  - ltrigger: left shoulder trigger button.
//  - rtrigger: right shoulder trigger button.
//  - hold: hold setting of power switch.
//  - wlan: wireless networking switch.
//  - remote_hold: hold switch on headset.
//  - vol_up: volume up button next to home at bottom of device.
//  - vol_down: volume down button next to home at bottom of device.
//  - disc: UMD disc sensor.
//  - memstick: memory stick sensor.
//  - forward: forward button on headset.
//  - back: back button on headset.
//  - playpause: play/pause button on headset.
//
// Response (same event name) with no extra data.
void WebSocketInputState::ButtonsSend(DebuggerRequest &req) {
	const JsonNode *jsonButtons = req.data.get("buttons");
	if (!jsonButtons) {
		return req.Fail("Missing 'buttons' parameter");
	}
	if (jsonButtons->value.getTag() != JSON_OBJECT) {
		return req.Fail("Invalid 'buttons' parameter type");
	}

	uint32_t downFlags = 0;
	uint32_t upFlags = 0;

	for (const JsonNode *button : jsonButtons->value) {
		auto info = buttonLookup.find(button->key);
		if (info == buttonLookup.end()) {
			return req.Fail(StringFromFormat("Unsupported 'buttons' object key '%s'", button->key));
		}
		if (button->value.getTag() == JSON_TRUE) {
			downFlags |= info->second;
		} else if (button->value.getTag() == JSON_FALSE) {
			upFlags |= info->second;
		} else if (button->value.getTag() != JSON_NULL) {
			return req.Fail(StringFromFormat("Unsupported 'buttons' object type for key '%s'", button->key));
		}
	}

	__CtrlUpdateButtons(downFlags, upFlags);

	req.Respond();
}

// Press and release a button (input.buttons.press)
//
// Parameters:
//  - button: required string indicating button name (see input.buttons.send.)
//  - duration: optional integer indicating frames to press for, defaults to 1.
//
// Response (same event name) with no extra data once released.
void WebSocketInputState::ButtonsPress(DebuggerRequest &req) {
	std::string button;
	if (!req.ParamString("button", &button))
		return;

	PressInfo press;
	press.duration = 1;
	if (!req.ParamU32("duration", &press.duration, false, DebuggerParamType::OPTIONAL))
		return;
	if (press.duration < 0)
		return req.Fail("Parameter 'duration' must not be negative");
	const JsonNode *value = req.data.get("ticket");
	press.ticket = value ? json_stringify(value) : "";

	auto info = buttonLookup.find(button);
	if (info == buttonLookup.end()) {
		return req.Fail(StringFromFormat("Unsupported button value '%s'", button.c_str()));
	}
	press.button = info->second;

	__CtrlUpdateButtons(press.button, 0);
	pressTickets_.push_back(press);
}

void WebSocketInputState::Broadcast(net::WebSocketServer *ws) {
	int counter = __DisplayGetNumVblanks();
	if (pressTickets_.empty() || lastCounter_ == counter)
		return;
	lastCounter_ = counter;

	for (PressInfo &press : pressTickets_) {
		press.duration--;
		if (press.duration == -1) {
			__CtrlUpdateButtons(0, press.button);
			ws->Send(press.Event());
		}
	}
	auto negative = [](const PressInfo &press) -> bool {
		return press.duration < 0;
	};
	pressTickets_.erase(std::remove_if(pressTickets_.begin(), pressTickets_.end(), negative), pressTickets_.end());
}

static bool AnalogValue(DebuggerRequest &req, float *value, const char *name) {
	const JsonNode *node = req.data.get(name);
	if (!node) {
		req.Fail(StringFromFormat("Missing '%s' parameter", name));
		return false;
	}
	if (node->value.getTag() != JSON_NUMBER) {
		req.Fail(StringFromFormat("Invalid '%s' parameter type", name));
		return false;
	}

	double val = node->value.toNumber();
	if (val < -1.0 || val > 1.0) {
		req.Fail(StringFromFormat("Parameter '%s' must be between -1.0 and 1.0", name));
		return false;
	}

	*value = (float)val;
	return true;
}

// Set coordinates of analog stick (input.analog.send)
//
// Parameters:
//  - x: required number from -1.0 to 1.0.
//  - y: required number from -1.0 to 1.0.
//  - stick: optional string, either "left" (default) or "right".
//
// Response (same event name) with no extra data.
void WebSocketInputState::AnalogSend(DebuggerRequest &req) {
	std::string stick = "left";
	if (!req.ParamString("stick", &stick, DebuggerParamType::OPTIONAL))
		return;
	if (stick != "left" && stick != "right")
		return req.Fail(StringFromFormat("Parameter 'stick' must be 'left' or 'right', not '%s'", stick.c_str()));
	float x, y;
	if (!AnalogValue(req, &x, "x") || !AnalogValue(req, &y, "y"))
		return;

	// TODO: Route into the control mapper's PSPKey function or similar instead.
	__CtrlSetAnalogXY(stick == "left" ? CTRL_STICK_LEFT : CTRL_STICK_RIGHT, x, y);

	req.Respond();
}
