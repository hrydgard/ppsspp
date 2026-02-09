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

#include <algorithm>
#include <vector>
#include <thread>
#include <mutex>
#include <string>
#include <set>

#include "Common/Data/Text/I18n.h"
#include "Common/Thread/ThreadUtil.h"
#include "Common/Data/Text/Parsers.h"
#include "Common/System/System.h"

#include "Common/File/FileUtil.h"
#include "Common/File/DirListing.h"
#include "Common/Serialize/Serializer.h"
#include "Common/Serialize/SerializeFuncs.h"
#include "Common/StringUtils.h"
#include "Common/TimeUtil.h"

#include "Core/SaveState.h"
#include "Core/Config.h"
#include "Core/Core.h"
#include "Core/CoreTiming.h"
#include "Core/Screenshot.h"
#include "Core/System.h"
#include "Core/SaveStateRewind.h"
#include "Core/FileSystems/MetaFileSystem.h"
#include "Core/ELF/ParamSFO.h"
#include "Core/HLE/HLE.h"
#include "Core/HLE/sceNet.h"
#include "Core/HLE/ReplaceTables.h"
#include "Core/HLE/sceDisplay.h"
#include "Core/HLE/sceKernel.h"
#include "Core/HLE/sceUtility.h"
#include "Core/MemMap.h"
#include "Core/MIPS/JitCommon/JitBlockCache.h"
#include "Core/RetroAchievements.h"
#include "HW/MemoryStick.h"

#ifndef MOBILE_DEVICE
#include "Core/AVIDump.h"
#include "Core/HLE/__sceAudio.h"
#endif

// Slot number is visual only, -2 will display special message
constexpr int LOAD_UNDO_SLOT = -2;
constexpr int SCREENSHOT_FAILURE_RETRIES = 6;

static const char * const STATE_EXTENSION = "ppst";
static const char * const UNDO_STATE_EXTENSION = "undo.ppst";
static const char * const UNDO_SCREENSHOT_EXTENSION = "undo.jpg";

static const char * const LOAD_UNDO_NAME = "load_undo.ppst";

namespace SaveState {

// Used for "confirm exit if you haven't saved in a while"
double g_lastSaveTime = -1.0;

static bool needsProcess = false;
static bool needsRestart = false;
static std::mutex mutex;
static bool hasLoadedState = false;
static const int STALE_STATE_USES = 2;
// 4 hours of total gameplay since the virtual PSP started the game.
static const u64 STALE_STATE_TIME = 4 * 3600 * 1000000ULL;
static int saveStateGeneration = 0;
static int saveDataGeneration = 0;
static int lastSaveDataGeneration = 0;
static std::string saveStateInitialGitVersion = "";
static StateRingbuffer rewindStates;

// Updated by Rescan. This is to avoid calling Exists a lot of times, which could be slow
// on some Android devices when we allow many save states.
static double g_lastRescan = 0.0;
static std::map<std::string, uint64_t, std::less<>> g_files;  // the value is the mod-time.

struct SaveStart {
	void DoState(PointerWrap &p);
};

enum class OperationType {
	Save,
	Load,
	Verify,
	Rewind,
};

struct Operation {
	// The slot number is for visual purposes only. Set to -1 for operations where we don't display a message for example.
	Operation(OperationType t, const Path &f, int slot_, Callback cb)
		: type(t), filename(f), callback(cb), slot(slot_) {}

	OperationType type;
	Path filename;
	Callback callback;
	int slot;
};

static std::vector<Operation> g_pendingOperations;

// If this isn't empty, a screenshot operation is pending. It's protected by mutex.
Path g_screenshotPath;

int g_screenshotFailures;

	CChunkFileReader::Error SaveToRam(std::vector<u8> &data) {
		SaveStart state;
		return CChunkFileReader::MeasureAndSavePtr(state, &data);
	}

	CChunkFileReader::Error LoadFromRam(std::vector<u8> &data, std::string *errorString) {
		SaveStart state;
		return CChunkFileReader::LoadPtr(&data[0], state, errorString);
	}

	// TODO: Should this be configurable?
	// Should only fail if the game hasn't created a framebuffer yet. Some games play video without a framebuffer,
	// we should probably just read back memory then instead (GTA for example). But this is a really unimportant edge case,
	// when in-game it's just not an issue.

