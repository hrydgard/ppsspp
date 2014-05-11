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
	WindowsHost(HWND mainWindow);
	virtual ~WindowsHost() {
		UpdateConsolePosition();
	}

	void UpdateMemView();
	void UpdateDisassembly();
	void UpdateUI();
	virtual void UpdateScreen() override;
	void SetDebugMode(bool mode);

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

	virtual bool GPUDebuggingActive() override;
	virtual void GPUNotifyCommand(u32 pc) override;
	virtual void GPUNotifyDisplay(u32 framebuf, u32 stride, int format) override;
	virtual void GPUNotifyDraw() override;
	virtual void GPUNotifyTextureAttachment(u32 addr) override;
	virtual bool GPUAllowTextureCache(u32 addr) override;
	virtual void ToggleDebugConsoleVisibility() override;

	virtual bool CanCreateShortcut() override { return false; }  // Turn on when fixed
	virtual bool CreateDesktopShortcut(std::string argumentPath, std::string title) override;

	virtual void GoFullscreen(bool) override;

	std::shared_ptr<KeyboardDevice> keyboard;

private:
	void SetConsolePosition();
	void UpdateConsolePosition();

	HWND window_;

	std::list<std::shared_ptr<InputDevice>> input;
};
