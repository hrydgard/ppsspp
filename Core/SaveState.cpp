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

#include "Common/StdMutex.h"
#include "Common/FileUtil.h"

#include "Core/SaveState.h"
#include "Core/Config.h"
#include "Core/Core.h"
#include "Core/CoreTiming.h"
#include "Core/Host.h"
#include "Core/System.h"
#include "Core/HLE/HLE.h"
#include "Core/HLE/sceKernel.h"
#include "HW/MemoryStick.h"
#include "Core/MemMap.h"
#include "Core/MIPS/MIPS.h"
#include "Core/MIPS/JitCommon/JitCommon.h"
#include "UI/OnScreenDisplay.h"
#include "i18n/i18n.h"

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

	static bool needsProcess = false;
	static std::vector<Operation> pending;
	static std::recursive_mutex mutex;

	void SaveStart::DoState(PointerWrap &p)
	{
		auto s = p.Section("SaveStart", 1);
		if (!s)
			return;

		// Gotta do CoreTiming first since we'll restore into it.
		CoreTiming::DoState(p);

		// Memory is a bit tricky when jit is enabled, since there's emuhacks in it.
		if (MIPSComp::jit && p.mode == p.MODE_WRITE)
		{
			auto blocks = MIPSComp::jit->GetBlockCache();
			auto saved = blocks->SaveAndClearEmuHackOps();
			Memory::DoState(p);
			blocks->RestoreSavedEmuHackOps(saved);
		}
		else
			Memory::DoState(p);

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


	// Slot utilities

	std::string GenerateSaveSlotFilename(int slot)
	{
		char discID[256];
		char temp[256];
		sprintf(discID, "%s_%s",
			g_paramSFO.GetValueString("DISC_ID").c_str(),
			g_paramSFO.GetValueString("DISC_VERSION").c_str());
		sprintf(temp, "ms0:/PSP/PPSSPP_STATE/%s_%i.ppst", discID, slot);
		std::string hostPath;
		if (pspFileSystem.GetHostPath(std::string(temp), hostPath)) {
			return hostPath;
		} else {
			return "";
		}
	}

	void LoadSlot(int slot, Callback callback, void *cbUserData)
	{
		std::string fn = GenerateSaveSlotFilename(slot);
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
		std::string fn = GenerateSaveSlotFilename(slot);
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
		std::string fn = GenerateSaveSlotFilename(slot);
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

	int GetMostRecentSaveSlot() {
		int newestSlot = -1;
		tm newestDate = {0};
		for (int i = 0; i < SAVESTATESLOTS; i++) {
			std::string fn = GenerateSaveSlotFilename(i);
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

	void HandleFailure()
	{
		PSP_Shutdown();
		std::string resetError;
		if (!PSP_Init(PSP_CoreParameter(), &resetError))
		{
			ERROR_LOG(BOOT, "Error resetting: %s", resetError.c_str());
			// TODO: This probably doesn't clean up well enough.
			Core_Stop();
			return;
		}
		host->BootDone();
		host->UpdateDisassembly();
	}

	void Process()
	{
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

			switch (op.type)
			{
			case SAVESTATE_LOAD:
				INFO_LOG(COMMON, "Loading state from %s", op.filename.c_str());
				result = CChunkFileReader::Load(op.filename, REVISION, PPSSPP_GIT_VERSION, state, &reason);
				if (result == CChunkFileReader::ERROR_NONE) {
					osm.Show(s->T("Loaded State"), 2.0);
					callbackResult = true;
				} else if (result == CChunkFileReader::ERROR_BROKEN_STATE) {
					HandleFailure();
					callbackResult = false;
				} else {
					osm.Show(s->T(reason.c_str(), "Load savestate failed"), 2.0);
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
					callbackResult = false;
				} else {
					osm.Show(s->T("Save State Failed"), 2.0);
					callbackResult = false;
				}
				break;

			case SAVESTATE_VERIFY:
				INFO_LOG(COMMON, "Verifying save state system");
				callbackResult = CChunkFileReader::Verify(state) == CChunkFileReader::ERROR_NONE;
				break;

			default:
				ERROR_LOG(COMMON, "Savestate failure: unknown operation type %d", op.type);
				callbackResult = false;
				break;
			}

			if (op.callback)
				op.callback(callbackResult, op.cbUserData);
		}
	}

	void Init()
	{
		// Make sure there's a directory for save slots
		pspFileSystem.MkDir("ms0:/PSP/PPSSPP_STATE");

		std::lock_guard<std::recursive_mutex> guard(mutex);
	}
}
