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

#include "../Core/Host.h"
#include "InputDevice.h"
#include <list>
#include <memory>

class WindowsHost : public Host
{
public:
	WindowsHost(HWND mainWindow, HWND displayWindow)
	{
		mainWindow_ = mainWindow;
		displayWindow_ = displayWindow;
		input = getInputDevices();
		loadedSymbolMap_ = false;
	}
	void UpdateMemView();
	void UpdateDisassembly();
	void UpdateUI();
	void SetDebugMode(bool mode);

	void AddSymbol(std::string name, u32 addr, u32 size, int type);

	bool InitGL(std::string *error_message);
	void PollControllers(InputState &input_state);
	void ShutdownGL();

	void InitSound(PMixer *mixer);
	void UpdateSound();
	void ShutdownSound();

	bool IsDebuggingEnabled();
	void BootDone();
	bool AttemptLoadSymbolMap();
	void SaveSymbolMap();
	void SetWindowTitle(const char *message);

private:
	HWND displayWindow_;
	HWND mainWindow_;
	std::list<std::shared_ptr<InputDevice>> input;
	bool loadedSymbolMap_;
};