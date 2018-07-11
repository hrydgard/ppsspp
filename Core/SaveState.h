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

#include <functional>
#include <string>
#include <vector>

#include "Common/ChunkFile.h"

namespace SaveState
{
	enum class Status {
		FAILURE,
		WARNING,
		SUCCESS,
	};
	typedef std::function<void(Status status, const std::string &message, void *cbUserData)> Callback;

	static const int NUM_SLOTS = 5;
	static const char *STATE_EXTENSION = "ppst";
	static const char *SCREENSHOT_EXTENSION = "jpg";
	static const char *UNDO_STATE_EXTENSION = "undo.ppst";
	static const char *UNDO_SCREENSHOT_EXTENSION = "undo.jpg";

	void Init();
	void Shutdown();

	// Cycle through the 5 savestate slots
	void NextSlot();
	void SaveSlot(const std::string &gameFilename, int slot, Callback callback, void *cbUserData = 0);
	void LoadSlot(const std::string &gameFilename, int slot, Callback callback, void *cbUserData = 0);
	bool UndoSaveSlot(const std::string &gameFilename, int slot);
	// Checks whether there's an existing save in the specified slot.
	bool HasSaveInSlot(const std::string &gameFilename, int slot);
	bool HasUndoSaveInSlot(const std::string &gameFilename, int slot);
	bool HasScreenshotInSlot(const std::string &gameFilename, int slot);

	int GetCurrentSlot();

	// Returns -1 if there's no oldest/newest slot.
	int GetNewestSlot(const std::string &gameFilename);
	int GetOldestSlot(const std::string &gameFilename);
	
	std::string GetSlotDateAsString(const std::string &gameFilename, int slot);
	std::string GenerateSaveSlotFilename(const std::string &gameFilename, int slot, const char *extension);

	std::string GetTitle(const std::string &filename);

	// Load the specified file into the current state (async.)
	// Warning: callback will be called on a different thread.
	void Load(const std::string &filename, Callback callback = Callback(), void *cbUserData = 0);

	// Save the current state to the specified file (async.)
	// Warning: callback will be called on a different thread.
	void Save(const std::string &filename, Callback callback = Callback(), void *cbUserData = 0);

	CChunkFileReader::Error SaveToRam(std::vector<u8> &state);
	CChunkFileReader::Error LoadFromRam(std::vector<u8> &state);

	// For testing / automated tests.  Runs a save state verification pass (async.)
	// Warning: callback will be called on a different thread.
	void Verify(Callback callback = Callback(), void *cbUserData = 0);

	// To go back to a previous snapshot (only if enabled.)
	// Warning: callback will be called on a different thread.
	void Rewind(Callback callback = Callback(), void *cbUserData = 0);

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
};
