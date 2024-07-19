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

#include "Common/Data/Text/I18n.h"
#include "Common/Thread/ThreadUtil.h"
#include "Common/Data/Text/Parsers.h"
#include "Common/System/System.h"

#include "Common/File/FileUtil.h"
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
#include "Core/FileSystems/MetaFileSystem.h"
#include "Core/ELF/ParamSFO.h"
#include "Core/HLE/HLE.h"
#include "Core/HLE/ReplaceTables.h"
#include "Core/HLE/sceDisplay.h"
#include "Core/HLE/sceKernel.h"
#include "Core/HLE/sceUtility.h"
#include "Core/MemMap.h"
#include "Core/MIPS/MIPS.h"
#include "Core/MIPS/JitCommon/JitBlockCache.h"
#include "Core/RetroAchievements.h"
#include "HW/MemoryStick.h"
#include "GPU/GPUState.h"

#ifndef MOBILE_DEVICE
#include "Core/AVIDump.h"
#include "Core/HLE/__sceAudio.h"
#endif

// Slot number is visual only, -2 will display special message
constexpr int LOAD_UNDO_SLOT = -2;

namespace SaveState
{
	struct SaveStart
	{
		void DoState(PointerWrap &p);
	};

	enum OperationType
	{
		SAVESTATE_SAVE,
		SAVESTATE_LOAD,
		SAVESTATE_VERIFY,
		SAVESTATE_REWIND,
		SAVESTATE_SAVE_SCREENSHOT,
	};

	struct Operation
	{
		// The slot number is for visual purposes only. Set to -1 for operations where we don't display a message for example.
		Operation(OperationType t, const Path &f, int slot_, Callback cb, void *cbUserData_)
			: type(t), filename(f), callback(cb), slot(slot_), cbUserData(cbUserData_)
		{
		}

		OperationType type;
		Path filename;
		Callback callback;
		int slot;
		void *cbUserData;
	};

	CChunkFileReader::Error SaveToRam(std::vector<u8> &data) {
		SaveStart state;
		return CChunkFileReader::MeasureAndSavePtr(state, &data);
	}

	CChunkFileReader::Error LoadFromRam(std::vector<u8> &data, std::string *errorString) {
		SaveStart state;
		return CChunkFileReader::LoadPtr(&data[0], state, errorString);
	}

	// This ring buffer of states is for rewind save states, which are kept in RAM.
	// Save states are compressed against one of two reference saves (bases_), and the reference
	// is switched to a fresh save every N saves, where N is BASE_USAGE_INTERVAL.
	// The compression is a simple block based scheme where 0 means to copy a block from the base,
	// and 1 means that the following bytes are the next block. See Compress/LockedDecompress.
	class StateRingbuffer {
	public:
		StateRingbuffer() {
			size_ = REWIND_NUM_STATES;
			states_.resize(size_);
			baseMapping_.resize(size_);
		}

		~StateRingbuffer() {
			if (compressThread_.joinable()) {
				compressThread_.join();
			}
		}

		CChunkFileReader::Error Save()
		{
			rewindLastTime_ = time_now_d();

			// Make sure we're not processing a previous save. That'll cause a hitch though, but at least won't
			// crash due to contention over buffer_.
			if (compressThread_.joinable())
				compressThread_.join();

			std::lock_guard<std::mutex> guard(lock_);

			int n = next_++ % size_;
			if ((next_ % size_) == first_)
				++first_;

			std::vector<u8> *compressBuffer = &buffer_;
			CChunkFileReader::Error err;

			if (base_ == -1 || ++baseUsage_ > BASE_USAGE_INTERVAL)
			{
				base_ = (base_ + 1) % ARRAY_SIZE(bases_);
				baseUsage_ = 0;
				err = SaveToRam(bases_[base_]);
				// Let's not bother savestating twice.
				compressBuffer = &bases_[base_];
			}
			else
				err = SaveToRam(buffer_);

			if (err == CChunkFileReader::ERROR_NONE)
				ScheduleCompress(&states_[n], compressBuffer, &bases_[base_]);
			else
				states_[n].clear();

			baseMapping_[n] = base_;
			return err;
		}