	void SaveStart::DoState(PointerWrap &p) {
		auto s = p.Section("SaveStart", 1, 3);
		if (!s)
			return;

		if (s >= 2) {
			// This only increments on save, of course.
			++saveStateGeneration;
			Do(p, saveStateGeneration);
			// This saves the first git version to create this save state (or generation of save states.)
			if (saveStateInitialGitVersion.empty())
				saveStateInitialGitVersion = PPSSPP_GIT_VERSION;
			Do(p, saveStateInitialGitVersion);
		} else {
			saveStateGeneration = 1;
		}
		if (s >= 3) {
			// Keep track of savedata (not save states) too.
			Do(p, saveDataGeneration);
		} else {
			saveDataGeneration = 0;
		}

		// Gotta do CoreTiming before HLE, but from v3 we've moved it after the memory stuff.
		if (s <= 2) {
			CoreTiming::DoState(p);
		}

		// Memory is a bit tricky when jit is enabled, since there's emuhacks in it.
		// These must be saved before copying out memory and restored after.
		auto savedReplacements = SaveAndClearReplacements();
		if (MIPSComp::jit && p.mode == p.MODE_WRITE) {
			std::lock_guard<std::recursive_mutex> guard(MIPSComp::jitLock);
			if (MIPSComp::jit) {
				std::vector<u32> savedBlocks;
				savedBlocks = MIPSComp::jit->SaveAndClearEmuHackOps();
				Memory::DoState(p);
				MIPSComp::jit->RestoreSavedEmuHackOps(savedBlocks);
			} else {
				Memory::DoState(p);
			}
		} else {
			Memory::DoState(p);
		}

		if (s >= 3) {
			CoreTiming::DoState(p);
		}

		// Don't bother restoring if reading, we'll deal with that in KernelModuleDoState.
		// In theory, different functions might have been runtime loaded in the state.
		if (p.mode != p.MODE_READ) {
			RestoreSavedReplacements(savedReplacements);
		}

		MemoryStick_DoState(p);
		currentMIPS->DoState(p);
		HLEDoState(p);
		__KernelDoState(p);
		Achievements::DoState(p);
		// Kernel object destructors might close open files, so do the filesystem last.
		pspFileSystem.DoState(p);
	}

	void Enqueue(const SaveState::Operation &op) {
		if (!NetworkAllowSaveState()) {
			return;
		}
		if (Achievements::HardcoreModeActive()) {
			if (g_Config.bAchievementsSaveStateInHardcoreMode && ((op.type == SaveState::OperationType::Save))) {
				// We allow saving in hardcore mode if this setting is on.
			} else {
				// Operation not allowed
				return;
			}
		}

		std::lock_guard<std::mutex> guard(mutex);
		g_pendingOperations.push_back(op);

		// Don't actually run it until next frame.
		// It's possible there might be a duplicate but it won't hurt us.
		needsProcess = true;
	}

	void Load(const Path &filename, int slot, Callback callback) {
		if (!NetworkAllowSaveState()) {
			return;
		}

		rewindStates.NotifyState();
		if (coreState == CoreState::CORE_RUNTIME_ERROR)
			Core_Break(BreakReason::SavestateLoad, 0);
		Enqueue(Operation(OperationType::Load, filename, slot, callback));
	}

	void Save(const Path &filename, int slot, Callback callback) {
		if (!NetworkAllowSaveState()) {
			return;
		}

		rewindStates.NotifyState();
		if (coreState == CoreState::CORE_RUNTIME_ERROR)
			Core_Break(BreakReason::SavestateSave, 0);
		Enqueue(Operation(OperationType::Save, filename, slot, callback));
	}

	void Verify(Callback callback) {
		Enqueue(Operation(OperationType::Verify, Path(), -1, callback));
	}

	void Rewind(Callback callback) {
		if (g_netInited) {
			return;
		}
		if (coreState == CoreState::CORE_RUNTIME_ERROR)
			Core_Break(BreakReason::SavestateRewind, 0);
		Enqueue(Operation(OperationType::Rewind, Path(), -1, callback));
	}

	bool CanRewind() {
		return !rewindStates.Empty();
	}

	// Slot utilities

	std::string AppendSlotTitle(const std::string &filename, const std::string &title) {
		char slotChar = 0;
		auto detectSlot = [&](const std::string &ext) {
			if (!endsWith(filename, std::string(".") + ext)) {
				return false;
			}

			// Usually these are slots, let's check the slot # after the last '_'.
			size_t slotNumPos = filename.find_last_of('_');
			if (slotNumPos == filename.npos) {
				return false;
			}

			const size_t extLength = ext.length() + 1;
			// If we take out the extension, '_', etc. we should be left with only a single digit.
			if (slotNumPos + 1 + extLength != filename.length() - 1) {
				return false;
			}

			slotChar = filename[slotNumPos + 1];
			if (slotChar < '0' || slotChar > '8') {
				return false;
			}

			// Change from zero indexed to human friendly.
			slotChar++;
			return true;
		};

		if (detectSlot(STATE_EXTENSION)) {
			return StringFromFormat("%s (%c)", title.c_str(), slotChar);
		}
		if (detectSlot(UNDO_STATE_EXTENSION)) {
			auto sy = GetI18NCategory(I18NCat::SYSTEM);
			// Allow the number to be positioned where it makes sense.
			std::string undo(sy->T("undo %c"));
			return title + " (" + StringFromFormat(undo.c_str(), slotChar) + ")";
		}

		// Couldn't detect, use the filename.
		return title + " (" + filename + ")";
	}

