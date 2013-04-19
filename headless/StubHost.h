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

#include "../Core/Host.h"

#define HEADLESSHOST_CLASS HeadlessHost

// TODO: Get rid of this junk
class HeadlessHost : public Host
{
public:
	// virtual void StartThread()
	virtual void UpdateUI() {}

	virtual void UpdateMemView() {}
	virtual void UpdateDisassembly() {}

	virtual void SetDebugMode(bool mode) { }

	virtual bool InitGL(std::string *error_message) {return false;}
	virtual void ShutdownGL() {}

	virtual void InitSound(PMixer *mixer) {}
	virtual void UpdateSound() {}
	virtual void ShutdownSound() {}

	// this is sent from EMU thread! Make sure that Host handles it properly
	virtual void BootDone() {}

	virtual bool IsDebuggingEnabled() {return false;}
	virtual bool AttemptLoadSymbolMap() {return false;}

	virtual void SendDebugOutput(const std::string &output) { printf("%s", output.c_str()); }
	virtual void SetComparisonScreenshot(const std::string &filename) {}


	// Unique for HeadlessHost
	virtual void SwapBuffers() {}

};