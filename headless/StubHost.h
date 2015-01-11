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

#include "Core/Host.h"
#include "Core/Debugger/SymbolMap.h"

// TODO: Get rid of this junk
class HeadlessHost : public Host
{
public:
	// void StartThread() override
	void UpdateUI() override {}

	void UpdateMemView() override {}
	void UpdateDisassembly() override {}

	void SetDebugMode(bool mode) { }

	bool InitGraphics(std::string *error_message) override {return false;}
	void ShutdownGraphics() override {}

	void InitSound() override {}
	void UpdateSound() override {}
	void ShutdownSound() override {}

	// this is sent from EMU thread! Make sure that Host handles it properly
	void BootDone() override {}

	bool IsDebuggingEnabled() override { return false; }
	bool AttemptLoadSymbolMap() override { symbolMap.Clear(); return false; }

	bool ShouldSkipUI() override { return true; }

	void SendDebugOutput(const std::string &output) override {
		if (output.find('\n') != output.npos) {
			DoFlushDebugOutput();
			fwrite(output.data(), sizeof(char), output.length(), stdout);
		} else {
			debugOutputBuffer_ += output;
		}
	}
	virtual void FlushDebugOutput() {
		DoFlushDebugOutput();
	}
	inline void DoFlushDebugOutput() {
		if (!debugOutputBuffer_.empty()) {
			fwrite(debugOutputBuffer_.data(), sizeof(char), debugOutputBuffer_.length(), stdout);
			debugOutputBuffer_.clear();
		}
	}
	virtual void SetComparisonScreenshot(const std::string &filename) {}


	// Unique for HeadlessHost
	virtual void SwapBuffers() {}

protected:
	std::string debugOutputBuffer_;
};