	std::string GetTitle(const Path &filename) {
		std::string title;
		if (CChunkFileReader::GetFileTitle(filename, &title) == CChunkFileReader::ERROR_NONE) {
			if (title.empty()) {
				return filename.GetFilename();
			}

			return AppendSlotTitle(filename.GetFilename(), title);
		}

		// The file can't be loaded - let's note that.
		auto sy = GetI18NCategory(I18NCat::SYSTEM);
		return filename.GetFilename() + " " + std::string(sy->T("(broken)"));
	}

	// TODO: gameFilename is ignored!
	// This means that this must always be called in-game, otherwise it will not work!
	//
	// This particular ID string generation is specific for save states.
	static std::string GenerateFullDiscId(const ParamSFOData &paramSFO) {
		std::string discId = paramSFO.GetValueString("DISC_ID");
		std::string discVer = paramSFO.GetValueString("DISC_VERSION");
		if (discId.empty()) {
			// Should never happen.
			discId = g_paramSFO.GenerateFakeID(Path());
			discVer = "1.00";
		}
		return StringFromFormat("%s_%s", discId.c_str(), discVer.c_str());
	}

	std::string GetGamePrefix(const ParamSFOData &paramSFO) {
		return GenerateFullDiscId(paramSFO);
	}

	// The prefix is always based on GenerateFullDiscId. So we can find these by scanning, too.
	std::string GenerateSaveSlotFilename(std::string_view gamePrefix, int slot, const char *extension) {
		return StringFromFormat("%.*s_%d.%s", STR_VIEW(gamePrefix), slot, extension);
	}

	Path GenerateSaveSlotPath(std::string_view gamePrefix, int slot, const char *extension) {
		std::string filename = GenerateSaveSlotFilename(gamePrefix, slot, extension);
		return GetSysDirectory(DIRECTORY_SAVESTATE) / filename;
	}

	int GetCurrentSlot() {
		return g_Config.iCurrentStateSlot;
	}

	void PrevSlot() {
		g_Config.iCurrentStateSlot = (g_Config.iCurrentStateSlot - 1 + g_Config.iSaveStateSlotCount) % g_Config.iSaveStateSlotCount;
	}

	void NextSlot() {
		g_Config.iCurrentStateSlot = (g_Config.iCurrentStateSlot + 1) % g_Config.iSaveStateSlotCount;
	}

	static void DeleteIfExists(const Path &fn) {
		// Just avoiding error messages.
		File::Delete(fn, true);
	}

	static void RenameIfExists(const Path &from, const Path &to) {
		if (File::Exists(from)) {
			File::Rename(from, to);
		}
	}

	static void SwapIfExists(const Path &from, const Path &to) {
		Path temp = from.WithExtraExtension(".tmp");
		if (File::Exists(from)) {
			File::Rename(from, temp);
			File::Rename(to, from);
			File::Rename(temp, to);
		}
	}

	static void ScheduleSaveScreenshot(const Path &path) {
		std::lock_guard<std::mutex> guard(mutex);
		g_screenshotPath = path;
		g_screenshotFailures = 0;
	}

	void LoadSlot(std::string_view gamePrefix, int slot, Callback callback) {
		if (!NetworkAllowSaveState()) {
			return;
		}

		Path fn = GenerateSaveSlotPath(gamePrefix, slot, STATE_EXTENSION);
		if (!fn.empty()) {
			// This add only 1 extra state, should we just always enable it?
			if (g_Config.bEnableStateUndo) {
				Path backup = GetSysDirectory(DIRECTORY_SAVESTATE) / LOAD_UNDO_NAME;
				
				auto saveCallback = [=](Status status, std::string_view message) {
					if (status != Status::FAILURE) {
						DeleteIfExists(backup);
						File::Rename(backup.WithExtraExtension(".tmp"), backup);
						g_Config.sStateLoadUndoGame = gamePrefix;
						g_Config.Save("Saving config for savestate last load undo");
					} else {
						ERROR_LOG(Log::SaveState, "Saving load undo state failed: %.*s", (int)message.size(), message.data());
					}
					Load(fn, slot, callback);
				};

				if (!backup.empty()) {
					Save(backup.WithExtraExtension(".tmp"), LOAD_UNDO_SLOT, saveCallback);
				} else {
					ERROR_LOG(Log::SaveState, "Saving load undo state failed. Error in the file system.");
					Load(fn, slot, callback);
				}
			} else {
				Load(fn, slot, callback);
			}
		} else {
			if (callback) {
				auto sy = GetI18NCategory(I18NCat::SYSTEM);
				callback(Status::FAILURE, sy->T("Failed to load state. Error in the file system."));
			}
		}
	}

