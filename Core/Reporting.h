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
#include <vector>
#include <cstdint>

#include "Common/CommonTypes.h"
#include "Common/File/Path.h"
#include "Common/LogReporting.h"

class PointerWrap;

namespace Reporting
{
	// Should be called whenever a new game is loaded/shutdown to forget things.
	void Init();
	void Shutdown();

	// Check savestate compatibility, mostly needed on load.
	void DoState(PointerWrap &p);

	// Should be called whenever the game configuration changes.
	void UpdateConfig();

	// Should be called when debugging APIs are used in a way that could make the game crash.
	void NotifyDebugger();

	// Should be called for each LoadExec, with parameters of the module executed.
	void NotifyExecModule(const char *name, int ver, uint32_t crc);

	// Returns whether or not the reporting system is currently enabled.
	bool IsEnabled();

	// Returns whether the reporting system can be enabled (based on system or settings.)
	bool IsSupported();

	// Set the current enabled state of the reporting system and desired reporting server host.
	// Returns if anything was changed.
	bool Enable(bool flag, const std::string &host);

	// Use the default reporting setting (per compiled settings) of host and enabled state.
	void EnableDefault();

	// Report the compatibility of the current game / configuration.
	void ReportCompatibility(const char *compat, int graphics, int speed, int gameplay, const std::string &screenshotFilename);

	// Get the latest compatibility result.  Only valid when GetStatus() is not BUSY.
	std::vector<std::string> CompatibilitySuggestions();

	// Queues game for CRC hash if needed.
	void QueueCRC(const Path &gamePath);

	// Returns true if the hash is available, does not queue if not.
	bool HasCRC(const Path &gamePath);

	void CancelCRC();

	// Blocks until the CRC hash is available for game, and returns it.
	// To avoid stalling, call HasCRC() in update() or similar and call this if it returns true.
	uint32_t RetrieveCRC(const Path &gamePath);

	enum class ReportStatus {
		WORKING,
		BUSY,
		FAILING,
	};

	// Whether server requests appear to be working.
	ReportStatus GetStatus();

	// Return the currently active host (or blank if not active.)
	std::string ServerHost();

	// Return the current game id.
	std::string CurrentGameID();
}
