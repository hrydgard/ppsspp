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
#include "Common/CommonWindows.h"
#include <list>
#include <memory>

extern float g_mouseDeltaX;
extern float g_mouseDeltaY;

class GraphicsContext;

class WindowsHost : public Host {
public:
	WindowsHost(HINSTANCE hInstance, HWND mainWindow, HWND displayWindow);

	~WindowsHost() {
		UpdateConsolePosition();
	}

	void UpdateMemView() override;
	void UpdateDisassembly() override;
	void UpdateUI() override;
	void SetDebugMode(bool mode) override;

	// If returns false, will return a null context
	bool InitGraphics(std::string *error_message, GraphicsContext **ctx) override;
	void PollControllers() override;
	void ShutdownGraphics() override;

	void InitSound() override;
	void UpdateSound() override;
	void ShutdownSound() override;

	bool IsDebuggingEnabled() override;
	void BootDone() override;
	bool AttemptLoadSymbolMap() override;
	void SaveSymbolMap() override;
	void SetWindowTitle(const char *message) override;

	void ToggleDebugConsoleVisibility() override;

	bool CanCreateShortcut() override;
	bool CreateDesktopShortcut(std::string argumentPath, std::string title) override;

	void NotifyUserMessage(const std::string &message, float duration = 1.0f, u32 color = 0x00FFFFFF, const char *id = nullptr) override;
	void SendUIMessage(const std::string &message, const std::string &value) override;

	std::shared_ptr<KeyboardDevice> keyboard;

	GraphicsContext *GetGraphicsContext() { return gfx_; }

private:
	void SetConsolePosition();
	void UpdateConsolePosition();

	HINSTANCE hInstance_;
	HWND displayWindow_;
	HWND mainWindow_;
	GraphicsContext *gfx_ = nullptr;
	size_t numDinputDevices_ = 0;

	std::list<std::shared_ptr<InputDevice>> input;
};