	bool UndoLoad(std::string_view gamePrefix, Callback callback) {
		if (!NetworkAllowSaveState()) {
			return false;
		}

		if (g_Config.sStateLoadUndoGame != gamePrefix) {
			if (callback) {
				auto sy = GetI18NCategory(I18NCat::SYSTEM);
				callback(Status::FAILURE, sy->T("Error: load undo state is from a different game"));
			}
			return false;
		}

		Path fn = GetSysDirectory(DIRECTORY_SAVESTATE) / LOAD_UNDO_NAME;
		if (!fn.empty()) {
			Load(fn, LOAD_UNDO_SLOT, callback);
			return true;
		} else {
			if (callback) {
				auto sy = GetI18NCategory(I18NCat::SYSTEM);
				callback(Status::FAILURE, sy->T("Failed to load state for load undo. Error in the file system."));
			}
			return false;
		}
	}

	void SaveSlot(std::string_view gamePrefix, int slot, Callback callback) {
		if (!NetworkAllowSaveState()) {
			return;
		}

		Path fn = GenerateSaveSlotPath(gamePrefix, slot, STATE_EXTENSION);
		Path fnUndo = GenerateSaveSlotPath(gamePrefix, slot, UNDO_STATE_EXTENSION);
		if (!fn.empty()) {
			Path shot = GenerateSaveSlotPath(gamePrefix, slot, SCREENSHOT_EXTENSION);
			auto renameCallback = [=](Status status, std::string_view message) {
				if (status != Status::FAILURE) {
					if (g_Config.bEnableStateUndo) {
						DeleteIfExists(fnUndo);
						RenameIfExists(fn, fnUndo);
						g_Config.sStateUndoLastSaveGame = gamePrefix;
						g_Config.iStateUndoLastSaveSlot = slot;
						g_Config.Save("Saving config for savestate last save undo");
					} else {
						DeleteIfExists(fn);
					}
					File::Rename(fn.WithExtraExtension(".tmp"), fn);
				}
				if (callback) {
					callback(status, message);
				}
			};
			// Let's also create a screenshot.
			if (g_Config.bEnableStateUndo) {
				Path shotUndo = GenerateSaveSlotPath(gamePrefix, slot, UNDO_SCREENSHOT_EXTENSION);
				DeleteIfExists(shotUndo);
				RenameIfExists(shot, shotUndo);
			}
			ScheduleSaveScreenshot(shot);
			Save(fn.WithExtraExtension(".tmp"), slot, renameCallback);
		} else {
			if (callback) {
				auto sy = GetI18NCategory(I18NCat::SYSTEM);
				callback(Status::FAILURE, sy->T("Failed to save state. Error in the file system."));
			}
		}
		Rescan(gamePrefix);
	}

	bool UndoSaveSlot(std::string_view gamePrefix, int slot) {
		if (!NetworkAllowSaveState()) {
			return false;
		}

		Path fnUndo = GenerateSaveSlotPath(gamePrefix, slot, UNDO_STATE_EXTENSION);

		// Do nothing if there's no undo.
		if (File::Exists(fnUndo)) {
			Path fn = GenerateSaveSlotPath(gamePrefix, slot, STATE_EXTENSION);
			Path shot = GenerateSaveSlotPath(gamePrefix, slot, SCREENSHOT_EXTENSION);
			Path shotUndo = GenerateSaveSlotPath(gamePrefix, slot, UNDO_SCREENSHOT_EXTENSION);
			// Swap them so they can undo again to redo.  Mistakes happen.
			SwapIfExists(shotUndo, shot);
			SwapIfExists(fnUndo, fn);
			return true;
		}

		Rescan(gamePrefix);
		return false;
	}

	void DeleteSlot(std::string_view gamePrefix, int slot) {
		Path fn = GenerateSaveSlotPath(gamePrefix, slot, STATE_EXTENSION);
		Path shot = GenerateSaveSlotPath(gamePrefix, slot, SCREENSHOT_EXTENSION);

		if (File::Exists(fn)) {
			DeleteIfExists(fn);
			DeleteIfExists(shot);
		}
		Rescan(gamePrefix);
	}