		CChunkFileReader::Error Restore(std::string *errorString)
		{
			std::lock_guard<std::mutex> guard(lock_);

			// No valid states left.
			if (Empty())
				return CChunkFileReader::ERROR_BAD_FILE;

			int n = (--next_ + size_) % size_;
			if (states_[n].empty())
				return CChunkFileReader::ERROR_BAD_FILE;

			static std::vector<u8> buffer;
			LockedDecompress(buffer, states_[n], bases_[baseMapping_[n]]);
			CChunkFileReader::Error error = LoadFromRam(buffer, errorString);
			rewindLastTime_ = time_now_d();
			return error;
		}

		void ScheduleCompress(std::vector<u8> *result, const std::vector<u8> *state, const std::vector<u8> *base)
		{
			if (compressThread_.joinable())
				compressThread_.join();
			compressThread_ = std::thread([=]{
				SetCurrentThreadName("SaveStateCompress");

				// Should do no I/O, so no JNI thread context needed.
				Compress(*result, *state, *base);
			});
		}

		void Compress(std::vector<u8> &result, const std::vector<u8> &state, const std::vector<u8> &base)
		{
			std::lock_guard<std::mutex> guard(lock_);
			// Bail if we were cleared before locking.
			if (first_ == 0 && next_ == 0)
				return;

			double start_time = time_now_d();
			result.clear();
			result.reserve(512 * 1024);
			for (size_t i = 0; i < state.size(); i += BLOCK_SIZE)
			{
				int blockSize = std::min(BLOCK_SIZE, (int)(state.size() - i));
				if (i + blockSize > base.size() || memcmp(&state[i], &base[i], blockSize) != 0)
				{
					result.push_back(1);
					result.insert(result.end(), state.begin() + i, state.begin() + i + blockSize);
				}
				else
					result.push_back(0);
			}

			double taken_s = time_now_d() - start_time;
			DEBUG_LOG(Log::SaveState, "Rewind: Compressed save from %d bytes to %d in %0.2f ms.", (int)state.size(), (int)result.size(), taken_s * 1000.0);
		}

		void LockedDecompress(std::vector<u8> &result, const std::vector<u8> &compressed, const std::vector<u8> &base)
		{
			result.clear();
			result.reserve(base.size());
			auto basePos = base.begin();
			for (size_t i = 0; i < compressed.size(); )
			{
				if (compressed[i] == 0)
				{
					++i;
					int blockSize = std::min(BLOCK_SIZE, (int)(base.size() - result.size()));
					result.insert(result.end(), basePos, basePos + blockSize);
					basePos += blockSize;
				}
				else
				{
					++i;
					int blockSize = std::min(BLOCK_SIZE, (int)(compressed.size() - i));
					result.insert(result.end(), compressed.begin() + i, compressed.begin() + i + blockSize);
					i += blockSize;
					// This check is to avoid advancing basePos out of range, which MSVC catches.
					// When this happens, we're at the end of decoding anyway.
					if (base.end() - basePos >= blockSize) {
						basePos += blockSize;
					}
				}
			}
		}

		void Clear()
		{
			if (compressThread_.joinable())
				compressThread_.join();

			// This lock is mainly for shutdown.
			std::lock_guard<std::mutex> guard(lock_);
			first_ = 0;
			next_ = 0;
			for (auto &b : bases_) {
				b.clear();
			}
			baseMapping_.clear();
			baseMapping_.resize(size_);
			for (auto &s : states_) {
				s.clear();
			}
			buffer_.clear();
			base_ = -1;
			baseUsage_ = 0;
			rewindLastTime_ = time_now_d();
		}

		bool Empty() const
		{
			return next_ == first_;
		}

		void Process() {
			if (g_Config.iRewindSnapshotInterval <= 0) {
				return;
			}
			if (coreState != CORE_RUNNING) {
				return;
			}

			// For fast-forwarding, otherwise they may be useless and too close.
			double now = time_now_d();
			double diff = now - rewindLastTime_;
			if (diff < g_Config.iRewindSnapshotInterval)
				return;

			DEBUG_LOG(Log::SaveState, "Saving rewind state");
			Save();
		}

		void NotifyState() {
			// Prevent saving snapshots immediately after loading or saving a state.
			rewindLastTime_ = time_now_d();
		}

	private:
		const int BLOCK_SIZE = 8192;
		const int REWIND_NUM_STATES = 20;
		// TODO: Instead, based on size of compressed state?
		const int BASE_USAGE_INTERVAL = 15;

