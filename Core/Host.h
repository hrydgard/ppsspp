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

#pragma once

#include <string>
#include "Common/CommonTypes.h"

struct InputState;

class Host {
public:
	virtual ~Host() {}
	//virtual void StartThread()
	virtual void UpdateUI() {}

	virtual void UpdateMemView() {}
	virtual void UpdateDisassembly() {}

	virtual void SetDebugMode(bool mode) { }

	virtual bool InitGraphics(std::string *error_string) = 0;
	virtual void ShutdownGraphics() = 0;

	virtual void InitSound() = 0;
	virtual void UpdateSound() {}
	virtual void UpdateScreen() {}
	virtual void GoFullscreen(bool) {}
	virtual void ShutdownSound() = 0;
	virtual void PollControllers(InputState &input_state) {}
	virtual void ToggleDebugConsoleVisibility() {}

	//this is sent from EMU thread! Make sure that Host handles it properly!
	virtual void BootDone() {}

	virtual bool IsDebuggingEnabled() {return true;}
	virtual bool AttemptLoadSymbolMap();
	virtual void SaveSymbolMap() {}
	virtual void SetWindowTitle(const char *message) {}

	virtual void SendCoreWait(bool) {}

	// While debugging is active, it's perfectly fine for these to block.
	virtual bool GPUDebuggingActive() { return false; }
	virtual void GPUNotifyCommand(u32 pc) {}
	virtual void GPUNotifyDisplay(u32 framebuf, u32 stride, int format) {}
	virtual void GPUNotifyDraw() {}
	virtual void GPUNotifyTextureAttachment(u32 addr) {}

	virtual bool CanCreateShortcut() {return false;}
	virtual bool CreateDesktopShortcut(std::string argumentPath, std::string title) {return false;}

	// Used for headless.
	virtual bool ShouldSkipUI() { return false; }
	virtual void SendDebugOutput(const std::string &output) {}
	virtual void SendDebugScreenshot(const u8 *pixbuf, u32 w, u32 h) {}
};

extern Host *host;