	bool UndoLastSave(std::string_view gamePrefix) {
		if (!NetworkAllowSaveState()) {
			return false;
		}

		if (g_Config.sStateUndoLastSaveGame != gamePrefix)
			return false;

		bool retval = UndoSaveSlot(gamePrefix, g_Config.iStateUndoLastSaveSlot);
		Rescan(gamePrefix);
		return retval;
	}

	void Rescan(std::string_view gamePrefix) {
		// Currently nothing to do here.
		// On Android this would be important to rescan the save state directory.
		std::vector<File::FileInfo> file;

		std::string prefix(gamePrefix);
		prefix += "_";

		if (!File::GetFilesInDir(GetSysDirectory(DIRECTORY_SAVESTATE), &file, nullptr, 0, prefix)) {
			ERROR_LOG(Log::System, "Failed to list savestate directory!");
			return;
		}

		g_files.clear();
		for (const auto &f : file) {
			g_files[f.name] = f.mtime;
		}

		g_lastRescan = time_now_d();
	}

	static bool SaveStateFileExists(std::string_view gamePrefix, int slot, const char *extension) {
		return g_files.find(GenerateSaveSlotFilename(gamePrefix, slot, extension)) != g_files.end();
	}

	bool HasSaveInSlot(std::string_view gamePrefix, int slot) {
		return SaveStateFileExists(gamePrefix, slot, STATE_EXTENSION);
	}

	bool HasUndoSaveInSlot(std::string_view gamePrefix, int slot) {
		return SaveStateFileExists(gamePrefix, slot, UNDO_STATE_EXTENSION);
	}

	bool HasUndoLastSave(std::string_view gamePrefix) {
		if (g_Config.sStateUndoLastSaveGame != gamePrefix)
			return false;

		return HasUndoSaveInSlot(gamePrefix, g_Config.iStateUndoLastSaveSlot);
	}

	bool HasScreenshotInSlot(std::string_view gamePrefix, int slot) {
		return SaveStateFileExists(gamePrefix, slot, SCREENSHOT_EXTENSION);
	}

	bool HasUndoLoad(std::string_view gamePrefix) {
		Path fn = GetSysDirectory(DIRECTORY_SAVESTATE) / LOAD_UNDO_NAME;
		return File::Exists(fn) && g_Config.sStateLoadUndoGame == gamePrefix;
	}

	bool operator < (const tm &t1, const tm &t2) {
		if (t1.tm_year < t2.tm_year) return true;
		if (t1.tm_year > t2.tm_year) return false;
		if (t1.tm_mon < t2.tm_mon) return true;
		if (t1.tm_mon > t2.tm_mon) return false;
		if (t1.tm_mday < t2.tm_mday) return true;
		if (t1.tm_mday > t2.tm_mday) return false;
		if (t1.tm_hour < t2.tm_hour) return true;
		if (t1.tm_hour > t2.tm_hour) return false;
		if (t1.tm_min < t2.tm_min) return true;
		if (t1.tm_min > t2.tm_min) return false;
		if (t1.tm_sec < t2.tm_sec) return true;
		if (t1.tm_sec > t2.tm_sec) return false;
		return false;
	}

	bool operator > (const tm &t1, const tm &t2) {
		if (t1.tm_year > t2.tm_year) return true;
		if (t1.tm_year < t2.tm_year) return false;
		if (t1.tm_mon > t2.tm_mon) return true;
		if (t1.tm_mon < t2.tm_mon) return false;
		if (t1.tm_mday > t2.tm_mday) return true;
		if (t1.tm_mday < t2.tm_mday) return false;
		if (t1.tm_hour > t2.tm_hour) return true;
		if (t1.tm_hour < t2.tm_hour) return false;
		if (t1.tm_min > t2.tm_min) return true;
		if (t1.tm_min < t2.tm_min) return false;
		if (t1.tm_sec > t2.tm_sec) return true;
		if (t1.tm_sec < t2.tm_sec) return false;
		return false;
	}

	bool operator ! (const tm &t1) {
		if (t1.tm_year || t1.tm_mon || t1.tm_mday || t1.tm_hour || t1.tm_min || t1.tm_sec) return false;
		return true;
	}

	int GetNewestSlot(std::string_view gamePrefix) {
		int newestSlot = -1;
		int64_t newestTime = 0;
		for (int i = 0; i < g_Config.iSaveStateSlotCount; i++) {
			auto iter = g_files.find(GenerateSaveSlotFilename(gamePrefix, i, STATE_EXTENSION));
			if (iter != g_files.end()) {
				int64_t mod = iter->second;
				if (mod > newestTime) {
					newestTime = mod;
					newestSlot = i;
				}
			}
		}
		return newestSlot;
	}