		typedef std::vector<u8> StateBuffer;

		int first_ = 0;
		int next_ = 0;
		int size_;

		std::vector<StateBuffer> states_;
		StateBuffer bases_[2];
		std::vector<int> baseMapping_;
		std::mutex lock_;
		std::thread compressThread_;
		std::vector<u8> buffer_;

		int base_ = -1;
		int baseUsage_ = 0;

		double rewindLastTime_ = 0.0f;
	};

	static bool needsProcess = false;
	static bool needsRestart = false;
	static std::vector<Operation> pending;
	static std::mutex mutex;
	static int screenshotFailures = 0;
	static bool hasLoadedState = false;
	static const int STALE_STATE_USES = 2;
	// 4 hours of total gameplay since the virtual PSP started the game.
	static const u64 STALE_STATE_TIME = 4 * 3600 * 1000000ULL;
	static int saveStateGeneration = 0;
	static int saveDataGeneration = 0;
	static int lastSaveDataGeneration = 0;
	static std::string saveStateInitialGitVersion = "";

	// TODO: Should this be configurable?
	static const int SCREENSHOT_FAILURE_RETRIES = 15;
	static StateRingbuffer rewindStates;

	void SaveStart::DoState(PointerWrap &p)
	{
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

	void Enqueue(const SaveState::Operation &op)
	{
		if (Achievements::HardcoreModeActive()) {
			if (g_Config.bAchievementsSaveStateInHardcoreMode && ((op.type == SaveState::SAVESTATE_SAVE) || (op.type == SAVESTATE_SAVE_SCREENSHOT))) {
				// We allow saving in hardcore mode if this setting is on.
			} else {
				// Operation not allowed
				return;
			}
		}

		std::lock_guard<std::mutex> guard(mutex);
		pending.push_back(op);

		// Don't actually run it until next frame.
		// It's possible there might be a duplicate but it won't hurt us.
		needsProcess = true;
		Core_UpdateSingleStep();
	}

	void Load(const Path &filename, int slot, Callback callback, void *cbUserData)
	{
		rewindStates.NotifyState();
		if (coreState == CoreState::CORE_RUNTIME_ERROR)
			Core_EnableStepping(true, "savestate.load", 0);
		Enqueue(Operation(SAVESTATE_LOAD, filename, slot, callback, cbUserData));
	}

	void Save(const Path &filename, int slot, Callback callback, void *cbUserData)
	{
		rewindStates.NotifyState();
		if (coreState == CoreState::CORE_RUNTIME_ERROR)
			Core_EnableStepping(true, "savestate.save", 0);
		Enqueue(Operation(SAVESTATE_SAVE, filename, slot, callback, cbUserData));
	}

	void Verify(Callback callback, void *cbUserData)
	{
		Enqueue(Operation(SAVESTATE_VERIFY, Path(), -1, callback, cbUserData));
	}

	void Rewind(Callback callback, void *cbUserData)
	{
		if (coreState == CoreState::CORE_RUNTIME_ERROR)
			Core_EnableStepping(true, "savestate.rewind", 0);
		Enqueue(Operation(SAVESTATE_REWIND, Path(), -1, callback, cbUserData));
	}

	void SaveScreenshot(const Path &filename, Callback callback, void *cbUserData)
	{
		Enqueue(Operation(SAVESTATE_SAVE_SCREENSHOT, filename, -1, callback, cbUserData));
	}

	bool CanRewind()
	{
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

	std::string GenerateFullDiscId(const Path &gameFilename) {
		std::string discId = g_paramSFO.GetValueString("DISC_ID");
		std::string discVer = g_paramSFO.GetValueString("DISC_VERSION");
		if (discId.empty()) {
			discId = g_paramSFO.GenerateFakeID(Path());
			discVer = "1.00";
		}
		return StringFromFormat("%s_%s", discId.c_str(), discVer.c_str());
	}

	Path GenerateSaveSlotFilename(const Path &gameFilename, int slot, const char *extension)
	{
		std::string filename = StringFromFormat("%s_%d.%s", GenerateFullDiscId(gameFilename).c_str(), slot, extension);
		return GetSysDirectory(DIRECTORY_SAVESTATE) / filename;
	}

	int GetCurrentSlot()
	{
		return g_Config.iCurrentStateSlot;
	}

	void PrevSlot()
	{
		g_Config.iCurrentStateSlot = (g_Config.iCurrentStateSlot - 1 + NUM_SLOTS) % NUM_SLOTS;
	}

	void NextSlot()
	{
		g_Config.iCurrentStateSlot = (g_Config.iCurrentStateSlot + 1) % NUM_SLOTS;
	}

	static void DeleteIfExists(const Path &fn) {
		// Just avoiding error messages.
		if (File::Exists(fn)) {
			File::Delete(fn);
		}
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

	void LoadSlot(const Path &gameFilename, int slot, Callback callback, void *cbUserData)
	{
		Path fn = GenerateSaveSlotFilename(gameFilename, slot, STATE_EXTENSION);
		if (!fn.empty()) {
			// This add only 1 extra state, should we just always enable it?
			if (g_Config.bEnableStateUndo) {
				Path backup = GetSysDirectory(DIRECTORY_SAVESTATE) / LOAD_UNDO_NAME;
				
				auto saveCallback = [=](Status status, std::string_view message, void *data) {
					if (status != Status::FAILURE) {
						DeleteIfExists(backup);
						File::Rename(backup.WithExtraExtension(".tmp"), backup);
						g_Config.sStateLoadUndoGame = GenerateFullDiscId(gameFilename);
						g_Config.Save("Saving config for savestate last load undo");
					} else {
						ERROR_LOG(Log::SaveState, "Saving load undo state failed: %.*s", (int)message.size(), message.data());
					}
					Load(fn, slot, callback, cbUserData);
				};

				if (!backup.empty()) {
					Save(backup.WithExtraExtension(".tmp"), LOAD_UNDO_SLOT, saveCallback, cbUserData);
				} else {
					ERROR_LOG(Log::SaveState, "Saving load undo state failed. Error in the file system.");
					Load(fn, slot, callback, cbUserData);
				}
			} else {
				Load(fn, slot, callback, cbUserData);
			}
		} else {
			if (callback) {
				auto sy = GetI18NCategory(I18NCat::SYSTEM);
				callback(Status::FAILURE, sy->T("Failed to load state. Error in the file system."), cbUserData);
			}
		}
	}

	bool UndoLoad(const Path &gameFilename, Callback callback, void *cbUserData)
	{
		if (g_Config.sStateLoadUndoGame != GenerateFullDiscId(gameFilename)) {
			if (callback) {
				auto sy = GetI18NCategory(I18NCat::SYSTEM);
				callback(Status::FAILURE, sy->T("Error: load undo state is from a different game"), cbUserData);
			}
			return false;
		}

		Path fn = GetSysDirectory(DIRECTORY_SAVESTATE) / LOAD_UNDO_NAME;
		if (!fn.empty()) {
			Load(fn, LOAD_UNDO_SLOT, callback, cbUserData);
			return true;
		} else {
			if (callback) {
				auto sy = GetI18NCategory(I18NCat::SYSTEM);
				callback(Status::FAILURE, sy->T("Failed to load state for load undo. Error in the file system."), cbUserData);
			}
			return false;
		}
	}

	void SaveSlot(const Path &gameFilename, int slot, Callback callback, void *cbUserData)
	{
		Path fn = GenerateSaveSlotFilename(gameFilename, slot, STATE_EXTENSION);
		Path fnUndo = GenerateSaveSlotFilename(gameFilename, slot, UNDO_STATE_EXTENSION);
		if (!fn.empty()) {
			Path shot = GenerateSaveSlotFilename(gameFilename, slot, SCREENSHOT_EXTENSION);
			auto renameCallback = [=](Status status, std::string_view message, void *data) {
				if (status != Status::FAILURE) {
					if (g_Config.bEnableStateUndo) {
						DeleteIfExists(fnUndo);
						RenameIfExists(fn, fnUndo);
						g_Config.sStateUndoLastSaveGame = GenerateFullDiscId(gameFilename);
						g_Config.iStateUndoLastSaveSlot = slot;
						g_Config.Save("Saving config for savestate last save undo");
					} else {
						DeleteIfExists(fn);
					}
					File::Rename(fn.WithExtraExtension(".tmp"), fn);
				}
				if (callback) {
					callback(status, message, data);
				}
			};
			// Let's also create a screenshot.
			if (g_Config.bEnableStateUndo) {
				Path shotUndo = GenerateSaveSlotFilename(gameFilename, slot, UNDO_SCREENSHOT_EXTENSION);
				DeleteIfExists(shotUndo);
				RenameIfExists(shot, shotUndo);
			}
			SaveScreenshot(shot, Callback(), 0);
			Save(fn.WithExtraExtension(".tmp"), slot, renameCallback, cbUserData);
		} else {
			if (callback) {
				auto sy = GetI18NCategory(I18NCat::SYSTEM);
				callback(Status::FAILURE, sy->T("Failed to save state. Error in the file system."), cbUserData);
			}
		}
	}

	bool UndoSaveSlot(const Path &gameFilename, int slot) {		
		Path fnUndo = GenerateSaveSlotFilename(gameFilename, slot, UNDO_STATE_EXTENSION);

		// Do nothing if there's no undo.
		if (File::Exists(fnUndo)) {
			Path fn = GenerateSaveSlotFilename(gameFilename, slot, STATE_EXTENSION);
			Path shot = GenerateSaveSlotFilename(gameFilename, slot, SCREENSHOT_EXTENSION);
			Path shotUndo = GenerateSaveSlotFilename(gameFilename, slot, UNDO_SCREENSHOT_EXTENSION);
			// Swap them so they can undo again to redo.  Mistakes happen.
			SwapIfExists(shotUndo, shot);
			SwapIfExists(fnUndo, fn);
			return true;
		}

		return false;
	}


	bool UndoLastSave(const Path &gameFilename) {
		if (g_Config.sStateUndoLastSaveGame != GenerateFullDiscId(gameFilename))
			return false;

		return UndoSaveSlot(gameFilename, g_Config.iStateUndoLastSaveSlot);
	}

	bool HasSaveInSlot(const Path &gameFilename, int slot)
	{
		Path fn = GenerateSaveSlotFilename(gameFilename, slot, STATE_EXTENSION);
		return File::Exists(fn);
	}

	bool HasUndoSaveInSlot(const Path &gameFilename, int slot)
	{
		Path fn = GenerateSaveSlotFilename(gameFilename, slot, UNDO_STATE_EXTENSION);
		return File::Exists(fn);
	}

	bool HasUndoLastSave(const Path &gameFilename) 
	{
		if (g_Config.sStateUndoLastSaveGame != GenerateFullDiscId(gameFilename))
			return false;

		return HasUndoSaveInSlot(gameFilename, g_Config.iStateUndoLastSaveSlot);
	}

	bool HasScreenshotInSlot(const Path &gameFilename, int slot)
	{
		Path fn = GenerateSaveSlotFilename(gameFilename, slot, SCREENSHOT_EXTENSION);
		return File::Exists(fn);
	}

	bool HasUndoLoad(const Path &gameFilename)
	{
		Path fn = GetSysDirectory(DIRECTORY_SAVESTATE) / LOAD_UNDO_NAME;
		return File::Exists(fn) && g_Config.sStateLoadUndoGame == GenerateFullDiscId(gameFilename);
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

	int GetNewestSlot(const Path &gameFilename) {
		int newestSlot = -1;
		tm newestDate = {0};
		for (int i = 0; i < NUM_SLOTS; i++) {
			Path fn = GenerateSaveSlotFilename(gameFilename, i, STATE_EXTENSION);
			if (File::Exists(fn)) {
				tm time;
				bool success = File::GetModifTime(fn, time);
				if (success && newestDate < time) {
					newestDate = time;
					newestSlot = i;
				}
			}
		}
		return newestSlot;
	}

	int GetOldestSlot(const Path &gameFilename) {
		int oldestSlot = -1;
		tm oldestDate = {0};
		for (int i = 0; i < NUM_SLOTS; i++) {
			Path fn = GenerateSaveSlotFilename(gameFilename, i, STATE_EXTENSION);
			if (File::Exists(fn)) {
				tm time;
				bool success = File::GetModifTime(fn, time);
				if (success && (!oldestDate || oldestDate > time)) {
					oldestDate = time;
					oldestSlot = i;
				}
			}
		}
		return oldestSlot;
	}

	std::string GetSlotDateAsString(const Path &gameFilename, int slot) {
		Path fn = GenerateSaveSlotFilename(gameFilename, slot, STATE_EXTENSION);
		if (File::Exists(fn)) {
			tm time;
			if (File::GetModifTime(fn, time)) {
				char buf[256];
				// TODO: Use local time format? Americans and some others might not like ISO standard :)
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
				return std::string(buf);
			}
		}
		return "";
	}

	std::vector<Operation> Flush()
	{
		std::lock_guard<std::mutex> guard(mutex);
		std::vector<Operation> copy = pending;
		pending.clear();

		return copy;
	}

	bool HandleLoadFailure(bool wasRewinding)
	{
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

	void Process()
	{
		rewindStates.Process();

		if (!needsProcess)
			return;
		needsProcess = false;

		if (!__KernelIsRunning())
		{
			ERROR_LOG(Log::SaveState, "Savestate failure: Unable to load without kernel, this should never happen.");
			return;
		}

		std::vector<Operation> operations = Flush();
		SaveStart state;

		for (size_t i = 0, n = operations.size(); i < n; ++i)
		{
			Operation &op = operations[i];
			CChunkFileReader::Error result;
			Status callbackResult;
			bool tempResult;
			std::string callbackMessage;
			std::string title;

			auto sc = GetI18NCategory(I18NCat::SCREEN);
			const char *i18nLoadFailure = sc->T_cstr("Failed to load state");
			const char *i18nSaveFailure = sc->T_cstr("Failed to save state");

			std::string slot_prefix = op.slot >= 0 ? StringFromFormat("(%d) ", op.slot + 1) : "";
			std::string errorString;

			switch (op.type)
			{
			case SAVESTATE_LOAD:
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

			case SAVESTATE_SAVE:
				INFO_LOG(Log::SaveState, "Saving state to %s", op.filename.c_str());
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

			case SAVESTATE_VERIFY:
				tempResult = CChunkFileReader::Verify(state) == CChunkFileReader::ERROR_NONE;
				callbackResult = tempResult ? Status::SUCCESS : Status::FAILURE;
				if (tempResult) {
					INFO_LOG(Log::SaveState, "Verified save state system");
				} else {
					ERROR_LOG(Log::SaveState, "Save state system verification failed");
				}
				break;

			case SAVESTATE_REWIND:
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

			case SAVESTATE_SAVE_SCREENSHOT:
			{
				int maxResMultiplier = 2;
				tempResult = TakeGameScreenshot(nullptr, op.filename, ScreenshotFormat::JPG, SCREENSHOT_DISPLAY, nullptr, nullptr, maxResMultiplier);
				callbackResult = tempResult ? Status::SUCCESS : Status::FAILURE;
				if (!tempResult) {
					ERROR_LOG(Log::SaveState, "Failed to take a screenshot for the savestate! %s", op.filename.c_str());
					if (screenshotFailures++ < SCREENSHOT_FAILURE_RETRIES) {
						// Requeue for next frame.
						SaveScreenshot(op.filename, op.callback, op.cbUserData);
					}
				} else {
					screenshotFailures = 0;
				}
				break;
			}
			default:
				ERROR_LOG(Log::SaveState, "Savestate failure: unknown operation type %d", op.type);
				callbackResult = Status::FAILURE;
				break;
			}

			if (op.callback)
				op.callback(callbackResult, callbackMessage, op.cbUserData);
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

	void Cleanup() {
		if (needsRestart) {
			PSP_Shutdown();
			std::string resetError;
			if (!PSP_Init(PSP_CoreParameter(), &resetError)) {
				ERROR_LOG(Log::Boot, "Error resetting: %s", resetError.c_str());
				// TODO: This probably doesn't clean up well enough.
				Core_Stop();
				return;
			}
			System_Notify(SystemNotification::BOOT_DONE);
			System_Notify(SystemNotification::DISASSEMBLY);
			needsRestart = false;
		}
	}

	void Init()
	{
		// Make sure there's a directory for save slots
		File::CreateFullPath(GetSysDirectory(DIRECTORY_SAVESTATE));

		std::lock_guard<std::mutex> guard(mutex);
		rewindStates.Clear();

		hasLoadedState = false;
		saveStateGeneration = 0;
		saveDataGeneration = 0;
		lastSaveDataGeneration = 0;
		saveStateInitialGitVersion.clear();
	}

	void Shutdown()
	{
		std::lock_guard<std::mutex> guard(mutex);
		rewindStates.Clear();
	}
}
