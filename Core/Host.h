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

#include "../Globals.h"

struct InputState;

class PMixer
{
public:
	PMixer() {}
	virtual ~PMixer() {}
	virtual int Mix(short *stereoout, int numSamples) {memset(stereoout,0,numSamples*2*sizeof(short)); return numSamples;}
};

class Host
{
public:
	virtual ~Host() {}
	//virtual void StartThread()
	virtual void UpdateUI() {}

	virtual void UpdateMemView() {}
	virtual void UpdateDisassembly() {}

	virtual void SetDebugMode(bool mode) { }

	virtual bool InitGL(std::string *error_string) = 0;
	virtual void ShutdownGL() = 0;

	virtual void InitSound(PMixer *mixer) = 0;
	virtual void UpdateSound() {}
	virtual void ShutdownSound() = 0;
	virtual void PollControllers(InputState &input_state) {}

	//this is sent from EMU thread! Make sure that Host handles it properly!
	virtual void BootDone() {}

	virtual bool IsDebuggingEnabled() {return true;}
	virtual bool AttemptLoadSymbolMap() {return false;}
	virtual void SaveSymbolMap() {}
	virtual void SetWindowTitle(const char *message) {}

	virtual void SendCoreWait(bool) {}

	virtual bool GpuStep() { return false; }
	virtual void SendGPUStart() {}
	virtual void SendGPUWait(u32 cmd, u32 addr, void* data) {}
	virtual void SetGPUStep(bool value, int flag = 0, int data = 0) {}
	virtual void NextGPUStep() {}

	// Used for headless.
	virtual void SendDebugOutput(const std::string &output) {}
	virtual void SendDebugScreenshot(const u8 *pixbuf, u32 w, u32 h) {}
};

extern Host *host;
