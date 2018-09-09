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

class GraphicsContext;

// TODO: Whittle this down. Collecting a bunch of random stuff like this isn't good design :P
class Host {
public:
	virtual ~Host() {}
	virtual void UpdateUI() {}

	virtual void UpdateMemView() {}
	virtual void UpdateDisassembly() {}

	virtual void SetDebugMode(bool mode) { }

	virtual bool InitGraphics(std::string *error_string, GraphicsContext **ctx) = 0;
	virtual void ShutdownGraphics() = 0;

	virtual void InitSound() = 0;
	virtual void UpdateSound() {}
	virtual void ShutdownSound() = 0;
	virtual void PollControllers() {}
	virtual void ToggleDebugConsoleVisibility() {}

	//this is sent from EMU thread! Make sure that Host handles it properly!
	virtual void BootDone() {}

	virtual bool IsDebuggingEnabled() {return true;}
	virtual bool AttemptLoadSymbolMap();
	virtual void SaveSymbolMap() {}
	virtual void SetWindowTitle(const char *message) {}

	virtual bool CanCreateShortcut() {return false;}
	virtual bool CreateDesktopShortcut(std::string argumentPath, std::string title) {return false;}

	virtual void NotifyUserMessage(const std::string &message, float duration = 1.0f, u32 color = 0x00FFFFFF, const char *id = nullptr) {}
	virtual void SendUIMessage(const std::string &message, const std::string &value) {}

	// Used for headless.
	virtual bool ShouldSkipUI() { return false; }
	virtual void SendDebugOutput(const std::string &output) {}
	virtual void SendDebugScreenshot(const u8 *pixbuf, u32 w, u32 h) {}
};

extern Host *host;