	int GetOldestSlot(std::string_view gamePrefix) {
		int oldestSlot = -1;
		int64_t oldestTime = INT64_MAX;
		for (int i = 0; i < g_Config.iSaveStateSlotCount; i++) {
			auto iter = g_files.find(GenerateSaveSlotFilename(gamePrefix, i, STATE_EXTENSION));
			if (iter != g_files.end()) {
				int64_t mod = iter->second;
				if (mod < oldestTime) {
					oldestTime = mod;
					oldestSlot = i;
				}
			}
		}
		return oldestSlot;
	}

	std::string GetSlotDateAsString(std::string_view gamePrefix, int slot) {
		std::string fn = GenerateSaveSlotFilename(gamePrefix, slot, STATE_EXTENSION);
		auto iter = g_files.find(fn);
		if (iter == g_files.end()) {
			return "";
		}
		time_t t = iter->second;
		tm time;
		localtime_r((time_t*)&t, &time);
		char buf[256];
		// TODO: Use local time format instead of the configured PSP format?
		// Americans and some others might not like ISO standard :)
		switch (g_Config.iDateFormat) {
		case PSP_SYSTEMPARAM_DATE_FORMAT_YYYYMMDD:
			strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &time);
			break;
		case PSP_SYSTEMPARAM_DATE_FORMAT_MMDDYYYY:
			strftime(buf, sizeof(buf), "%m-%d-%Y %H:%M:%S", &time);
			break;
		case PSP_SYSTEMPARAM_DATE_FORMAT_DDMMYYYY:
			strftime(buf, sizeof(buf), "%d-%m-%Y %H:%M:%S", &time);
			break;
		default: // Should never happen
			return "";
		}
		return buf;
	}

	std::vector<Operation> Flush() {
		std::lock_guard<std::mutex> guard(mutex);
		std::vector<Operation> copy = g_pendingOperations;
		g_pendingOperations.clear();

		return copy;
	}

	bool HandleLoadFailure(bool wasRewinding) {
		if (wasRewinding) {
			WARN_LOG(Log::SaveState, "HandleLoadFailure - trying a rewind state.");
			// Okay, first, let's give the next rewind state a shot - maybe we can at least not reset entirely.
			// Actually, this seems like a bad thing - some systems can end up in bad states, like sceFont :(
			CChunkFileReader::Error result;
			do {
				std::string errorString;
				result = rewindStates.Restore(&errorString);
			} while (result == CChunkFileReader::ERROR_BROKEN_STATE);

			if (result == CChunkFileReader::ERROR_NONE) {
				return true;
			}
		}

		// We tried, our only remaining option is to reset the game.
		needsRestart = true;
		// Make sure we don't proceed to run anything yet.
		coreState = CORE_NEXTFRAME;
		return false;
	}

	bool HasLoadedState() {
		return hasLoadedState;
	}

	bool IsStale() {
		if (saveStateGeneration >= STALE_STATE_USES) {
			return CoreTiming::GetGlobalTimeUs() > STALE_STATE_TIME;
		}
		return false;
	}

	bool IsOldVersion() {
		if (saveStateInitialGitVersion.empty())
			return false;

		Version state(saveStateInitialGitVersion);
		Version gitVer(PPSSPP_GIT_VERSION);
		if (!state.IsValid() || !gitVer.IsValid())
			return false;

		return state < gitVer;
	}

	static Status TriggerLoadWarnings(std::string &callbackMessage) {
		auto sc = GetI18NCategory(I18NCat::SCREEN);

		if (g_Config.bHideStateWarnings)
			return Status::SUCCESS;

		if (IsStale()) {
			// For anyone wondering why (too long to put on the screen in an osm):
			// Using save states instead of saves simulates many hour play sessions.
			// Sometimes this exposes game bugs that were rarely seen on real devices,
			// because few people played on a real PSP for 10 hours straight.
			callbackMessage = sc->T("Loaded. Save in game, restart, and load for less bugs.");
			return Status::SUCCESS;
		}
		if (IsOldVersion()) {
			// Save states also preserve bugs from old PPSSPP versions, so warn.
			callbackMessage = sc->T("Loaded. Save in game, restart, and load for less bugs.");
			return Status::SUCCESS;
		}
		// If the loaded state (saveDataGeneration) is older, the game may prevent saving again.
		// This can happen with newer too, but ignore to/from 0 as a common likely safe case.
		if (saveDataGeneration != lastSaveDataGeneration && saveDataGeneration != 0 && lastSaveDataGeneration != 0) {
			if (saveDataGeneration < lastSaveDataGeneration)
				callbackMessage = sc->T("Loaded. Game may refuse to save over newer savedata.");
			else
				callbackMessage = sc->T("Loaded. Game may refuse to save over different savedata.");
			return Status::WARNING;
		}
		return Status::SUCCESS;
	}

	// NOTE: This can cause ending of the current renderpass, due to the readback needed for the screenshot.
	// TODO: This should run the actual operations on a thread. While this returns true (for example), emulation
	// *must* not run further, in order not to disturb the current state operation.
	void Process() {
		rewindStates.Process();

		if (!needsProcess)
			return;
		needsProcess = false;

		if (!__KernelIsRunning()) {
			ERROR_LOG(Log::SaveState, "Savestate failure: Unable to load without kernel, this should never happen.");
			return;
		}

		std::vector<Operation> operations = Flush();
		SaveStart state;

		for (const auto &op : operations) {
			CChunkFileReader::Error result;
			Status callbackResult;
			std::string callbackMessage;
			std::string title;

			auto sc = GetI18NCategory(I18NCat::SCREEN);
			const char *i18nLoadFailure = sc->T_cstr("Failed to load state");
			const char *i18nSaveFailure = sc->T_cstr("Failed to save state");

			std::string slot_prefix = op.slot >= 0 ? StringFromFormat("(%d) ", op.slot + 1) : "";
			std::string errorString;

			switch (op.type) {
			case OperationType::Load:
				INFO_LOG(Log::SaveState, "Loading state from '%s'", op.filename.c_str());
				// Use the state's latest version as a guess for saveStateInitialGitVersion.
				result = CChunkFileReader::Load(op.filename, &saveStateInitialGitVersion, state, &errorString);
				if (result == CChunkFileReader::ERROR_NONE) {
					callbackMessage = op.slot != LOAD_UNDO_SLOT ? sc->T("Loaded State") : sc->T("State load undone");
					callbackResult = TriggerLoadWarnings(callbackMessage);
					hasLoadedState = true;
					Core_ResetException();

					if (!slot_prefix.empty())
						callbackMessage = slot_prefix + callbackMessage;

#ifndef MOBILE_DEVICE
					if (g_Config.bSaveLoadResetsAVdumping) {
						if (g_Config.bDumpFrames) {
							AVIDump::Stop();
							AVIDump::Start(PSP_CoreParameter().renderWidth, PSP_CoreParameter().renderHeight);
						}
						if (g_Config.bDumpAudio) {
							WAVDump::Reset();
						}
					}
#endif
					g_lastSaveTime = time_now_d();
				} else if (result == CChunkFileReader::ERROR_BROKEN_STATE) {
					HandleLoadFailure(false);
					callbackMessage = std::string(i18nLoadFailure) + ": " + errorString;
					ERROR_LOG(Log::SaveState, "Load state failure: %s", errorString.c_str());
					callbackResult = Status::FAILURE;
				} else {
					callbackMessage = sc->T(errorString.c_str(), i18nLoadFailure);
					callbackResult = Status::FAILURE;
				}
				break;

			case OperationType::Save:
				INFO_LOG(Log::SaveState, "Saving state to '%s'", op.filename.c_str());
				title = g_paramSFO.GetValueString("TITLE");
				if (title.empty()) {
					// Homebrew title
					title = PSP_CoreParameter().fileToStart.ToVisualString();
					std::size_t lslash = title.find_last_of('/');
					title = title.substr(lslash + 1);
				}
				result = CChunkFileReader::Save(op.filename, title, PPSSPP_GIT_VERSION, state);
				if (result == CChunkFileReader::ERROR_NONE) {
					callbackMessage = slot_prefix + std::string(sc->T("Saved State"));
					callbackResult = Status::SUCCESS;
#ifndef MOBILE_DEVICE
					if (g_Config.bSaveLoadResetsAVdumping) {
						if (g_Config.bDumpFrames) {
							AVIDump::Stop();
							AVIDump::Start(PSP_CoreParameter().renderWidth, PSP_CoreParameter().renderHeight);
						}
						if (g_Config.bDumpAudio) {
							WAVDump::Reset();
						}
					}
#endif
					g_lastSaveTime = time_now_d();
				} else if (result == CChunkFileReader::ERROR_BROKEN_STATE) {
					// TODO: What else might we want to do here? This should be very unusual.
					callbackMessage = i18nSaveFailure;
					ERROR_LOG(Log::SaveState, "Save state failure");
					callbackResult = Status::FAILURE;
				} else {
					callbackMessage = i18nSaveFailure;
					callbackResult = Status::FAILURE;
				}
				break;

			case OperationType::Verify:
			{
				int tempResult = CChunkFileReader::Verify(state) == CChunkFileReader::ERROR_NONE;
				callbackResult = tempResult ? Status::SUCCESS : Status::FAILURE;
				if (tempResult) {
					INFO_LOG(Log::SaveState, "Verified save state system");
				} else {
					ERROR_LOG(Log::SaveState, "Save state system verification failed");
				}
				break;
			}

			case OperationType::Rewind:
				INFO_LOG(Log::SaveState, "Rewinding to recent savestate snapshot");
				result = rewindStates.Restore(&errorString);
				if (result == CChunkFileReader::ERROR_NONE) {
					callbackMessage = sc->T("Loaded State");
					callbackResult = Status::SUCCESS;
					hasLoadedState = true;
					Core_ResetException();
				} else if (result == CChunkFileReader::ERROR_BROKEN_STATE) {
					// Cripes.  Good news is, we might have more.  Let's try those too, better than a reset.
					if (HandleLoadFailure(true)) {
						// Well, we did rewind, even if too much...
						callbackMessage = sc->T("Loaded State");
						callbackResult = Status::SUCCESS;
						hasLoadedState = true;
						Core_ResetException();
					} else {
						callbackMessage = std::string(i18nLoadFailure) + ": " + errorString;
						callbackResult = Status::FAILURE;
					}
				} else {
					callbackMessage = std::string(i18nLoadFailure) + ": " + errorString;
					callbackResult = Status::FAILURE;
				}
				break;

			default:
				ERROR_LOG(Log::SaveState, "Savestate failure: unknown operation type %d", (int)op.type);
				callbackResult = Status::FAILURE;
				break;
			}

			if (op.callback) {
				op.callback(callbackResult, callbackMessage);
			}
		}
		if (operations.size()) {
			// Avoid triggering frame skipping due to slowdown
			__DisplaySetWasPaused();
		}
	}

	void NotifySaveData() {
		saveDataGeneration++;
		lastSaveDataGeneration = saveDataGeneration;
	}

	bool PollRestartNeeded() {
		if (needsRestart) {
			needsRestart = false;
			return true;
		}
		return false;
	}

	void Init() {
		// Make sure there's a directory for save slots
		File::CreateFullPath(GetSysDirectory(DIRECTORY_SAVESTATE));

		std::lock_guard<std::mutex> guard(mutex);
		rewindStates.Clear();

		hasLoadedState = false;
		saveStateGeneration = 0;
		saveDataGeneration = 0;
		lastSaveDataGeneration = 0;
		saveStateInitialGitVersion.clear();

		g_lastSaveTime = time_now_d();
	}

	void Shutdown() {
		std::lock_guard<std::mutex> guard(mutex);
		rewindStates.Clear();
	}

	double SecondsSinceLastSavestate() {
		if (g_lastSaveTime < 0) {
			return -1.0;
		} else {
			return time_now_d() - g_lastSaveTime;
		}
	}

