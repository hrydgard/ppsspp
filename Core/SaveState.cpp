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
#include <atomic>
#include <condition_variable>
#include <deque>
#include <map>
#include <memory>
#include <vector>
#include <thread>
#include <mutex>
#include <string>
#include <set>

#include "Common/Data/Text/I18n.h"
#include "Common/Thread/ThreadUtil.h"
#include "Common/Data/Text/Parsers.h"
#include "Common/System/System.h"
#include "Common/System/OSD.h"

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
#include "Core/HLE/sceKernelModule.h"
#include "Core/HLE/sceUtility.h"
#include "Core/HLE/sceSas.h"
#include "Core/HLE/sceIo.h"
#include "Core/MemMap.h"
#include "GPU/GPU.h"
#include "GPU/GPUCommon.h"
#include "Core/MIPS/JitCommon/JitBlockCache.h"
#include "Core/MIPS/JitCommon/JitCommon.h"
#include "Core/RetroAchievements.h"
#include "HW/MemoryStick.h"
#include "Core/HW/MMIO.h"
#include "Core/MIPS/MeCore.h"

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
// Namespaced the way UNDO_STATE_EXTENSION is, so a stray "<prefix>_<slot>.txt" someone happened to
// leave in the savestate folder isn't mistaken for a slot's name.
static const char * const NAME_EXTENSION = "name.txt";

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
// Set when a slot save finishes. That's on the emu thread, so the rescan is left to the next reader.
static std::atomic<bool> g_filesStale;

enum class OperationType {
	Save,
	Load,
	Verify,
	Rewind,
};

// A load's file, read on the worker. Shared between the operation and the job.
struct PendingRead {
	std::atomic<bool> done{ false };
	CChunkFileReader::Error error = CChunkFileReader::ERROR_NONE;
	std::unique_ptr<u8[]> buffer;
	size_t size = 0;
	std::string gitVersion;
	// "PPSSPP <version>" from the header, for the failure message.
	std::string savedBy;
};

// A save from when it's asked for until its file is written.
struct SaveRecord {
	Path path;
	// How many loads had been applied when its snapshot was taken, or -1 before that.
	int loadCount = -1;
	// Callbacks of repeats of it (a save key pressed twice). They hear its result, with no message.
	std::vector<Callback> repeats;
};

struct Operation {
	// The slot number is for visual purposes only. Set to -1 for operations where we don't display a message for example.
	Operation(OperationType t, const Path &f, int slot_, Callback cb)
		: type(t), path(f), callback(cb), slot(slot_) {}

	OperationType type;
	Path path;
	Callback callback;
	int slot;
	// For slot operations, the game prefix. A slot save writes to a temporary file and moves the
	// slot's old state to its undo name. A slot load keeps the state it replaces, for load undo.
	std::string slotGamePrefix;
	// Set once the operation has started: a load or rewind waits at the head of the queue until its
	// worker part is done.
	bool started = false;
	std::shared_ptr<PendingRead> read;
	// For a save. Its fields are under mutex.
	std::shared_ptr<SaveRecord> save;
};

// Front to back, each operation's emu thread part runs in turn. A load or rewind that's still
// waiting for the worker holds up the operations behind it.
static std::deque<Operation> g_pendingOperations;

// Saves asked for and not yet written, oldest first. Under mutex.
static std::vector<std::shared_ptr<SaveRecord>> g_pendingSaves;
// Loads (and rewinds) applied, so a save can tell whether one came after its snapshot.
static std::atomic<int> g_loadCount;
// Save snapshots in memory, waiting to be compressed and written. Each is a whole state.
static std::atomic<int> g_saveSnapshots;
constexpr int MAX_SAVE_SNAPSHOTS = 2;

// The savestate worker. One thread, so jobs run in order: a slot's save is written before a later
// load of it reads the file.
static std::thread g_workerThread;
static std::mutex g_workerMutex;
static std::condition_variable g_workerCond;
static std::condition_variable g_workerIdleCond;
static std::deque<std::function<void()>> g_workerJobs;
static bool g_workerBusy = false;
static bool g_workerQuit = false;

// What's left of an operation once its worker job is done, to run on the emu thread from Process().
// The argument says whether to call the operation's callbacks: not at shutdown, where they can
// refer to an EmuScreen that's going away.
static std::mutex g_completionMutex;
static std::vector<std::function<void(bool)>> g_completions;

