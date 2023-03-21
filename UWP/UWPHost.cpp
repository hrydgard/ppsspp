// Copyright (c) 2012- PPSSPP Project.

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

#include "ppsspp_config.h"

#include <algorithm>

#include "Common/File/DirListing.h"
#include "Common/System/NativeApp.h"
#include "Common/System/System.h"
#include "Common/Input/InputState.h"

#include "Common/StringUtils.h"
#include "Core/Core.h"
#include "Core/Config.h"
#include "Core/CoreParameter.h"
#include "Core/System.h"
#include "Core/Debugger/SymbolMap.h"

#include "UI/OnScreenDisplay.h"

#include "Windows/XinputDevice.h"

#include "UWP/XAudioSoundStream.h"
#include "UWP/UWPHost.h"

UWPHost::UWPHost() {

	// add first XInput device to respond
	input.push_back(std::make_unique<XinputDevice>());
}

UWPHost::~UWPHost() {

}

void UWPHost::SetConsolePosition() {
}

void UWPHost::UpdateConsolePosition() {
}

void UWPHost::SetWindowTitle(const char *message) {
}

void UWPHost::UpdateSound() {
}

void UWPHost::PollControllers() {
	for (const auto& device : this->input)
	{
		if (device->UpdateState() == InputDevice::UPDATESTATE_SKIP_PAD)
			break;
	}

	/*
	g_mouseDeltaX *= 0.9f;
	g_mouseDeltaY *= 0.9f;

	// TODO: Tweak!
	float scaleFactor = g_dpi_scale * 0.01f;

	float mx = std::max(-1.0f, std::min(1.0f, g_mouseDeltaX * scaleFactor));
	float my = std::max(-1.0f, std::min(1.0f, g_mouseDeltaY * scaleFactor));
	AxisInput axisX, axisY;
	axisX.axisId = JOYSTICK_AXIS_MOUSE_REL_X;
	axisX.deviceId = DEVICE_ID_MOUSE;
	axisX.value = mx;
	axisY.axisId = JOYSTICK_AXIS_MOUSE_REL_Y;
	axisY.deviceId = DEVICE_ID_MOUSE;
	axisY.value = my;
	*/
}

static Path SymbolMapFilename(const Path &currentFilename, const char *ext) {
	File::FileInfo info;
	// can't fail, definitely exists if it gets this far
	File::GetFileInfo(currentFilename, &info);
	if (info.isDirectory) {
		return currentFilename / (std::string(".ppsspp-symbols") + ext);
	}
	return currentFilename.WithReplacedExtension(ext);
}

bool UWPHost::AttemptLoadSymbolMap() {
	bool result1 = g_symbolMap->LoadSymbolMap(SymbolMapFilename(PSP_CoreParameter().fileToStart, ".ppmap"));
	// Load the old-style map file.
	if (!result1)
		result1 = g_symbolMap->LoadSymbolMap(SymbolMapFilename(PSP_CoreParameter().fileToStart, ".map"));
	bool result2 = g_symbolMap->LoadNocashSym(SymbolMapFilename(PSP_CoreParameter().fileToStart, ".sym"));
	return result1 || result2;
}

void UWPHost::SaveSymbolMap() {
	g_symbolMap->SaveSymbolMap(SymbolMapFilename(PSP_CoreParameter().fileToStart, ".ppmap"));
}

void UWPHost::ToggleDebugConsoleVisibility() {
	// N/A
}

void UWPHost::NotifyUserMessage(const std::string &message, float duration, u32 color, const char *id) {
	osm.Show(message, duration, color, -1, true, id);
}
