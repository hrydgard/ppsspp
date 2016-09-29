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

#include "base/mutex.h"
#include "base/timeutil.h"
#include "i18n/i18n.h"
#include "thread/thread.h"
#include "thread/threadutil.h"

#include "Common/FileUtil.h"
#include "Common/ChunkFile.h"

#include "Core/SaveState.h"
#include "Core/Config.h"
#include "Core/Core.h"
#include "Core/CoreTiming.h"
#include "Core/Host.h"
#include "Core/Screenshot.h"
#include "Core/System.h"
#include "Core/FileSystems/MetaFileSystem.h"
#include "Core/ELF/ParamSFO.h"
#include "Core/HLE/HLE.h"
#include "Core/HLE/sceDisplay.h"
#include "Core/HLE/ReplaceTables.h"
#include "Core/HLE/sceKernel.h"
#include "Core/MemMap.h"
#include "Core/MIPS/MIPS.h"
#include "Core/MIPS/JitCommon/JitBlockCache.h"
#include "HW/MemoryStick.h"
#include "GPU/GPUState.h"

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
		Operation(OperationType t, const std::string &f, Callback cb, void *cbUserData_)
			: type(t), filename(f), callback(cb), cbUserData(cbUserData_)
		{
		}

		OperationType type;
		std::string filename;
		Callback callback;
		void *cbUserData;
	};

	CChunkFileReader::Error SaveToRam(std::vector<u8> &data) {
		SaveStart state;
		size_t sz = CChunkFileReader::MeasurePtr(state);
		if (data.size() < sz)
			data.resize(sz);
		return CChunkFileReader::SavePtr(&data[0], state);
	}

	CChunkFileReader::Error LoadFromRam(std::vector<u8> &data) {
		SaveStart state;
		return CChunkFileReader::LoadPtr(&data[0], state);
	}

	struct StateRingbuffer
	{
		StateRingbuffer(int size) : first_(0), next_(0), size_(size), base_(-1)
		{
			states_.resize(size);
			baseMapping_.resize(size);
		}

		CChunkFileReader::Error Save()
		{
			lock_guard guard(lock_);

			int n = next_++ % size_;
			if ((next_ % size_) == first_)
				++first_;

			static std::vector<u8> buffer;
			std::vector<u8> *compressBuffer = &buffer;
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
				err = SaveToRam(buffer);

			if (err == CChunkFileReader::ERROR_NONE)
				ScheduleCompress(&states_[n], compressBuffer, &bases_[base_]);
			else
				states_[n].clear();
			baseMapping_[n] = base_;
			return err;
		}

		CChunkFileReader::Error Restore()
		{
			lock_guard guard(lock_);

			// No valid states left.
			if (Empty())
				return CChunkFileReader::ERROR_BAD_FILE;

			int n = (--next_ + size_) % size_;
			if (states_[n].empty())
				return CChunkFileReader::ERROR_BAD_FILE;

			static std::vector<u8> buffer;
			Decompress(buffer, states_[n], bases_[baseMapping_[n]]);
			return LoadFromRam(buffer);
		}

		void ScheduleCompress(std::vector<u8> *result, const std::vector<u8> *state, const std::vector<u8> *base)
		{
			auto th = new std::thread([=]{
				setCurrentThreadName("SaveStateCompress");
				Compress(*result, *state, *base);
			});
			th->detach();
		}

		void Compress(std::vector<u8> &result, const std::vector<u8> &state, const std::vector<u8> &base)
		{
			lock_guard guard(lock_);
			// Bail if we were cleared before locking.
			if (first_ == 0 && next_ == 0)
				return;

			result.clear();
			for (size_t i = 0; i < state.size(); i += BLOCK_SIZE)
			{
				int blockSize = std::min(BLOCK_SIZE, (int)(state.size() - i));
				if (i + blockSize > base.size() || memcmp(&state[i], &base[i], blockSize) != 0)
				{
					result.push_back(1);
					result.insert(result.end(), state.begin() + i, state.begin() +i + blockSize);
				}
				else
					result.push_back(0);
			}
		}

		void Decompress(std::vector<u8> &result, const std::vector<u8> &compressed, const std::vector<u8> &base)
		{
			lock_guard guard(lock_);
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
					basePos += blockSize;
				}
			}
		}

		void Clear()
		{
			// This lock is mainly for shutdown.
			lock_guard guard(lock_);
			first_ = 0;
			next_ = 0;
		}

		bool Empty() const
		{
			return next_ == first_;
		}

		static const int BLOCK_SIZE;
		// TODO: Instead, based on size of compressed state?
		static const int BASE_USAGE_INTERVAL;

		typedef std::vector<u8> StateBuffer;

		int first_;
		int next_;
		int size_;

		std::vector<StateBuffer> states_;
		StateBuffer bases_[2];
		std::vector<int> baseMapping_;
		recursive_mutex lock_;

		int base_;
		int baseUsage_;
	};

	static bool needsProcess = false;
	static std::vector<Operation> pending;
	static recursive_mutex mutex;
	static bool hasLoadedState = false;

	// TODO: Should this be configurable?
	static const int REWIND_NUM_STATES = 20;
	static StateRingbuffer rewindStates(REWIND_NUM_STATES);
	// TODO: Any reason for this to be configurable?
	const static float rewindMaxWallFrequency = 1.0f;
	static float rewindLastTime = 0.0f;
	const int StateRingbuffer::BLOCK_SIZE = 8192;
	const int StateRingbuffer::BASE_USAGE_INTERVAL = 15;

	void SaveStart::DoState(PointerWrap &p)
	{
		auto s = p.Section("SaveStart", 1);
		if (!s)
			return;

		// Gotta do CoreTiming first since we'll restore into it.
		CoreTiming::DoState(p);

		// Memory is a bit tricky when jit is enabled, since there's emuhacks in it.
		auto savedReplacements = SaveAndClearReplacements();
		if (MIPSComp::jit && p.mode == p.MODE_WRITE)
		{
			std::vector<u32> savedBlocks;
			savedBlocks = MIPSComp::jit->SaveAndClearEmuHackOps();
			Memory::DoState(p);
			MIPSComp::jit->RestoreSavedEmuHackOps(savedBlocks);
		}
		else
			Memory::DoState(p);
		RestoreSavedReplacements(savedReplacements);

		MemoryStick_DoState(p);
		currentMIPS->DoState(p);
		HLEDoState(p);
		__KernelDoState(p);
		// Kernel object destructors might close open files, so do the filesystem last.
		pspFileSystem.DoState(p);
	}

	void Enqueue(SaveState::Operation op)
	{
		lock_guard guard(mutex);
		pending.push_back(op);

		// Don't actually run it until next frame.
		// It's possible there might be a duplicate but it won't hurt us.
		needsProcess = true;
		Core_UpdateSingleStep();
	}

	void Load(const std::string &filename, Callback callback, void *cbUserData)
	{
		Enqueue(Operation(SAVESTATE_LOAD, filename, callback, cbUserData));
	}

	void Save(const std::string &filename, Callback callback, void *cbUserData)
	{
		Enqueue(Operation(SAVESTATE_SAVE, filename, callback, cbUserData));
	}

	void Verify(Callback callback, void *cbUserData)
	{
		Enqueue(Operation(SAVESTATE_VERIFY, std::string(""), callback, cbUserData));
	}

	void Rewind(Callback callback, void *cbUserData)
	{
		Enqueue(Operation(SAVESTATE_REWIND, std::string(""), callback, cbUserData));
	}

	void SaveScreenshot(const std::string &filename, Callback callback, void *cbUserData)
	{
		Enqueue(Operation(SAVESTATE_SAVE_SCREENSHOT, filename, callback, cbUserData));
	}

	bool CanRewind()
	{
		return !rewindStates.Empty();
	}

	// Slot utilities

	std::string AppendSlotTitle(const std::string &filename, const std::string &title) {
		if (!endsWith(filename, std::string(".") + STATE_EXTENSION)) {
			return title + " (" + filename + ")";
		}

		// Usually these are slots, let's check the slot # after the last '_'.
		size_t slotNumPos = filename.find_last_of('_');
		if (slotNumPos == filename.npos) {
			return title + " (" + filename + ")";
		}

		const size_t extLength = strlen(STATE_EXTENSION) + 1;
		// If we take out the extension, '_', etc. we should be left with only a single digit.
		if (slotNumPos + 1 + extLength != filename.length() - 1) {
			return title + " (" + filename + ")";
		}

		std::string slot = filename.substr(slotNumPos + 1, 1);
		if (slot[0] < '0' || slot[0] > '8') {
			return title + " (" + filename + ")";
		}

		// Change from zero indexed to human friendly.
		slot[0]++;
		return title + " (" + slot + ")";
	}

	std::string GetTitle(const std::string &filename) {
		std::string title;
		if (CChunkFileReader::GetFileTitle(filename, &title) == CChunkFileReader::ERROR_NONE) {
			if (title.empty()) {
				return File::GetFilename(filename);
			}

			return AppendSlotTitle(filename, title);
		}

		// The file can't be loaded - let's note that.
		I18NCategory *sy = GetI18NCategory("System");
		return File::GetFilename(filename) + " " + sy->T("(broken)");
	}

	std::string GenerateSaveSlotFilename(const std::string &gameFilename, int slot, const char *extension)
	{
		std::string discId = g_paramSFO.GetValueString("DISC_ID");
		std::string fullDiscId;
		if (discId.size()) {
			fullDiscId = StringFromFormat("%s_%s",
				g_paramSFO.GetValueString("DISC_ID").c_str(),
				g_paramSFO.GetValueString("DISC_VERSION").c_str());
		} else {
			// Okay, no discId. Probably homebrew, let's use the last part of the path name.
			if (File::IsDirectory(gameFilename)) {
				// EBOOT.PBP directory, most likely.
				std::string path = gameFilename;
				size_t slash = path.rfind('/');  // Always '/', not '\\', as we're in a virtual directory
				if (slash != std::string::npos && slash < path.size() - 1)
					path = path.substr(slash + 1);
				fullDiscId = path;
			} else {
				// Probably a loose elf.
				std::string fn = File::GetFilename(gameFilename);
				size_t dot = fn.rfind('.');
				if (dot != std::string::npos) {
					fullDiscId = fn.substr(0, dot);
				} else {
					fullDiscId = "elf";  // Fallback
				}
			}
		}

		std::string temp = StringFromFormat("ms0:/PSP/PPSSPP_STATE/%s_%i.%s", fullDiscId.c_str(), slot, extension);
		std::string hostPath;
		if (pspFileSystem.GetHostPath(temp, hostPath)) {
			return hostPath;
		} else {
			return "";
		}
	}

	int GetCurrentSlot()
	{
		return g_Config.iCurrentStateSlot;
	}

	void NextSlot()
	{
		g_Config.iCurrentStateSlot = (g_Config.iCurrentStateSlot + 1) % NUM_SLOTS;
	}

	void LoadSlot(const std::string &gameFilename, int slot, Callback callback, void *cbUserData)
	{
		std::string fn = GenerateSaveSlotFilename(gameFilename, slot, STATE_EXTENSION);
		if (!fn.empty()) {
			Load(fn, callback, cbUserData);
		} else {
			I18NCategory *sy = GetI18NCategory("System");
			if (callback)
				callback(false, sy->T("Failed to load state. Error in the file system."), cbUserData);
		}
	}

	void SaveSlot(const std::string &gameFilename, int slot, Callback callback, void *cbUserData)
	{
		std::string fn = GenerateSaveSlotFilename(gameFilename, slot, STATE_EXTENSION);
		std::string shot = GenerateSaveSlotFilename(gameFilename, slot, SCREENSHOT_EXTENSION);
		if (!fn.empty()) {
			auto renameCallback = [=](bool status, const std::string &message, void *data) {
				if (status) {
					if (File::Exists(fn)) {
						File::Delete(fn);
					}
					File::Rename(fn + ".tmp", fn);
				}
				if (callback) {
					callback(status, message, data);
				}
			};
			// Let's also create a screenshot.
			SaveScreenshot(shot, Callback(), 0);
			Save(fn + ".tmp", renameCallback, cbUserData);
		} else {
			I18NCategory *sy = GetI18NCategory("System");
			if (callback)
				callback(false, sy->T("Failed to save state. Error in the file system."), cbUserData);
		}
	}

	bool HasSaveInSlot(const std::string &gameFilename, int slot)
	{
		std::string fn = GenerateSaveSlotFilename(gameFilename, slot, STATE_EXTENSION);
		return File::Exists(fn);
	}

	bool HasScreenshotInSlot(const std::string &gameFilename, int slot)
	{
		std::string fn = GenerateSaveSlotFilename(gameFilename, slot, SCREENSHOT_EXTENSION);
		return File::Exists(fn);
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

	int GetNewestSlot(const std::string &gameFilename) {
		int newestSlot = -1;
		tm newestDate = {0};
		for (int i = 0; i < NUM_SLOTS; i++) {
			std::string fn = GenerateSaveSlotFilename(gameFilename, i, STATE_EXTENSION);
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

	std::string GetSlotDateAsString(const std::string &gameFilename, int slot) {
		std::string fn = GenerateSaveSlotFilename(gameFilename, slot, STATE_EXTENSION);
		if (File::Exists(fn)) {
			tm time;
			if (File::GetModifTime(fn, time)) {
				char buf[256];
				// TODO: Use local time format? Americans and some others might not like ISO standard :)
				strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &time);
				return std::string(buf);
			}
		}
		return "";
	}

	std::vector<Operation> Flush()
	{
		lock_guard guard(mutex);
		std::vector<Operation> copy = pending;
		pending.clear();

		return copy;
	}

	bool HandleFailure()
	{
		// Okay, first, let's give the rewind state a shot - maybe we can at least not reset entirely.
		// Even if this was a rewind, maybe we can still load a previous one.
		CChunkFileReader::Error result;
		do
			result = rewindStates.Restore();
		while (result == CChunkFileReader::ERROR_BROKEN_STATE);

		if (result == CChunkFileReader::ERROR_NONE) {
			return true;
		}

		// We tried, our only remaining option is to reset the game.
		PSP_Shutdown();
		std::string resetError;
		if (!PSP_Init(PSP_CoreParameter(), &resetError))
		{
			ERROR_LOG(BOOT, "Error resetting: %s", resetError.c_str());
			// TODO: This probably doesn't clean up well enough.
			Core_Stop();
			return false;
		}
		host->BootDone();
		host->UpdateDisassembly();
		return false;
	}

	static inline void CheckRewindState()
	{
		if (gpuStats.numFlips % g_Config.iRewindFlipFrequency != 0)
			return;

		// For fast-forwarding, otherwise they may be useless and too close.
		time_update();
		float diff = time_now() - rewindLastTime;
		if (diff < rewindMaxWallFrequency)
			return;

		rewindLastTime = time_now();
		DEBUG_LOG(BOOT, "saving rewind state");
		rewindStates.Save();
	}

	bool HasLoadedState()
	{
		return hasLoadedState;
	}

	void Process()
	{
#ifndef MOBILE_DEVICE
		if (g_Config.iRewindFlipFrequency != 0 && gpuStats.numFlips != 0)
			CheckRewindState();
#endif

		if (!needsProcess)
			return;
		needsProcess = false;

		if (!__KernelIsRunning())
		{
			ERROR_LOG(COMMON, "Savestate failure: Unable to load without kernel, this should never happen.");
			return;
		}

		std::vector<Operation> operations = Flush();
		SaveStart state;

		for (size_t i = 0, n = operations.size(); i < n; ++i)
		{
			Operation &op = operations[i];
			CChunkFileReader::Error result;
			bool callbackResult;
			std::string callbackMessage;
			std::string reason;

			I18NCategory *sc = GetI18NCategory("Screen");
			const char *i18nLoadFailure = sc->T("Load savestate failed", "");
			const char *i18nSaveFailure = sc->T("Save State Failed", "");
			if (strlen(i18nLoadFailure) == 0)
				i18nLoadFailure = sc->T("Failed to load state");
			if (strlen(i18nSaveFailure) == 0)
				i18nSaveFailure = sc->T("Failed to save state");

			switch (op.type)
			{
			case SAVESTATE_LOAD:
				INFO_LOG(COMMON, "Loading state from %s", op.filename.c_str());
				result = CChunkFileReader::Load(op.filename, PPSSPP_GIT_VERSION, state, &reason);
				if (result == CChunkFileReader::ERROR_NONE) {
					callbackMessage = sc->T("Loaded State");
					callbackResult = true;
					hasLoadedState = true;
				} else if (result == CChunkFileReader::ERROR_BROKEN_STATE) {
					HandleFailure();
					callbackMessage = i18nLoadFailure;
					ERROR_LOG(COMMON, "Load state failure: %s", reason.c_str());
					callbackResult = false;
				} else {
					callbackMessage = sc->T(reason.c_str(), i18nLoadFailure);
					callbackResult = false;
				}
				break;

			case SAVESTATE_SAVE:
				INFO_LOG(COMMON, "Saving state to %s", op.filename.c_str());
				result = CChunkFileReader::Save(op.filename, g_paramSFO.GetValueString("TITLE"), PPSSPP_GIT_VERSION, state);
				if (result == CChunkFileReader::ERROR_NONE) {
					callbackMessage = sc->T("Saved State");
					callbackResult = true;
				} else if (result == CChunkFileReader::ERROR_BROKEN_STATE) {
					HandleFailure();
					callbackMessage = i18nSaveFailure;
					ERROR_LOG(COMMON, "Save state failure: %s", reason.c_str());
					callbackResult = false;
				} else {
					callbackMessage = i18nSaveFailure;
					callbackResult = false;
				}
				break;

			case SAVESTATE_VERIFY:
				callbackResult = CChunkFileReader::Verify(state) == CChunkFileReader::ERROR_NONE;
				if (callbackResult) {
					INFO_LOG(COMMON, "Verified save state system");
				} else {
					ERROR_LOG(COMMON, "Save state system verification failed");
				}
				break;

			case SAVESTATE_REWIND:
				INFO_LOG(COMMON, "Rewinding to recent savestate snapshot");
				result = rewindStates.Restore();
				if (result == CChunkFileReader::ERROR_NONE) {
					callbackMessage = sc->T("Loaded State");
					callbackResult = true;
					hasLoadedState = true;
				} else if (result == CChunkFileReader::ERROR_BROKEN_STATE) {
					// Cripes.  Good news is, we might have more.  Let's try those too, better than a reset.
					if (HandleFailure()) {
						// Well, we did rewind, even if too much...
						callbackMessage = sc->T("Loaded State");
						callbackResult = true;
						hasLoadedState = true;
					} else {
						callbackMessage = i18nLoadFailure;
						callbackResult = false;
					}
				} else {
					callbackMessage = i18nLoadFailure;
					callbackResult = false;
				}
				break;

			case SAVESTATE_SAVE_SCREENSHOT:
				callbackResult = TakeGameScreenshot(op.filename.c_str(), SCREENSHOT_JPG, SCREENSHOT_DISPLAY);
				if (!callbackResult) {
					ERROR_LOG(COMMON, "Failed to take a screenshot for the savestate! %s", op.filename.c_str());
				}
				break;

			default:
				ERROR_LOG(COMMON, "Savestate failure: unknown operation type %d", op.type);
				callbackResult = false;
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

	void Init()
	{
		// Make sure there's a directory for save slots
		pspFileSystem.MkDir("ms0:/PSP/PPSSPP_STATE");

		lock_guard guard(mutex);
		rewindStates.Clear();

		hasLoadedState = false;
	}

	void Shutdown()
	{
		lock_guard guard(mutex);
		rewindStates.Clear();
	}
}
