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
#include "KeyboardDevice.h"
#include <list>
#include <memory>

extern float mouseDeltaX;
extern float mouseDeltaY;

class WindowsHost : public Host {
public:
	WindowsHost(HWND mainWindow, HWND displayWindow);

	~WindowsHost() {
		UpdateConsolePosition();
	}

	void UpdateMemView() override;
	void UpdateDisassembly() override;
	void UpdateUI() override;
	void UpdateScreen() override;
	void SetDebugMode(bool mode) override;

	bool InitGraphics(std::string *error_message) override;
	void PollControllers(InputState &input_state) override;
	void ShutdownGraphics() override;

	void InitSound() override;
	void UpdateSound() override;
	void ShutdownSound() override;

	bool IsDebuggingEnabled() override;
	void BootDone() override;
	bool AttemptLoadSymbolMap() override;
	void SaveSymbolMap() override;
	void SetWindowTitle(const char *message) override;

	bool GPUDebuggingActive() override;
	void GPUNotifyCommand(u32 pc) override;
	void GPUNotifyDisplay(u32 framebuf, u32 stride, int format) override;
	void GPUNotifyDraw() override;
	void GPUNotifyTextureAttachment(u32 addr) override;
	void ToggleDebugConsoleVisibility() override;

	bool CanCreateShortcut() override;
	bool CreateDesktopShortcut(std::string argumentPath, std::string title) override;

	void GoFullscreen(bool) override;

	std::shared_ptr<KeyboardDevice> keyboard;

private:
	void SetConsolePosition();
	void UpdateConsolePosition();

	HWND displayWindow_;
	HWND mainWindow_;

	std::list<std::shared_ptr<InputDevice>> input;
};