int g_screenshotFailures;

	CChunkFileReader::Error SaveToRam(std::vector<u8> &data) {
		SaveStart state;
		state.countsAsSave = false;
		return CChunkFileReader::MeasureAndSavePtr(state, &data);
	}

	CChunkFileReader::Error LoadFromRam(std::vector<u8> &data, std::string *errorString) {
		SaveStart state;
		return CChunkFileReader::LoadPtr(&data[0], data.size(), state, errorString);
	}

	// TODO: Should this be configurable?
	// Should only fail if the game hasn't created a framebuffer yet. Some games play video without a framebuffer,
	// we should probably just read back memory then instead (GTA for example). But this is a really unimportant edge case,
	// when in-game it's just not an issue.

	void SaveStart::DoState(PointerWrap &p) {
		// Nothing may still be writing PSP memory while it's saved, or be left to write into what's loaded.
		__UtilityWaitForIO();
		__SasWaitForMix();
		__IoWaitForAsync();
		if (gpu)
			gpu->FlushPendingDrawing();

		auto s = p.Section("SaveStart", 1, 5);
		if (!s)
			return;

		if (s >= 2) {
			// This only increments on save, of course (once, not also in the measuring pass.)
			if (p.mode == p.MODE_WRITE && countsAsSave) {
				++saveStateGeneration;
			}
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

		// The state may hold different code. KernelModuleDoState hooks the functions again.
		std::vector<std::pair<u32, u32>> hookedOps;
		// The loaded modules' code is compared with the state's as it's loaded. Where it's the same,
		// the JIT keeps its blocks, so they don't all have to be compiled again.
		Memory::RamCompare codeCompare;
		if (p.mode == p.MODE_READ) {
			g_loadCount++;
			hookedOps = Replacement_GetHookedOps();
			RestoreReplacedInstructions(0, 0xFFFFFFFF);
			codeCompare.ranges = __KernelModuleCodeRanges();
		}
		Memory::DoState(p, &codeCompare);
		if (p.mode == p.MODE_READ) {
			Replacement_RepairStaleHookOps(hookedOps);
			__KernelModuleSetUnchangedCodeOnLoad(codeCompare.unchanged);
			currentMIPS->KeepJitBlocksOnNextLoad(std::move(codeCompare.unchanged));
		}

		if (s >= 3) {
			CoreTiming::DoState(p);
		}

		MemoryStick_DoState(p);
		currentMIPS->DoState(p);
		HLEDoState(p);
		__KernelDoState(p);
		Achievements::DoState(p);
		// Kernel object destructors might close open files, so do the filesystem last.
		pspFileSystem.DoState(p);
		if (s >= 4) {
			MMIO::DoState(p);
		} else if (p.mode == p.MODE_READ) {
			MMIO::Init();
		}
		if (s >= 5) {
			MeCore::DoState(p);
		} else if (p.mode == p.MODE_READ) {
			MeCore::Shutdown();
		}

		if (p.mode == p.MODE_READ && p.error == PointerWrap::ERROR_FAILURE) {
			// RAM may be loaded and the JIT not told, or the other way round. Keep nothing.
			currentMIPS->KeepJitBlocksOnNextLoad({});
			currentMIPS->ClearJitCacheDeferred();
		}
	}

	static void WorkerLoop() {
		SetCurrentThreadName("SaveStateWorker");
		// Savestates can be in Android's scoped storage, which goes through JNI.
		AndroidJNIThreadContext jniContext;

		std::unique_lock<std::mutex> lock(g_workerMutex);
		while (true) {
			g_workerCond.wait(lock, [] { return g_workerQuit || !g_workerJobs.empty(); });
			// Asked to quit, finish what's queued first.
			if (g_workerJobs.empty()) {
				break;
			}
			std::function<void()> job = std::move(g_workerJobs.front());
			g_workerJobs.pop_front();
			g_workerBusy = true;
			lock.unlock();
			job();
			lock.lock();
			g_workerBusy = false;
			if (g_workerJobs.empty()) {
				g_workerIdleCond.notify_all();
			}
		}
	}

	void RunOnWorker(std::function<void()> job) {
		std::lock_guard<std::mutex> guard(g_workerMutex);
		if (!g_workerThread.joinable()) {
			g_workerQuit = false;
			g_workerThread = std::thread(&WorkerLoop);
		}
		g_workerJobs.push_back(std::move(job));
		g_workerCond.notify_one();
	}

	void WaitForWorker() {
		std::unique_lock<std::mutex> lock(g_workerMutex);
		g_workerIdleCond.wait(lock, [] { return g_workerJobs.empty() && !g_workerBusy; });
	}

	static void StopWorker() {
		{
			std::lock_guard<std::mutex> guard(g_workerMutex);
			g_workerQuit = true;
			g_workerCond.notify_one();
		}
		if (g_workerThread.joinable()) {
			g_workerThread.join();
		}
	}

	// Shutdown() normally stops the worker. This is for an exit that never got there: destroying a
	// joinable std::thread terminates the process.
	static struct WorkerStopper {
		~WorkerStopper() {
			StopWorker();
		}
	} g_workerStopper;

	static void PostCompletion(std::function<void(bool)> completion) {
		std::lock_guard<std::mutex> guard(g_completionMutex);
		g_completions.push_back(std::move(completion));
	}

	static void RunCompletions(bool callCallbacks) {
		std::vector<std::function<void(bool)>> completions;
		{
			std::lock_guard<std::mutex> guard(g_completionMutex);
			completions.swap(g_completions);
		}
		for (auto &completion : completions) {
			completion(callCallbacks);
		}
	}

	// For an operation that was refused after a warning was already shown, so there's no message.
	// The callback still has to hear about it (auto-load tracks failure that way).
	static void CallbackRefused(const Callback &callback) {
		if (callback) {
			callback(Status::FAILURE, "", "");
		}
	}

	static bool IsSavePending(const Path &path) {
		std::lock_guard<std::mutex> guard(mutex);
		for (const auto &save : g_pendingSaves) {
			if (save->path == path) {
				return true;
			}
		}
		return false;
	}

	// If a save to path hasn't finished yet, this is a repeat of it (a key pressed twice): the
	// callback hears that save's result, with no message so it's only shown once. Returns true if so.
	// Unless a load was applied after that save's snapshot: then this saves something else.
	static bool AttachToPendingSave(const Path &path, const Callback &callback) {
		std::lock_guard<std::mutex> guard(mutex);
		// One queued and not yet applied comes before this save too.
		for (const Operation &op : g_pendingOperations) {
			if (op.type == OperationType::Load || op.type == OperationType::Rewind) {
				return false;
			}
		}
		for (auto iter = g_pendingSaves.rbegin(); iter != g_pendingSaves.rend(); ++iter) {
			SaveRecord &save = **iter;
			if (!(save.path == path)) {
				continue;
			}
			if (save.loadCount != -1 && save.loadCount != g_loadCount) {
				return false;
			}
			if (callback) {
				save.repeats.push_back(callback);
			}
			return true;
		}
		return false;
	}

	// A save is done (written, failed or refused). Returns the callbacks of its repeats.
	static std::vector<Callback> FinishPendingSave(const std::shared_ptr<SaveRecord> &save) {
		std::lock_guard<std::mutex> guard(mutex);
		g_pendingSaves.erase(std::remove(g_pendingSaves.begin(), g_pendingSaves.end(), save), g_pendingSaves.end());
		return std::move(save->repeats);
	}

	static void RefuseOperation(const Operation &op) {
		CallbackRefused(op.callback);
		if (op.type == OperationType::Save && op.save) {
			for (const Callback &repeat : FinishPendingSave(op.save)) {
				CallbackRefused(repeat);
			}
		}
	}

	// A session booted into the XMB inserts and ejects discs, which changes the mounts, and keeps
	// doing so for a game launched from it. MetaFileSystem's state pairs its sections with mounts by
	// position and doesn't record which disc was in, so a state from such a session could fail to
	// load, or load one disc's file state onto another.
	static bool IsVSHSession() {
		return PSP_CoreParameter().fileToStart.GetFilename() == "vshmain.prx";
	}

	static bool WarnUserIfVSHSession() {
		if (!IsVSHSession()) {
			return false;
		}
		auto pa = GetI18NCategory(I18NCat::PAUSE);
		g_OSD.Show(OSDType::MESSAGE_INFO, pa->T("Save states are not available in the XMB"), 3.0f, "savevsh");
		return true;
	}

	bool Enqueue(const SaveState::Operation &op) {
		// These two both refuse the operation, and both say so - dropping it silently just
		// looks like the hotkey didn't work. Callers that ask the same questions themselves
		// return before getting here, so nothing shows a message twice.
		if (NetworkWarnUserIfOnlineAndCantSavestate()) {
			CallbackRefused(op.callback);
			return false;
		}
		// Hardcore mode bans loading savestates outright, and saving too unless the user opted
		// back into that.
		if (Achievements::WarnUserIfHardcoreModeActive(op.type == OperationType::Save)) {
			CallbackRefused(op.callback);
			return false;
		}
		// Verify saves and loads back in place, with the mounts as they are.
		if (op.type != OperationType::Verify && WarnUserIfVSHSession()) {
			CallbackRefused(op.callback);
			return false;
		}

		std::lock_guard<std::mutex> guard(mutex);
		g_pendingOperations.push_back(op);
		if (op.type == OperationType::Save) {
			Operation &queued = g_pendingOperations.back();
			queued.save = std::make_shared<SaveRecord>();
			queued.save->path = op.path;
			g_pendingSaves.push_back(queued.save);
		}

		// Don't actually run it until next frame.
		needsProcess = true;
		return true;
	}

	static bool QueueLoad(const Operation &op) {
		if (NetworkWarnUserIfOnlineAndCantSavestate()) {
			CallbackRefused(op.callback);
			return false;
		}

		rewindStates.NotifyState();
		if (coreState == CoreState::CORE_RUNTIME_ERROR)
			Core_Break(BreakReason::SavestateLoad, 0);
		return Enqueue(op);
	}

	static bool QueueSave(const Operation &op) {
		if (NetworkWarnUserIfOnlineAndCantSavestate()) {
			CallbackRefused(op.callback);
			return false;
		}
		if (AttachToPendingSave(op.path, op.callback)) {
			return true;
		}

		rewindStates.NotifyState();
		if (coreState == CoreState::CORE_RUNTIME_ERROR)
			Core_Break(BreakReason::SavestateSave, 0);
		return Enqueue(op);
	}

	bool Load(const Path &filename, int slot, Callback callback) {
		return QueueLoad(Operation(OperationType::Load, filename, slot, callback));
	}

	bool Save(const Path &filename, int slot, Callback callback) {
		return QueueSave(Operation(OperationType::Save, filename, slot, callback));
	}

	bool Verify(Callback callback) {
		return Enqueue(Operation(OperationType::Verify, Path(), -1, callback));
	}

	bool Rewind(Callback callback) {
		// Or a snapshot can be taken just before the rewind runs (after a long pause, say), and the
		// rewind then restores that.
		rewindStates.NotifyState();
		if (coreState == CoreState::CORE_RUNTIME_ERROR)
			Core_Break(BreakReason::SavestateRewind, 0);
		return Enqueue(Operation(OperationType::Rewind, Path(), -1, callback));
	}

	bool CanRewind() {
		return !rewindStates.Empty();
	}

	bool IsBusy() {
		std::lock_guard<std::mutex> guard(mutex);
		// A save leaves the queue once its snapshot is taken, but stays pending until it's written.
		return !g_pendingOperations.empty() || !g_pendingSaves.empty();
	}

	// Slot utilities

	std::string AppendSlotTitle(const std::string &filename, const std::string &title) {
		char slotChar = 0;
		auto detectSlot = [&slotChar, filename](const std::string &ext) {
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

	static void ScheduleSaveScreenshot(const Path &path, bool retry = false) {
		std::lock_guard<std::mutex> guard(mutex);
		if (!retry) {
			g_screenshotFailures = 0;
		}

		// Savestate thumbnails don't need to be bigger.
		constexpr int maxResMultiplier = 2;
		ScheduleScreenshot(path, ScreenshotFormat::JPG, ScreenshotType::Display, maxResMultiplier, [path](ScreenshotResult result) {
			switch (result) {
			case ScreenshotResult::ScreenshotNotPossible:
				// Try again soon, for a short while.
				WARN_LOG(Log::SaveState, "Failed to take a screenshot for the savestate! (%s) The savestate will lack an icon.", path.c_str());
				if (coreState != CORE_STEPPING_CPU && g_screenshotFailures++ < SCREENSHOT_FAILURE_RETRIES) {
					// Requeue for next frame (if we were stepping, no point, will just spam errors quickly).
					ScheduleSaveScreenshot(path, true);
				}
				break;
			case ScreenshotResult::FailedToWriteFile:
			case ScreenshotResult::Success:
				g_screenshotFailures = 0;
				break;
			}
		});
	}

	void LoadSlot(std::string_view gamePrefix, int slot, Callback callback) {
		// Enqueue() would refuse too, but only after the load undo backup below has replaced the old one.
		if (NetworkWarnUserIfOnlineAndCantSavestate() || Achievements::WarnUserIfHardcoreModeActive(false) || WarnUserIfVSHSession()) {
			CallbackRefused(callback);
			return;
		}

		Path fn = GenerateSaveSlotPath(gamePrefix, slot, STATE_EXTENSION);
		// A save that hasn't been written yet will be by the time the load reads the file.
		if (!fn.empty() && !File::Exists(fn) && !IsSavePending(fn)) {
			if (callback) {
				auto sc = GetI18NCategory(I18NCat::SCREEN);
				callback(Status::FAILURE, sc->T("LoadStateDoesntExist", sc->T("Failed to load state")), "");
			}
			return;
		}
		if (!fn.empty()) {
			// The state it replaces is kept for load undo, when the load is applied.
			Operation op(OperationType::Load, fn, slot, callback);
			op.slotGamePrefix = gamePrefix;
			QueueLoad(op);
		} else {
			if (callback) {
				auto sy = GetI18NCategory(I18NCat::SYSTEM);
				callback(Status::FAILURE, sy->T("Failed to load state. Error in the file system."), "");
			}
		}
	}

	bool UndoLoad(std::string_view gamePrefix, Callback callback) {
		if (NetworkWarnUserIfOnlineAndCantSavestate()) {
			CallbackRefused(callback);
			return false;
		}

		if (g_Config.sStateLoadUndoGame != gamePrefix) {
			if (callback) {
				auto sy = GetI18NCategory(I18NCat::SYSTEM);
				callback(Status::FAILURE, sy->T("Error: load undo state is from a different game"), "");
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
				callback(Status::FAILURE, sy->T("Failed to load state for load undo. Error in the file system."), "");
			}
			return false;
		}
	}

	void SaveSlot(std::string_view gamePrefix, int slot, Callback callback) {
		// Enqueue() would refuse too, but only after the screenshots below have been rotated.
		if (NetworkWarnUserIfOnlineAndCantSavestate() || Achievements::WarnUserIfHardcoreModeActive(true) || WarnUserIfVSHSession()) {
			CallbackRefused(callback);
			return;
		}
		Path fn = GenerateSaveSlotPath(gamePrefix, slot, STATE_EXTENSION);
		if (!fn.empty()) {
			// Before the screenshots below are touched: a repeat shouldn't rotate them again.
			if (AttachToPendingSave(fn, callback)) {
				return;
			}
			Path shot = GenerateSaveSlotPath(gamePrefix, slot, SCREENSHOT_EXTENSION);
			// Let's also create a screenshot.
			if (g_Config.bEnableStateUndo) {
				Path shotUndo = GenerateSaveSlotPath(gamePrefix, slot, UNDO_SCREENSHOT_EXTENSION);
				DeleteIfExists(shotUndo);
				RenameIfExists(shot, shotUndo);
			}
			ScheduleSaveScreenshot(shot);
			// Written to a temporary file, then moved into place with the old state kept for undo.
			Operation op(OperationType::Save, fn, slot, callback);
			op.slotGamePrefix = gamePrefix;
			QueueSave(op);
		} else {
			if (callback) {
				auto sy = GetI18NCategory(I18NCat::SYSTEM);
				callback(Status::FAILURE, sy->T("Failed to save state. Error in the file system."), "");
			}
		}
		Rescan(gamePrefix);
	}

	bool UndoSaveSlot(std::string_view gamePrefix, int slot) {
		if (NetworkWarnUserIfOnlineAndCantSavestate()) {
			return false;
		}

		Path fnUndo = GenerateSaveSlotPath(gamePrefix, slot, UNDO_STATE_EXTENSION);
		// A save still being written would move files around under us.
		WaitForWorker();

		// Do nothing if there's no undo.
		if (File::Exists(fnUndo)) {
			Path fn = GenerateSaveSlotPath(gamePrefix, slot, STATE_EXTENSION);
			Path shot = GenerateSaveSlotPath(gamePrefix, slot, SCREENSHOT_EXTENSION);
			Path shotUndo = GenerateSaveSlotPath(gamePrefix, slot, UNDO_SCREENSHOT_EXTENSION);
			// Swap them so they can undo again to redo.  Mistakes happen.
			SwapIfExists(shotUndo, shot);
			SwapIfExists(fnUndo, fn);
			Rescan(gamePrefix);
			return true;
		}

		Rescan(gamePrefix);
		return false;
	}

	void DeleteSlot(std::string_view gamePrefix, int slot) {
		Path fn = GenerateSaveSlotPath(gamePrefix, slot, STATE_EXTENSION);
		Path shot = GenerateSaveSlotPath(gamePrefix, slot, SCREENSHOT_EXTENSION);
		Path fnName = GenerateSaveSlotPath(gamePrefix, slot, NAME_EXTENSION);
		// Or a save still being written would recreate the file.
		WaitForWorker();

		if (File::Exists(fn)) {
			DeleteIfExists(fn);
			DeleteIfExists(shot);
			DeleteIfExists(fnName);
		}
		Rescan(gamePrefix);
	}

	bool UndoLastSave(std::string_view gamePrefix) {
		if (NetworkWarnUserIfOnlineAndCantSavestate()) {
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

	static void RescanIfStale(std::string_view gamePrefix) {
		if (g_filesStale.exchange(false)) {
			Rescan(gamePrefix);
		}
	}

	static bool SaveStateFileExists(std::string_view gamePrefix, int slot, const char *extension) {
		RescanIfStale(gamePrefix);
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
		RescanIfStale(gamePrefix);
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
		RescanIfStale(gamePrefix);
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

	static std::string GetSaveFileDateAsString(const std::string &fn) {
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

	std::string GetSlotDateAsString(std::string_view gamePrefix, int slot) {
		std::string fn = GenerateSaveSlotFilename(gamePrefix, slot, STATE_EXTENSION);
		return GetSaveFileDateAsString(fn);
	}

	std::string GetSlotCustomName(std::string_view gamePrefix, int slot) {
		// Most slots don't have a name file, and this gets called for every slot each time the
		// pause screen builds its views - so consult the listing before touching the disk.
		if (!SaveStateFileExists(gamePrefix, slot, NAME_EXTENSION)) {
			return std::string();
		}
		Path path = GenerateSaveSlotPath(gamePrefix, slot, NAME_EXTENSION);
		std::string result;
		File::ReadBinaryFileToString(path, &result);
		return result;
	}

	void SetSlotCustomName(std::string_view gamePrefix, int slot, std::string_view new_name){
		const Path path = GenerateSaveSlotPath(gamePrefix, slot, NAME_EXTENSION);
		if (new_name.empty()) {
			// Clearing the name. Leaving an empty file behind would work, but it'd stay in the
			// savestate folder for good - a slot with no name shouldn't have a name file.
			DeleteIfExists(path);
		} else {
			File::WriteStringToFile(true, new_name, path);
		}
		// What we just wrote (or removed) isn't reflected in the listing GetSlotCustomName reads,
		// so without this the change wouldn't show up until something else happened to rescan.
		Rescan(gamePrefix);
	}

	std::vector<Path> GetCompanionFilePaths(const Path &statePath) {
		const std::string stateExtension = std::string(".") + STATE_EXTENSION;
		// A path that isn't a savestate yields nothing, which WithReplacedExtension now tells us
		// rather than handing back the path itself. An undo state does work, and correctly:
		// "x.undo.ppst" maps onto "x.undo.jpg".
		std::vector<Path> paths;
		for (const char *extension : { SCREENSHOT_EXTENSION, NAME_EXTENSION }) {
			Path companion;
			if (statePath.WithReplacedExtension(stateExtension, std::string(".") + extension, &companion)) {
				paths.push_back(companion);
			}
		}
		return paths;
	}

	// Which build last saved the file, from its header. Shown when a load fails, since a state from
	// another version is the usual suspect.
	static std::string SavedByVersion(const Path &path) {
		std::string title;
		std::string gitVersion;
		if (CChunkFileReader::GetFileTitle(path, &title, &gitVersion) != CChunkFileReader::ERROR_NONE || gitVersion.empty()) {
			return "";
		}
		return "PPSSPP " + gitVersion;
	}

	static bool HandleLoadFailure(bool wasRewinding, std::string *metadata) {
		if (wasRewinding) {
			WARN_LOG(Log::SaveState, "HandleLoadFailure - trying a rewind state.");
			// Okay, first, let's give the next rewind state a shot - maybe we can at least not reset entirely.
			// Actually, this seems like a bad thing - some systems can end up in bad states, like sceFont :(
			CChunkFileReader::Error result;
			do {
				std::string errorString;
				result = rewindStates.Restore(&errorString, metadata);
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

	static std::string SlotPrefix(int slot) {
		return slot >= 0 ? StringFromFormat("(%d) ", slot + 1) : "";
	}

	static void ResetAVDumping() {
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
	}

	// Freeze-frame reloads its snapshot every frame, which would undo any other load right away. So a
	// load while frozen becomes the new frozen state (freezeNext snapshots it), and is a full load:
	// the GPU only skips its resets for freeze-frame's own reloads, which it tells by frozen.
	struct FrozenLoadScope {
		const bool frozen = PSP_CoreParameter().frozen;
		FrozenLoadScope() {
			PSP_CoreParameter().frozen = false;
		}
		~FrozenLoadScope() {
			if (frozen) {
				PSP_CoreParameter().frozen = true;
				PSP_CoreParameter().freezeNext = true;
			}
		}
	};

	// What a save's worker job needs, and its completion after.
	struct SaveJob {
		Path path;
		// Where the file is written: path, or for a slot save, a temporary file moved into place after.
		Path writePath;
		std::string slotGamePrefix;
		int slot;
		// For a slot save: whether the old state was kept as the slot's undo, and where.
		bool keepUndo = false;
		Path undoPath;
		Callback callback;
		std::shared_ptr<SaveRecord> save;
	};

	// Emu thread, once the file is written (or failed to be).
	static void FinishSave(const SaveJob &job, CChunkFileReader::Error result, bool callCallbacks) {
		auto sc = GetI18NCategory(I18NCat::SCREEN);
		Status status = Status::SUCCESS;
		std::string message;
		if (result == CChunkFileReader::ERROR_NONE) {
			message = SlotPrefix(job.slot) + std::string(sc->T("Saved State"));
			g_lastSaveTime = time_now_d();
			if (!job.slotGamePrefix.empty()) {
				if (job.keepUndo) {
					g_Config.sStateUndoLastSaveGame = job.slotGamePrefix;
					g_Config.iStateUndoLastSaveSlot = job.slot;
					g_Config.Save("Saving config for savestate last save undo");
				}
				g_filesStale = true;
				// The pause screen may have listed the slots before the file was there.
				System_PostUIMessage(UIMessage::SAVESTATE_SLOT_SAVED);
			}
		} else {
			if (result == CChunkFileReader::ERROR_BROKEN_STATE) {
				// TODO: What else might we want to do here? This should be very unusual.
				ERROR_LOG(Log::SaveState, "Save state failure");
			}
			message = sc->T("Failed to save state");
			status = Status::FAILURE;
		}

		std::vector<Callback> repeats = FinishPendingSave(job.save);
		if (!callCallbacks) {
			return;
		}
		if (job.callback) {
			job.callback(status, message, "");
		}
		for (const Callback &repeat : repeats) {
			repeat(status, "", "");
		}
	}

	// Returns false if it has to wait for an earlier save to be written first.
	static bool RunSave(const Operation &op) {
		if (g_saveSnapshots >= MAX_SAVE_SNAPSHOTS) {
			return false;
		}

		INFO_LOG(Log::SaveState, "Saving state to '%s'", op.path.c_str());
		SaveJob job;
		job.path = op.path;
		job.writePath = op.slotGamePrefix.empty() ? op.path : op.path.WithExtraExtension(".tmp");
		job.slotGamePrefix = op.slotGamePrefix;
		job.slot = op.slot;
		job.callback = op.callback;
		job.save = op.save;
		if (!op.slotGamePrefix.empty()) {
			job.keepUndo = g_Config.bEnableStateUndo;
			job.undoPath = GenerateSaveSlotPath(op.slotGamePrefix, op.slot, UNDO_STATE_EXTENSION);
		}

		std::string title = g_paramSFO.GetValueString("TITLE");
		if (title.empty()) {
			// Homebrew title
			title = PSP_CoreParameter().fileToStart.ToVisualString();
			std::size_t lslash = title.find_last_of('/');
			title = title.substr(lslash + 1);
		}

		u8 *buffer = nullptr;
		size_t size = 0;
		SaveStart state;
		CChunkFileReader::Error result = CChunkFileReader::MeasureAndSavePtr(state, &buffer, &size);
		if (result != CChunkFileReader::ERROR_NONE) {
			FinishSave(job, result, true);
			return true;
		}
		ResetAVDumping();

		g_saveSnapshots++;
		RunOnWorker([job, title, buffer, size]() {
			CChunkFileReader::Error result = CChunkFileReader::SaveFile(job.writePath, title, PPSSPP_GIT_VERSION, buffer, size);
			g_saveSnapshots--;
			if (result == CChunkFileReader::ERROR_NONE && !job.slotGamePrefix.empty()) {
				if (job.keepUndo) {
					DeleteIfExists(job.undoPath);
					RenameIfExists(job.path, job.undoPath);
				} else {
					DeleteIfExists(job.path);
				}
				if (!File::Rename(job.writePath, job.path)) {
					ERROR_LOG(Log::SaveState, "Failed to move '%s' into place", job.writePath.c_str());
					result = CChunkFileReader::ERROR_BAD_FILE;
				}
			}
			PostCompletion([job, result](bool callCallbacks) {
				FinishSave(job, result, callCallbacks);
			});
		});
		return true;
	}

	// Keeps the state a slot load replaced, as load undo: the snapshot RunLoad took before loading (which
	// this takes ownership of), compressed and written on the worker.
	static void SaveLoadUndo(const std::string &gamePrefix, u8 *buffer, size_t size) {
		Path backup = GetSysDirectory(DIRECTORY_SAVESTATE) / LOAD_UNDO_NAME;
		if (backup.empty()) {
			ERROR_LOG(Log::SaveState, "Saving load undo state failed. Error in the file system.");
			free(buffer);
			return;
		}

		std::string title = g_paramSFO.GetValueString("TITLE");
		g_saveSnapshots++;
		RunOnWorker([backup, title, buffer, size, gamePrefix]() {
			Path temp = backup.WithExtraExtension(".tmp");
			CChunkFileReader::Error result = CChunkFileReader::SaveFile(temp, title, PPSSPP_GIT_VERSION, buffer, size);
			g_saveSnapshots--;
			if (result == CChunkFileReader::ERROR_NONE) {
				DeleteIfExists(backup);
				if (!File::Rename(temp, backup)) {
					// The undo would load whatever was there before, maybe another game's state.
					result = CChunkFileReader::ERROR_BAD_FILE;
				}
			}
			// Not a callback, so it runs at shutdown too.
			PostCompletion([result, gamePrefix](bool) {
				if (result == CChunkFileReader::ERROR_NONE) {
					g_Config.sStateLoadUndoGame = gamePrefix;
					g_Config.Save("Saving config for savestate last load undo");
				} else {
					ERROR_LOG(Log::SaveState, "Saving load undo state failed");
				}
			});
		});
	}

	// Returns false while the file is still being read.
	static bool RunLoad(Operation &op) {
		if (!op.read) {
			INFO_LOG(Log::SaveState, "Loading state from '%s'", op.path.c_str());
			op.read = std::make_shared<PendingRead>();
			RunOnWorker([read = op.read, path = op.path]() {
				u8 *buffer = nullptr;
				read->error = CChunkFileReader::LoadFile(path, &read->gitVersion, buffer, read->size);
				read->buffer.reset(buffer);
				if (read->error == CChunkFileReader::ERROR_NONE) {
					read->savedBy = read->gitVersion.empty() ? "" : "PPSSPP " + read->gitVersion;
				} else {
					read->savedBy = SavedByVersion(path);
				}
				read->done = true;
			});
			return false;
		}
		if (!op.read->done) {
			return false;
		}

		PendingRead &read = *op.read;
		auto sc = GetI18NCategory(I18NCat::SCREEN);
		const char *i18nLoadFailure = sc->T_cstr("Failed to load state");
		Status status;
		std::string message;
		std::string metadata;

		if (read.error != CChunkFileReader::ERROR_NONE) {
			message = read.error == CChunkFileReader::ERROR_FILE_MISSING ? std::string(sc->T("LoadStateDoesntExist")) : std::string(i18nLoadFailure);
			metadata = read.savedBy;
			if (op.callback) {
				op.callback(Status::FAILURE, message, metadata);
			}
			return true;
		}

		// A load that fails partway leaves the emulator half in the new state. With a snapshot of the current
		// one, we can go back to it rather than reset the game. The snapshot is also what load undo keeps.
		const bool keepUndo = !op.slotGamePrefix.empty() && g_Config.bEnableStateUndo;
		u8 *snapshot = nullptr;
		size_t snapshotSize = 0;
		{
			SaveStart current;
			current.countsAsSave = keepUndo;
			if (CChunkFileReader::MeasureAndSavePtr(current, &snapshot, &snapshotSize) != CChunkFileReader::ERROR_NONE) {
				// Out of memory, most likely. A failed load will have to reset the game.
				WARN_LOG(Log::SaveState, "Couldn't snapshot the current state before loading");
				snapshot = nullptr;
			}
		}

		// Use the state's latest version as a guess for saveStateInitialGitVersion.
		saveStateInitialGitVersion = read.gitVersion;
		SaveStart state;
		std::string errorString;
		CChunkFileReader::Error result;
		bool restored = false;
		{
			FrozenLoadScope frozenScope;
			result = CChunkFileReader::LoadPtr(read.buffer.get(), read.size, state, &errorString);
			read.buffer.reset();
			if (result != CChunkFileReader::ERROR_NONE && snapshot) {
				SaveStart current;
				std::string restoreError;
				restored = CChunkFileReader::LoadPtr(snapshot, snapshotSize, current, &restoreError) == CChunkFileReader::ERROR_NONE;
				if (!restored) {
					ERROR_LOG(Log::SaveState, "Restoring the state from before the failed load also failed, at %s", restoreError.c_str());
				}
			}
		}

		// After a failed load we're back where we were (unless that failed too, and the game resets), so
		// the undo already on disk is still the useful one.
		if (snapshot && keepUndo && !restored) {
			SaveLoadUndo(op.slotGamePrefix, snapshot, snapshotSize);
		} else {
			free(snapshot);
		}

		if (result == CChunkFileReader::ERROR_NONE) {
			message = op.slot != LOAD_UNDO_SLOT ? sc->T("Loaded State") : sc->T("State load undone");
			status = TriggerLoadWarnings(message);
			metadata = SaveState::GetSaveFileDateAsString(op.path.GetFilename());

			hasLoadedState = true;
			Core_ResetException();

			if (op.slot >= 0)
				message = SlotPrefix(op.slot) + message;

			ResetAVDumping();
			g_lastSaveTime = time_now_d();
		} else {
			if (!restored) {
				HandleLoadFailure(false, &metadata);
			}
			// The file was fine, its contents weren't: almost always a state from another version.
			// errorString is the section that failed.
			message = sc->T("LoadStateWrongVersion", i18nLoadFailure);
			metadata = read.savedBy.empty() ? "(" + errorString + ")" : read.savedBy + " (" + errorString + ")";
			ERROR_LOG(Log::SaveState, "Load state failure at %s (saved by %s)", errorString.c_str(), read.savedBy.c_str());
			status = Status::FAILURE;
		}
		if (op.callback) {
			op.callback(status, message, metadata);
		}
		return true;
	}

	// Returns false while the snapshot is still being decoded.
	static bool RunRewind(Operation &op) {
		auto sc = GetI18NCategory(I18NCat::SCREEN);
		const char *i18nLoadFailure = sc->T_cstr("Failed to load state");
		if (!op.started) {
			INFO_LOG(Log::SaveState, "Rewinding to recent savestate snapshot");
			op.started = true;
			if (rewindStates.StartRestore()) {
				return false;
			}
			if (op.callback) {
				op.callback(Status::FAILURE, i18nLoadFailure, "");
			}
			return true;
		}
		if (!rewindStates.RestoreDecoded()) {
			return false;
		}

		Status status;
		std::string message;
		std::string metadata;
		std::string errorString;
		// Covers HandleLoadFailure()'s retries below too.
		FrozenLoadScope frozenScope;
		CChunkFileReader::Error result = rewindStates.FinishRestore(&errorString, &metadata);
		if (result == CChunkFileReader::ERROR_NONE) {
			message = sc->T("Loaded State");
			status = Status::SUCCESS;
			hasLoadedState = true;
			Core_ResetException();
		} else if (result == CChunkFileReader::ERROR_BROKEN_STATE) {
			// Cripes.  Good news is, we might have more.  Let's try those too, better than a reset.
			if (HandleLoadFailure(true, &metadata)) {
				// Well, we did rewind, even if too much...
				message = sc->T("Loaded State");
				status = Status::SUCCESS;
				hasLoadedState = true;
				Core_ResetException();
			} else {
				message = std::string(i18nLoadFailure) + " (" + errorString + ")";
				status = Status::FAILURE;
			}
		} else {
			message = i18nLoadFailure;
			status = Status::FAILURE;
		}
		if (op.callback) {
			op.callback(status, message, metadata);
		}
		return true;
	}

	static void RunVerify(const Operation &op) {
		SaveStart verifyState;
		verifyState.countsAsSave = false;
		bool success = CChunkFileReader::Verify(verifyState) == CChunkFileReader::ERROR_NONE;
		if (success) {
			INFO_LOG(Log::SaveState, "Verified save state system");
		} else {
			ERROR_LOG(Log::SaveState, "Save state system verification failed");
		}
		if (op.callback) {
			op.callback(success ? Status::SUCCESS : Status::FAILURE, "", "");
		}
	}

	// The emu thread part of an operation. Returns false if it has to wait, keeping it (and those
	// behind it) queued for a later Process().
	static bool RunOperation(Operation &op) {
		if (!op.started && !op.read) {
			// Re-check here, and not just in Enqueue: during boot, RetroAchievements is still
			// identifying the game asynchronously, and until it's done hardcore mode doesn't
			// read as active yet. An operation queued in that window (a hotkey press, --state,
			// auto-load) passed the check in Enqueue and would otherwise be applied here, after
			// identification has finished and hardcore mode has come up.
			if (Achievements::WarnUserIfHardcoreModeActive(op.type == OperationType::Save)) {
				WARN_LOG(Log::SaveState, "Dropping queued savestate operation - hardcore mode is active");
				RefuseOperation(op);
				return true;
			}
		}

		switch (op.type) {
		case OperationType::Save:
			return RunSave(op);
		case OperationType::Load:
			return RunLoad(op);
		case OperationType::Rewind:
			return RunRewind(op);
		case OperationType::Verify:
			RunVerify(op);
			return true;
		default:
			ERROR_LOG(Log::SaveState, "Savestate failure: unknown operation type %d", (int)op.type);
			RefuseOperation(op);
			return true;
		}
	}

	// NOTE: This can cause ending of the current renderpass, due to the readback needed for the screenshot.
	void Process(bool takeRewindSnapshots) {
		if (takeRewindSnapshots && !IsVSHSession()) {
			rewindStates.Process();
		}
		RunCompletions(true);

		if (!needsProcess)
			return;
		if (coreState == CORE_STEPPING_GE || coreState == CORE_RUNNING_GE) {
			// A display list stopped in the GE debugger still belongs to the sceGe call that started
			// it, which finishes when the list does. Wait for that.
			return;
		}

		std::unique_lock<std::mutex> lock(mutex);
		if (!__KernelIsRunning()) {
			ERROR_LOG(Log::SaveState, "Savestate failure: Unable to load without kernel, this should never happen.");
			std::deque<Operation> operations;
			operations.swap(g_pendingOperations);
			needsProcess = false;
			lock.unlock();
			for (const Operation &op : operations) {
				if (op.type == OperationType::Rewind && op.started) {
					rewindStates.CancelRestore();
				}
				RefuseOperation(op);
			}
			return;
		}

		bool ranAny = false;
		while (!g_pendingOperations.empty()) {
			// Enqueue() only appends, which leaves this reference alone. Unlocked, since callbacks can
			// queue more.
			Operation &op = g_pendingOperations.front();
			if (op.save) {
				// Its snapshot is next: nothing can be loaded in between, it's first in the queue.
				op.save->loadCount = g_loadCount;
			}
			lock.unlock();
			const bool finished = RunOperation(op);
			lock.lock();
			if (!finished) {
				break;
			}
			ranAny = true;
			g_pendingOperations.pop_front();
		}
		needsProcess = !g_pendingOperations.empty();
		lock.unlock();

		if (ranAny) {
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
		// A save that was asked for still gets written. Then the bookkeeping that follows a write
		// (the undo slot in the config, for one), but not the callbacks.
		WaitForWorker();
		RunCompletions(false);

		std::lock_guard<std::mutex> guard(mutex);
		rewindStates.Clear();
		// Operations still queued belong to the game that's going away (Process() doesn't run while
		// achievements block execution at boot). Run later, they'd load its state into the next game,
		// and their callbacks can capture the EmuScreen that queued them.
		if (!g_pendingOperations.empty()) {
			WARN_LOG(Log::SaveState, "Dropping %d queued savestate operations at shutdown", (int)g_pendingOperations.size());
			g_pendingOperations.clear();
		}
		g_pendingSaves.clear();
		needsProcess = false;
		StopWorker();
	}

	double SecondsSinceLastSavestate() {
		if (g_lastSaveTime < 0) {
			return -1.0;
		} else {
			return time_now_d() - g_lastSaveTime;
		}
	}

}  // namespace SaveState