bool ProcessScreenshot(bool skipBufferEffects) {
	Path screenshotPath;
	{
		std::lock_guard<std::mutex> guard(mutex);
		if (!g_screenshotPath.empty()) {
			screenshotPath = g_screenshotPath;
			g_screenshotPath.clear();
		} else {
			return false;
		}
	}

	// Savestate thumbnails don't need to be bigger.
	constexpr int maxResMultiplier = 2;
	ScreenshotResult tempResult = TakeGameScreenshot(nullptr, screenshotPath, ScreenshotFormat::JPG, SCREENSHOT_DISPLAY, maxResMultiplier, [](bool success) {
		if (success) {
			g_screenshotFailures = 0;
		}
	});

	switch (tempResult) {
	case ScreenshotResult::ScreenshotNotPossible:
		// Try again soon, for a short while.
		WARN_LOG(Log::SaveState, "Failed to take a screenshot for the savestate! (%s) The savestate will lack an icon.", g_screenshotPath.c_str());
		if (coreState != CORE_STEPPING_CPU && g_screenshotFailures++ < SCREENSHOT_FAILURE_RETRIES) {
			// Requeue for next frame (if we were stepping, no point, will just spam errors quickly).
			ScheduleSaveScreenshot(g_screenshotPath);
		}
		break;
	case ScreenshotResult::FailedToWriteFile:
		break;
	case ScreenshotResult::DelayedResult:
		return true;
	case ScreenshotResult::Success:
		return true;
	}
	return false; // Didn't take a screenshot right now.
}

}  // namespace SaveState
