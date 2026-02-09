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

#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "Common/File/Path.h"
#include "Common/Serialize/Serializer.h"

class ParamSFOData;
#undef Process

namespace SaveState {
	enum class Status {
		FAILURE,
		WARNING,
		SUCCESS,
	};
	typedef std::function<void(Status status, std::string_view message)> Callback;

	static const char * const SCREENSHOT_EXTENSION = "jpg";

	void Init();
	void Shutdown();

	// Cycle through the 5 savestate slots
	void PrevSlot();
	void NextSlot();

	std::string GetGamePrefix(const ParamSFOData &paramSFO);

	// Run the various actions directly.
	void SaveSlot(std::string_view gamePrefix, int slot, Callback callback);
	void LoadSlot(std::string_view gamePrefix, int slot, Callback callback);
	bool UndoSaveSlot(std::string_view gamePrefix, int slot);
	bool UndoLastSave(std::string_view gamePrefix);
	bool UndoLoad(std::string_view gamePrefix, Callback callback);
	void DeleteSlot(std::string_view gamePrefix, int slot);

	// This will rescan the save state directory for files associated with the specified game.
	// Note that when we have many save states, this is really important on Android.
	void Rescan(std::string_view gamePrefix);

	// Checks whether there's an existing save in the specified slot.
	bool HasSaveInSlot(std::string_view gamePrefix, int slot);
	bool HasUndoSaveInSlot(std::string_view gamePrefix, int slot);
	bool HasUndoLastSave(std::string_view gamePrefix);
	bool HasUndoLoad(std::string_view gamePrefix);
	bool HasScreenshotInSlot(std::string_view gamePrefix, int slot);

	// Just returns the current slot from config.
	int GetCurrentSlot();

	// Returns -1 if there's no oldest/newest slot.
	int GetNewestSlot(std::string_view gamePrefix);
	int GetOldestSlot(std::string_view gamePrefix);
	
	std::string GetSlotDateAsString(std::string_view gamePrefix, int slot);
	Path GenerateSaveSlotPath(std::string_view gamePrefix, int slot, const char *extension);

	std::string GetTitle(const Path &filename);

	// Load the specified file into the current state (async.)
	// Warning: callback will be called on a different thread.
	void Load(const Path &filename, int slot, Callback callback = Callback());

	// Save the current state to the specified file (async.)
	// Warning: callback will be called on a different thread.
	void Save(const Path &filename, int slot, Callback callback = Callback());

	CChunkFileReader::Error SaveToRam(std::vector<u8> &state);
	CChunkFileReader::Error LoadFromRam(std::vector<u8> &state, std::string *errorString);

	// For testing / automated tests.  Runs a save state verification pass (async.)
	// Warning: callback will be called on a different thread.
	void Verify(Callback callback = Callback());

	// To go back to a previous snapshot (only if enabled.)
	// Warning: callback will be called on a different thread.
	void Rewind(Callback callback = Callback());

	// Returns true if there are rewind snapshots available.
	bool CanRewind();

	// Returns true if a savestate has been used during this session.
	bool HasLoadedState();

	// Returns true if the state has been reused instead of real saves many times.
	bool IsStale();

	// Returns true if state is from an older PPSSPP version.
	bool IsOldVersion();

	// Check if there's any save stating needing to be done.  Normally called once per frame.
	void Process();

	// Separate function to just process screenshots, as they need to be done at a specific time in a frame.
	// Returns true if a screenshot was taken.
	bool ProcessScreenshot(bool skipBufferEffects);

	// Notify save state code that new save data has been written.
	void NotifySaveData();

	// Checks whether a bad load required the caller to trigger a restart (and if returns true, resets the flag internally).
	bool PollRestartNeeded();

	// Returns the time since last save. -1 if N/A.
	double SecondsSinceLastSavestate();
}  // namespace SaveState
