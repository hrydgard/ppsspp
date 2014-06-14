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

#include <vector>

#include "base/timeutil.h"
#include "i18n/i18n.h"

#include "Common/StdMutex.h"
#include "Common/FileUtil.h"
#include "Common/ChunkFile.h"

#include "Core/SaveState.h"
#include "Core/Config.h"
#include "Core/Core.h"
#include "Core/CoreTiming.h"
#include "Core/Host.h"
#include "Core/System.h"
#include "Core/FileSystems/MetaFileSystem.h"
#include "Core/ELF/ParamSFO.h"
#include "Core/HLE/HLE.h"
#include "Core/HLE/sceDisplay.h"
#include "Core/HLE/ReplaceTables.h"
#include "Core/HLE/sceKernel.h"
#include "Core/MemMap.h"
#include "Core/MIPS/MIPS.h"
#include "Core/MIPS/JitCommon/JitCommon.h"
#include "HW/MemoryStick.h"
#include "GPU/GPUState.h"
#include "UI/OnScreenDisplay.h"

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
				Compress(states_[n], *compressBuffer, bases_[base_]);
			else
				states_[n].clear();
			baseMapping_[n] = base_;
			return err;
		}

		CChunkFileReader::Error Restore()
		{
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

		void Compress(std::vector<u8> &result, const std::vector<u8> &state, const std::vector<u8> &base)
		{
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
			first_ = 0;
			next_ = 0;
		}

		bool Empty()
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
		int base_;
		int baseUsage_;
	};

	static bool needsProcess = false;
	static std::vector<Operation> pending;
	static std::recursive_mutex mutex;
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
			auto blockCache = MIPSComp::jit->GetBlockCache();
			auto savedBlocks = blockCache->SaveAndClearEmuHackOps();
			Memory::DoState(p);
			blockCache->RestoreSavedEmuHackOps(savedBlocks);
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
		std::lock_guard<std::recursive_mutex> guard(mutex);
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

	bool CanRewind()
	{
		return !rewindStates.Empty();
	}

	static const char *STATE_EXTENSION = "ppst";
	static const char *SCREENSHOT_EXTENSION = "jpg";
	// Slot utilities

	std::string GenerateSaveSlotFilename(int slot, const char *extension)
	{
		char discID[256];
		char temp[256];
		sprintf(discID, "%s_%s",
			g_paramSFO.GetValueString("DISC_ID").c_str(),
			g_paramSFO.GetValueString("DISC_VERSION").c_str());
		sprintf(temp, "ms0:/PSP/PPSSPP_STATE/%s_%i.%s", discID, slot, extension);
		std::string hostPath;
		if (pspFileSystem.GetHostPath(std::string(temp), hostPath)) {
			return hostPath;
		} else {
			return "";
		}
	}

	void NextSlot()
	{
		I18NCategory *sy = GetI18NCategory("System");
		g_Config.iCurrentStateSlot = (g_Config.iCurrentStateSlot + 1) % SaveState::SAVESTATESLOTS;
		char msg[30];
		sprintf(msg, "%s: %d", sy->T("Savestate Slot"), g_Config.iCurrentStateSlot + 1);
		osm.Show(msg);
	}

	void LoadSlot(int slot, Callback callback, void *cbUserData)
	{
		std::string fn = GenerateSaveSlotFilename(slot, STATE_EXTENSION);
		if (!fn.empty()) {
			Load(fn, callback, cbUserData);
		} else {
			I18NCategory *s = GetI18NCategory("Screen");
			osm.Show("Failed to load state. Error in the file system.", 2.0);
			if (callback)
				(*callback)(false, cbUserData);
		}
	}

	void SaveSlot(int slot, Callback callback, void *cbUserData)
	{
		std::string fn = GenerateSaveSlotFilename(slot, STATE_EXTENSION);
		if (!fn.empty()) {
			Save(fn, callback, cbUserData);
		} else {
			I18NCategory *s = GetI18NCategory("Screen");
			osm.Show("Failed to save state. Error in the file system.", 2.0);
			if (callback)
				(*callback)(false, cbUserData);
		}
	}

	bool HasSaveInSlot(int slot)
	{
		std::string fn = GenerateSaveSlotFilename(slot, STATE_EXTENSION);
		return File::Exists(fn);
	}

	bool HasScreenshotInSlot(int slot)
	{
		std::string fn = GenerateSaveSlotFilename(slot, SCREENSHOT_EXTENSION);
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

	int GetNewestSlot() {
		int newestSlot = -1;
		tm newestDate = {0};
		for (int i = 0; i < SAVESTATESLOTS; i++) {
			std::string fn = GenerateSaveSlotFilename(i, STATE_EXTENSION);
			if (File::Exists(fn)) {
				tm time = File::GetModifTime(fn);
				if (newestDate < time) {
					newestDate = time;
					newestSlot = i;
				}
			}
		}
		return newestSlot;
	}


	std::vector<Operation> Flush()
	{
		std::lock_guard<std::recursive_mutex> guard(mutex);
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
			std::string reason;

			I18NCategory *s = GetI18NCategory("Screen");
			// I couldn't stand the inconsistency.  But trying not to break old lang files.
			const char *i18nLoadFailure = s->T("Load savestate failed", "");
			const char *i18nSaveFailure = s->T("Save State Failed", "");
			if (strlen(i18nLoadFailure) == 0)
				i18nLoadFailure = s->T("Failed to load state");
			if (strlen(i18nSaveFailure) == 0)
				i18nSaveFailure = s->T("Failed to save state");

			switch (op.type)
			{
			case SAVESTATE_LOAD:
				INFO_LOG(COMMON, "Loading state from %s", op.filename.c_str());
				result = CChunkFileReader::Load(op.filename, REVISION, PPSSPP_GIT_VERSION, state, &reason);
				if (result == CChunkFileReader::ERROR_NONE) {
					osm.Show(s->T("Loaded State"), 2.0);
					callbackResult = true;
					hasLoadedState = true;
				} else if (result == CChunkFileReader::ERROR_BROKEN_STATE) {
					HandleFailure();
					osm.Show(i18nLoadFailure, 2.0);
					ERROR_LOG(COMMON, "Load state failure: %s", reason.c_str());
					callbackResult = false;
				} else {
					osm.Show(s->T(reason.c_str(), i18nLoadFailure), 2.0);
					callbackResult = false;
				}
				break;

			case SAVESTATE_SAVE:
				INFO_LOG(COMMON, "Saving state to %s", op.filename.c_str());
				result = CChunkFileReader::Save(op.filename, REVISION, PPSSPP_GIT_VERSION, state);
				if (result == CChunkFileReader::ERROR_NONE) {
					osm.Show(s->T("Saved State"), 2.0);
					callbackResult = true;
				} else if (result == CChunkFileReader::ERROR_BROKEN_STATE) {
					HandleFailure();
					osm.Show(i18nSaveFailure, 2.0);
					ERROR_LOG(COMMON, "Save state failure: %s", reason.c_str());
					callbackResult = false;
				} else {
					osm.Show(i18nSaveFailure, 2.0);
					callbackResult = false;
				}
				break;

			case SAVESTATE_VERIFY:
				INFO_LOG(COMMON, "Verifying save state system");
				callbackResult = CChunkFileReader::Verify(state) == CChunkFileReader::ERROR_NONE;
				break;

			case SAVESTATE_REWIND:
				INFO_LOG(COMMON, "Rewinding to recent savestate snapshot");
				result = rewindStates.Restore();
				if (result == CChunkFileReader::ERROR_NONE) {
					osm.Show(s->T("Loaded State"), 2.0);
					callbackResult = true;
					hasLoadedState = true;
				} else if (result == CChunkFileReader::ERROR_BROKEN_STATE) {
					// Cripes.  Good news is, we might have more.  Let's try those too, better than a reset.
					if (HandleFailure()) {
						// Well, we did rewind, even if too much...
						osm.Show(s->T("Loaded State"), 2.0);
						callbackResult = true;
						hasLoadedState = true;
					} else {
						osm.Show(i18nLoadFailure, 2.0);
						callbackResult = false;
					}
				} else {
					osm.Show(i18nLoadFailure, 2.0);
					callbackResult = false;
				}
				break;

			default:
				ERROR_LOG(COMMON, "Savestate failure: unknown operation type %d", op.type);
				callbackResult = false;
				break;
			}

			if (op.callback)
				op.callback(callbackResult, op.cbUserData);
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

		std::lock_guard<std::recursive_mutex> guard(mutex);
		rewindStates.Clear();

		hasLoadedState = false;
	}
}
