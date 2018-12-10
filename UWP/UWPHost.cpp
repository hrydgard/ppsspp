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

#include "file/file_util.h"
#include "base/NativeApp.h"
#include "input/input_state.h"

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
	input.push_back(std::shared_ptr<InputDevice>(new XinputDevice()));
}

UWPHost::~UWPHost() {

}

void UWPHost::SetConsolePosition() {
}

void UWPHost::UpdateConsolePosition() {
}

bool UWPHost::InitGraphics(std::string *error_message, GraphicsContext **ctx) {
	// Done elsewhere
	return true;
}

void UWPHost::ShutdownGraphics() {
	// Done elsewhere
}

void UWPHost::SetWindowTitle(const char *message) {
	// Should really be done differently
}

void UWPHost::InitSound() {
}

void UWPHost::UpdateSound() {
}

void UWPHost::ShutdownSound() {
}

void UWPHost::UpdateUI() {
}

void UWPHost::UpdateMemView() {
}

void UWPHost::UpdateDisassembly() {
}

void UWPHost::SetDebugMode(bool mode) {
}

void UWPHost::PollControllers() {
	bool doPad = true;
	for (auto iter = this->input.begin(); iter != this->input.end(); iter++)
	{
		auto device = *iter;
		if (!doPad && device->IsPad())
			continue;
		if (device->UpdateState() == InputDevice::UPDATESTATE_SKIP_PAD)
			doPad = false;
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

void UWPHost::BootDone() {
	g_symbolMap->SortSymbols();

	SetDebugMode(false);
	Core_EnableStepping(false);
}

static std::string SymbolMapFilename(const char *currentFilename, char* ext) {
	FileInfo info;

	std::string result = currentFilename;

	// can't fail, definitely exists if it gets this far
	getFileInfo(currentFilename, &info);
	if (info.isDirectory) {
#ifdef _WIN32
		char* slash = "\\";
#else
		char* slash = "/";
#endif
		if (!endsWith(result, slash))
			result += slash;

		return result + ".ppsspp-symbols" + ext;
	} else {
		size_t dot = result.rfind('.');
		if (dot == result.npos)
			return result + ext;

		result.replace(dot, result.npos, ext);
		return result;
	}
}

bool UWPHost::AttemptLoadSymbolMap() {
	bool result1 = g_symbolMap->LoadSymbolMap(SymbolMapFilename(PSP_CoreParameter().fileToStart.c_str(), ".ppmap").c_str());
	// Load the old-style map file.
	if (!result1)
		result1 = g_symbolMap->LoadSymbolMap(SymbolMapFilename(PSP_CoreParameter().fileToStart.c_str(), ".map").c_str());
	bool result2 = g_symbolMap->LoadNocashSym(SymbolMapFilename(PSP_CoreParameter().fileToStart.c_str(), ".sym").c_str());
	return result1 || result2;
}

void UWPHost::SaveSymbolMap() {
	g_symbolMap->SaveSymbolMap(SymbolMapFilename(PSP_CoreParameter().fileToStart.c_str(), ".ppmap").c_str());
}

bool UWPHost::IsDebuggingEnabled() {
	return false;
}

bool UWPHost::CanCreateShortcut() {
	return false;  // Turn on when below function fixed
}

bool UWPHost::CreateDesktopShortcut(std::string argumentPath, std::string gameTitle) {
	// TODO: not working correctly
	return false;
}

void UWPHost::ToggleDebugConsoleVisibility() {
	// N/A
}

void UWPHost::NotifyUserMessage(const std::string &message, float duration, u32 color, const char *id) {
	osm.Show(message, duration, color, -1, true, id);
}
