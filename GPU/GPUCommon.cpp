#include <algorithm>
#include "native/base/mutex.h"
#include "native/base/timeutil.h"
#include "GeDisasm.h"
#include "GPUCommon.h"
#include "GPUState.h"
#include "ChunkFile.h"
#include "Core/Config.h"
#include "Core/CoreTiming.h"
#include "Core/MemMap.h"
#include "Core/Host.h"
#include "Core/Reporting.h"
#include "Core/HLE/HLE.h"
#include "Core/HLE/sceKernelMemory.h"
#include "Core/HLE/sceKernelInterrupt.h"
#include "Core/HLE/sceKernelThread.h"
#include "Core/HLE/sceGe.h"

GPUCommon::GPUCommon() :
	dumpNextFrame_(false),
	dumpThisFrame_(false)
{
	Reinitialize();
	SetThreadEnabled(g_Config.bSeparateCPUThread);
}

void GPUCommon::Reinitialize() {
	easy_guard guard(listLock);
	memset(dls, 0, sizeof(dls));
	for (int i = 0; i < DisplayListMaxCount; ++i) {
		dls[i].state = PSP_GE_DL_STATE_NONE;
		dls[i].waitTicks = 0;
	}

	nextListID = 0;
	currentList = NULL;
	isbreak = false;
	drawCompleteTicks = 0;
	busyTicks = 0;
	timeSpentStepping_ = 0.0;
	interruptsEnabled_ = true;
	UpdateTickEstimate(0);
}

void GPUCommon::PopDLQueue() {
	easy_guard guard(listLock);
	if(!dlQueue.empty()) {
		dlQueue.pop_front();
		if(!dlQueue.empty()) {
			bool running = currentList->state == PSP_GE_DL_STATE_RUNNING;
			currentList = &dls[dlQueue.front()];
			if (running)
				currentList->state = PSP_GE_DL_STATE_RUNNING;
		} else {
			currentList = NULL;
		}
	}
}

bool GPUCommon::BusyDrawing() {
	u32 state = DrawSync(1);
	if (state == PSP_GE_LIST_DRAWING || state == PSP_GE_LIST_STALLING) {
		lock_guard guard(listLock);
		if (currentList && currentList->state != PSP_GE_DL_STATE_PAUSED) {
			return true;
		}
	}
	return false;
}

u32 GPUCommon::DrawSync(int mode) {
	if (g_Config.bSeparateCPUThread) {
		// Sync first, because the CPU is usually faster than the emulated GPU.
		SyncThread();
	}

	easy_guard guard(listLock);
	if (mode < 0 || mode > 1)
		return SCE_KERNEL_ERROR_INVALID_MODE;

	if (mode == 0) {
		if (!__KernelIsDispatchEnabled()) {
			return SCE_KERNEL_ERROR_CAN_NOT_WAIT;
		}
		if (__IsInInterrupt()) {
			return SCE_KERNEL_ERROR_ILLEGAL_CONTEXT;
		}

		if (drawCompleteTicks > CoreTiming::GetTicks()) {
			__GeWaitCurrentThread(GPU_SYNC_DRAW, 1, "GeDrawSync");
		} else {
			for (int i = 0; i < DisplayListMaxCount; ++i) {
				if (dls[i].state == PSP_GE_DL_STATE_COMPLETED) {
					dls[i].state = PSP_GE_DL_STATE_NONE;
				}
			}
		}
		return 0;
	}

	// If there's no current list, it must be complete.
	DisplayList *top = NULL;
	for (auto it = dlQueue.begin(), end = dlQueue.end(); it != end; ++it) {
		if (dls[*it].state != PSP_GE_DL_STATE_COMPLETED) {
			top = &dls[*it];
			break;
		}
	}
	if (!top || top->state == PSP_GE_DL_STATE_COMPLETED)
		return PSP_GE_LIST_COMPLETED;

	if (currentList->pc == currentList->stall)
		return PSP_GE_LIST_STALLING;

	return PSP_GE_LIST_DRAWING;
}

void GPUCommon::CheckDrawSync() {
	easy_guard guard(listLock);
	if (dlQueue.empty()) {
		for (int i = 0; i < DisplayListMaxCount; ++i)
			dls[i].state = PSP_GE_DL_STATE_NONE;
	}
}

int GPUCommon::ListSync(int listid, int mode) {
	if (g_Config.bSeparateCPUThread) {
		// Sync first, because the CPU is usually faster than the emulated GPU.
		SyncThread();
	}

	easy_guard guard(listLock);
	if (listid < 0 || listid >= DisplayListMaxCount)
		return SCE_KERNEL_ERROR_INVALID_ID;

	if (mode < 0 || mode > 1)
		return SCE_KERNEL_ERROR_INVALID_MODE;

	DisplayList& dl = dls[listid];
	if (mode == 1) {
		switch (dl.state) {
		case PSP_GE_DL_STATE_QUEUED:
			if (dl.interrupted)
				return PSP_GE_LIST_PAUSED;
			return PSP_GE_LIST_QUEUED;

		case PSP_GE_DL_STATE_RUNNING:
			if (dl.pc == dl.stall)
				return PSP_GE_LIST_STALLING;
			return PSP_GE_LIST_DRAWING;

		case PSP_GE_DL_STATE_COMPLETED:
			return PSP_GE_LIST_COMPLETED;

		case PSP_GE_DL_STATE_PAUSED:
			return PSP_GE_LIST_PAUSED;

		default:
			return SCE_KERNEL_ERROR_INVALID_ID;
		}
	}

	if (!__KernelIsDispatchEnabled()) {
		return SCE_KERNEL_ERROR_CAN_NOT_WAIT;
	}
	if (__IsInInterrupt()) {
		return SCE_KERNEL_ERROR_ILLEGAL_CONTEXT;
	}

	if (dl.waitTicks > CoreTiming::GetTicks()) {
		__GeWaitCurrentThread(GPU_SYNC_LIST, listid, "GeListSync");
	}
	return PSP_GE_LIST_COMPLETED;
}

int GPUCommon::GetStack(int index, u32 stackPtr) {
	easy_guard guard(listLock);
	if (currentList == NULL) {
		// Seems like it doesn't return an error code?
		return 0;
	}

	if (currentList->stackptr <= index) {
		return SCE_KERNEL_ERROR_INVALID_INDEX;
	}

	if (index >= 0) {
		auto stack = PSPPointer<u32>::Create(stackPtr);
		if (stack.IsValid()) {
			auto entry = currentList->stack[index];
			// Not really sure what most of these values are.
			stack[0] = 0;
			stack[1] = entry.pc + 4;
			stack[2] = entry.offsetAddr;
			stack[7] = entry.baseAddr;
		}
	}

	return currentList->stackptr;
}

u32 GPUCommon::EnqueueList(u32 listpc, u32 stall, int subIntrBase, PSPPointer<PspGeListArgs> args, bool head) {
	easy_guard guard(listLock);
	// TODO Check the stack values in missing arg and ajust the stack depth

	// Check alignment
	// TODO Check the context and stack alignement too
	if (((listpc | stall) & 3) != 0)
		return SCE_KERNEL_ERROR_INVALID_POINTER;

	int id = -1;
	u64 currentTicks = CoreTiming::GetTicks();
	// Check compatibility
	if (sceKernelGetCompiledSdkVersion() > 0x01FFFFFF) {
		//numStacks = 0;
		//stack = NULL;
		for (int i = 0; i < DisplayListMaxCount; ++i) {
			if (dls[i].state != PSP_GE_DL_STATE_NONE && dls[i].state != PSP_GE_DL_STATE_COMPLETED) {
				// Logically, if the CPU has not interrupted yet, it hasn't seen the latest pc either.
				// Exit enqueues right after an END, which fails without ignoring pendingInterrupt lists.
				if (dls[i].pc == listpc && !dls[i].pendingInterrupt) {
					ERROR_LOG(G3D, "sceGeListEnqueue: can't enqueue, list address %08X already used", listpc);
					return 0x80000021;
				}
			}
		}
	}
	// TODO Check if list stack dls[i].stack already used then return 0x80000021 as above

	for (int i = 0; i < DisplayListMaxCount; ++i) {
		int possibleID = (i + nextListID) % DisplayListMaxCount;
		auto possibleList = dls[possibleID];
		if (possibleList.pendingInterrupt) {
			continue;
		}

		if (possibleList.state == PSP_GE_DL_STATE_NONE) {
			id = possibleID;
			break;
		}
		if (possibleList.state == PSP_GE_DL_STATE_COMPLETED && possibleList.waitTicks < currentTicks) {
			id = possibleID;
		}
	}
	if (id < 0) {
		ERROR_LOG_REPORT(G3D, "No DL ID available to enqueue");
		for (auto it = dlQueue.begin(); it != dlQueue.end(); ++it) {
			DisplayList &dl = dls[*it];
			DEBUG_LOG(G3D, "DisplayList %d status %d pc %08x stall %08x", *it, dl.state, dl.pc, dl.stall);
		}
		return SCE_KERNEL_ERROR_OUT_OF_MEMORY;
	}
	nextListID = id + 1;

	DisplayList &dl = dls[id];
	dl.id = id;
	dl.startpc = listpc & 0x0FFFFFFF;
	dl.pc = listpc & 0x0FFFFFFF;
	dl.stall = stall & 0x0FFFFFFF;
	dl.subIntrBase = std::max(subIntrBase, -1);
	dl.stackptr = 0;
	dl.signal = PSP_GE_SIGNAL_NONE;
	dl.interrupted = false;
	dl.waitTicks = (u64)-1;
	dl.interruptsEnabled = interruptsEnabled_;
	dl.started = false;
	dl.offsetAddr = 0;
	dl.bboxResult = false;

	if (args.IsValid() && args->context.IsValid())
		dl.context = args->context;
	else
		dl.context = 0;

	if (head) {
		if (currentList) {
			if (currentList->state != PSP_GE_DL_STATE_PAUSED)
				return SCE_KERNEL_ERROR_INVALID_VALUE;
			currentList->state = PSP_GE_DL_STATE_QUEUED;
		}

		dl.state = PSP_GE_DL_STATE_PAUSED;

		currentList = &dl;
		dlQueue.push_front(id);
	} else if (currentList) {
		dl.state = PSP_GE_DL_STATE_QUEUED;
		dlQueue.push_back(id);
	} else {
		dl.state = PSP_GE_DL_STATE_RUNNING;
		currentList = &dl;
		dlQueue.push_front(id);

		drawCompleteTicks = (u64)-1;

		// TODO save context when starting the list if param is set
		guard.unlock();
		ProcessDLQueue();
	}

	return id;
}

u32 GPUCommon::DequeueList(int listid) {
	easy_guard guard(listLock);
	if (listid < 0 || listid >= DisplayListMaxCount || dls[listid].state == PSP_GE_DL_STATE_NONE)
		return SCE_KERNEL_ERROR_INVALID_ID;

	auto &dl = dls[listid];
	if (dl.started)
		return SCE_KERNEL_ERROR_BUSY;

	dl.state = PSP_GE_DL_STATE_NONE;

	if (listid == dlQueue.front())
		PopDLQueue();
	else
		dlQueue.remove(listid);

	dl.waitTicks = 0;
	__GeTriggerWait(GPU_SYNC_LIST, listid);

	CheckDrawSync();

	return 0;
}

u32 GPUCommon::UpdateStall(int listid, u32 newstall) {
	easy_guard guard(listLock);
	if (listid < 0 || listid >= DisplayListMaxCount || dls[listid].state == PSP_GE_DL_STATE_NONE)
		return SCE_KERNEL_ERROR_INVALID_ID;
	auto &dl = dls[listid];
	if (dl.state == PSP_GE_DL_STATE_COMPLETED)
		return SCE_KERNEL_ERROR_ALREADY;

	dl.stall = newstall & 0x0FFFFFFF;
	
	guard.unlock();
	ProcessDLQueue();

	return 0;
}

u32 GPUCommon::Continue() {
	easy_guard guard(listLock);
	if (!currentList)
		return 0;

	if (currentList->state == PSP_GE_DL_STATE_PAUSED)
	{
		if (!isbreak)
		{
			// TODO: Supposedly this returns SCE_KERNEL_ERROR_BUSY in some case, previously it had
			// currentList->signal == PSP_GE_SIGNAL_HANDLER_PAUSE, but it doesn't reproduce.

			currentList->state = PSP_GE_DL_STATE_RUNNING;
			currentList->signal = PSP_GE_SIGNAL_NONE;

			// TODO Restore context of DL is necessary
			// TODO Restore BASE

			// We have a list now, so it's not complete.
			drawCompleteTicks = (u64)-1;
		}
		else
			currentList->state = PSP_GE_DL_STATE_QUEUED;
	}
	else if (currentList->state == PSP_GE_DL_STATE_RUNNING)
	{
		if (sceKernelGetCompiledSdkVersion() >= 0x02000000)
			return 0x80000020;
		return -1;
	}
	else
	{
		if (sceKernelGetCompiledSdkVersion() >= 0x02000000)
			return 0x80000004;
		return -1;
	}

	guard.unlock();
	ProcessDLQueue();
	return 0;
}

u32 GPUCommon::Break(int mode) {
	easy_guard guard(listLock);
	if (mode < 0 || mode > 1)
		return SCE_KERNEL_ERROR_INVALID_MODE;

	if (!currentList)
		return SCE_KERNEL_ERROR_ALREADY;

	if (mode == 1)
	{
		// Clear the queue
		dlQueue.clear();
		for (int i = 0; i < DisplayListMaxCount; ++i)
		{
			dls[i].state = PSP_GE_DL_STATE_NONE;
			dls[i].signal = PSP_GE_SIGNAL_NONE;
		}

		nextListID = 0;
		currentList = NULL;
		return 0;
	}

	if (currentList->state == PSP_GE_DL_STATE_NONE || currentList->state == PSP_GE_DL_STATE_COMPLETED)
	{
		if (sceKernelGetCompiledSdkVersion() >= 0x02000000)
			return 0x80000004;
		return -1;
	}

	if (currentList->state == PSP_GE_DL_STATE_PAUSED)
	{
		if (sceKernelGetCompiledSdkVersion() > 0x02000010)
		{
			if (currentList->signal == PSP_GE_SIGNAL_HANDLER_PAUSE)
			{
				ERROR_LOG_REPORT(G3D, "sceGeBreak: can't break signal-pausing list");
			}
			else
				return SCE_KERNEL_ERROR_ALREADY;
		}
		return SCE_KERNEL_ERROR_BUSY;
	}

	if (currentList->state == PSP_GE_DL_STATE_QUEUED)
	{
		currentList->state = PSP_GE_DL_STATE_PAUSED;
		return currentList->id;
	}

	// TODO Save BASE
	// TODO Adjust pc to be just before SIGNAL/END

	// TODO: Is this right?
	if (currentList->signal == PSP_GE_SIGNAL_SYNC)
		currentList->pc += 8;

	currentList->interrupted = true;
	currentList->state = PSP_GE_DL_STATE_PAUSED;
	currentList->signal = PSP_GE_SIGNAL_HANDLER_SUSPEND;
	isbreak = true;

	return currentList->id;
}

void GPUCommon::NotifySteppingEnter() {
	if (g_Config.bShowDebugStats) {
		time_update();
		timeSteppingStarted_ = time_now_d();
	}
}
void GPUCommon::NotifySteppingExit() {
	if (g_Config.bShowDebugStats) {
		if (timeSteppingStarted_ <= 0.0) {
			ERROR_LOG(G3D, "Mismatched stepping enter/exit.");
		}
		time_update();
		timeSpentStepping_ += time_now_d() - timeSteppingStarted_;
		timeSteppingStarted_ = 0.0;
	}
}

bool GPUCommon::InterpretList(DisplayList &list) {
	// Initialized to avoid a race condition with bShowDebugStats changing.
	double start = 0.0;
	if (g_Config.bShowDebugStats) {
		time_update();
		start = time_now_d();
	}

	easy_guard guard(listLock);

	if (list.state == PSP_GE_DL_STATE_PAUSED)
		return false;
	currentList = &list;

	if (!list.started && list.context.IsValid()) {
		gstate.Save(list.context);
	}
	list.started = true;

	gstate_c.offsetAddr = list.offsetAddr;

	if (!Memory::IsValidAddress(list.pc)) {
		ERROR_LOG_REPORT(G3D, "DL PC = %08x WTF!!!!", list.pc);
		return true;
	}

	cycleLastPC = list.pc;
	cyclesExecuted += 60;
	downcount = list.stall == 0 ? 0x0FFFFFFF : (list.stall - list.pc) / 4;
	list.state = PSP_GE_DL_STATE_RUNNING;
	list.interrupted = false;

	gpuState = list.pc == list.stall ? GPUSTATE_STALL : GPUSTATE_RUNNING;
	guard.unlock();

	const bool useDebugger = host->GPUDebuggingActive();
	const bool useFastRunLoop = !dumpThisFrame_ && !useDebugger;
	while (gpuState == GPUSTATE_RUNNING) {
		{
			easy_guard innerGuard(listLock);
			if (list.pc == list.stall) {
				gpuState = GPUSTATE_STALL;
				downcount = 0;
			}
		}

		if (useFastRunLoop) {
			FastRunLoop(list);
		} else {
			SlowRunLoop(list);
		}

		{
			easy_guard innerGuard(listLock);
			downcount = list.stall == 0 ? 0x0FFFFFFF : (list.stall - list.pc) / 4;

			if (gpuState == GPUSTATE_STALL && list.stall != list.pc) {
				// Unstalled.
				gpuState = GPUSTATE_RUNNING;
			}
		}
	}

	// We haven't run the op at list.pc, so it shouldn't count.
	if (cycleLastPC != list.pc) {
		UpdatePC(list.pc - 4, list.pc);
	}

	list.offsetAddr = gstate_c.offsetAddr;

	if (g_Config.bShowDebugStats) {
		time_update();
		double total = time_now_d() - start - timeSpentStepping_;
		hleSetSteppingTime(timeSpentStepping_);
		timeSpentStepping_ = 0.0;
		gpuStats.msProcessingDisplayLists += total;
	}
	return gpuState == GPUSTATE_DONE || gpuState == GPUSTATE_ERROR;
}

void GPUCommon::SlowRunLoop(DisplayList &list)
{
	const bool dumpThisFrame = dumpThisFrame_;
	while (downcount > 0)
	{
		host->GPUNotifyCommand(list.pc);
		u32 op = Memory::ReadUnchecked_U32(list.pc);
		u32 cmd = op >> 24;

		u32 diff = op ^ gstate.cmdmem[cmd];
		PreExecuteOp(op, diff);
		if (dumpThisFrame) {
			char temp[256];
			u32 prev;
			if (Memory::IsValidAddress(list.pc - 4)) {
				prev = Memory::ReadUnchecked_U32(list.pc - 4);
			} else {
				prev = 0;
			}
			GeDisassembleOp(list.pc, op, prev, temp);
			NOTICE_LOG(G3D, "%s", temp);
		}
		gstate.cmdmem[cmd] = op;

		ExecuteOp(op, diff);

		list.pc += 4;
		--downcount;
	}
}

// The newPC parameter is used for jumps, we don't count cycles between.
void GPUCommon::UpdatePC(u32 currentPC, u32 newPC) {
	// Rough estimate, 2 CPU ticks (it's double the clock rate) per GPU instruction.
	u32 executed = (currentPC - cycleLastPC) / 4;
	cyclesExecuted += 2 * executed;
	cycleLastPC = newPC;

	if (g_Config.bShowDebugStats) {
		gpuStats.otherGPUCycles += 2 * executed;
		gpuStats.gpuCommandsAtCallLevel[std::min(currentList->stackptr, 3)] += executed;
	}

	// Exit the runloop and recalculate things.  This happens a lot in some games.
	easy_guard innerGuard(listLock);
	if (currentList)
		downcount = currentList->stall == 0 ? 0x0FFFFFFF : (currentList->stall - newPC) / 4;
	else
		downcount = 0;
}

void GPUCommon::ReapplyGfxState() {
	if (IsOnSeparateCPUThread()) {
		ScheduleEvent(GPU_EVENT_REAPPLY_GFX_STATE);
	} else {
		ReapplyGfxStateInternal();
	}
}

void GPUCommon::ReapplyGfxStateInternal() {
	// ShaderManager_DirtyShader();
	// The commands are embedded in the command memory so we can just reexecute the words. Convenient.
	// To be safe we pass 0xFFFFFFFF as the diff.

	for (int i = GE_CMD_VERTEXTYPE; i < GE_CMD_BONEMATRIXNUMBER; i++) {
		if (i != GE_CMD_ORIGIN && i != GE_CMD_OFFSETADDR) {
			ExecuteOp(gstate.cmdmem[i], 0xFFFFFFFF);
		}
	}

	// Can't write to bonematrixnumber here

	for (int i = GE_CMD_MORPHWEIGHT0; i <= GE_CMD_PATCHFACING; i++) {
		ExecuteOp(gstate.cmdmem[i], 0xFFFFFFFF);
	}

	// There are a few here in the middle that we shouldn't execute...

	for (int i = GE_CMD_VIEWPORTX1; i < GE_CMD_TRANSFERSTART; i++) {
		ExecuteOp(gstate.cmdmem[i], 0xFFFFFFFF);
	}

	// Let's just skip the transfer size stuff, it's just values.
}

inline void GPUCommon::UpdateState(GPUState state) {
	gpuState = state;
	if (state != GPUSTATE_RUNNING)
		downcount = 0;
}

void GPUCommon::ProcessEvent(GPUEvent ev) {
	switch (ev.type) {
	case GPU_EVENT_PROCESS_QUEUE:
		ProcessDLQueueInternal();
		break;

	case GPU_EVENT_REAPPLY_GFX_STATE:
		ReapplyGfxStateInternal();
		break;

	default:
		ERROR_LOG_REPORT(G3D, "Unexpected GPU event type: %d", (int)ev);
	}
}

int GPUCommon::GetNextListIndex() {
	easy_guard guard(listLock);
	auto iter = dlQueue.begin();
	if (iter != dlQueue.end()) {
		return *iter;
	} else {
		return -1;
	}
}

bool GPUCommon::ProcessDLQueue() {
	ScheduleEvent(GPU_EVENT_PROCESS_QUEUE);
	return true;
}

void GPUCommon::ProcessDLQueueInternal() {
	startingTicks = CoreTiming::GetTicks();
	cyclesExecuted = 0;
	UpdateTickEstimate(std::max(busyTicks, startingTicks + cyclesExecuted));

	// Game might've written new texture data.
	gstate_c.textureChanged = TEXCHANGE_UPDATED;

	// Seems to be correct behaviour to process the list anyway?
	if (startingTicks < busyTicks) {
		DEBUG_LOG(G3D, "Can't execute a list yet, still busy for %lld ticks", busyTicks - startingTicks);
		//return;
	}

	for (int listIndex = GetNextListIndex(); listIndex != -1; listIndex = GetNextListIndex()) {
		DisplayList &l = dls[listIndex];
		DEBUG_LOG(G3D, "Okay, starting DL execution at %08x - stall = %08x", l.pc, l.stall);
		if (!InterpretList(l)) {
			return;
		} else {
			easy_guard guard(listLock);

			// Some other list could've taken the spot while we dilly-dallied around.
			if (l.state != PSP_GE_DL_STATE_QUEUED) {
				// At the end, we can remove it from the queue and continue.
				dlQueue.erase(std::remove(dlQueue.begin(), dlQueue.end(), listIndex), dlQueue.end());
			}
			UpdateTickEstimate(std::max(busyTicks, startingTicks + cyclesExecuted));
		}
	}

	easy_guard guard(listLock);
	currentList = NULL;

	drawCompleteTicks = startingTicks + cyclesExecuted;
	busyTicks = std::max(busyTicks, drawCompleteTicks);
	__GeTriggerSync(GPU_SYNC_DRAW, 1, drawCompleteTicks);
	// Since the event is in CoreTiming, we're in sync.  Just set 0 now.
	UpdateTickEstimate(0);
}

void GPUCommon::PreExecuteOp(u32 op, u32 diff) {
	// Nothing to do
}

void GPUCommon::Execute_OffsetAddr(u32 op, u32 diff) {
	gstate_c.offsetAddr = op << 8;
}

void GPUCommon::Execute_Origin(u32 op, u32 diff) {
	easy_guard guard(listLock);
	gstate_c.offsetAddr = currentList->pc;
}

void GPUCommon::Execute_Jump(u32 op, u32 diff) {
	easy_guard guard(listLock);
	const u32 target = gstate_c.getRelativeAddress(op & 0x00FFFFFC);
	if (Memory::IsValidAddress(target)) {
		UpdatePC(currentList->pc, target - 4);
		currentList->pc = target - 4; // pc will be increased after we return, counteract that
	} else {
		ERROR_LOG_REPORT(G3D, "JUMP to illegal address %08x - ignoring! data=%06x", target, op & 0x00FFFFFF);
	}
}

void GPUCommon::Execute_BJump(u32 op, u32 diff) {
	if (!currentList->bboxResult) {
		// bounding box jump.
		easy_guard guard(listLock);
		const u32 target = gstate_c.getRelativeAddress(op & 0x00FFFFFC);
		if (Memory::IsValidAddress(target)) {
			UpdatePC(currentList->pc, target - 4);
			currentList->pc = target - 4; // pc will be increased after we return, counteract that
		} else {
			ERROR_LOG_REPORT(G3D, "BJUMP to illegal address %08x - ignoring! data=%06x", target, op & 0x00FFFFFF);
		}
	}
}

void GPUCommon::Execute_Call(u32 op, u32 diff) {
	easy_guard guard(listLock);

	// Saint Seiya needs correct support for relative calls.
	const u32 retval = currentList->pc + 4;
	const u32 target = gstate_c.getRelativeAddress(op & 0x00FFFFFC);

	// Bone matrix optimization - many games will CALL a bone matrix (!).
	if ((Memory::ReadUnchecked_U32(target) >> 24) == GE_CMD_BONEMATRIXDATA) {
		// Check for the end
		if ((Memory::ReadUnchecked_U32(target + 11 * 4) >> 24) == GE_CMD_BONEMATRIXDATA &&
				(Memory::ReadUnchecked_U32(target + 12 * 4) >> 24) == GE_CMD_RET) {
			// Yep, pretty sure this is a bone matrix call.
			FastLoadBoneMatrix(target);
			return;
		}
	}

	if (currentList->stackptr == ARRAY_SIZE(currentList->stack)) {
		ERROR_LOG_REPORT(G3D, "CALL: Stack full!");
	} else if (!Memory::IsValidAddress(target)) {
		ERROR_LOG_REPORT(G3D, "CALL to illegal address %08x - ignoring! data=%06x", target, op & 0x00FFFFFF);
	} else {
		auto &stackEntry = currentList->stack[currentList->stackptr++];
		stackEntry.pc = retval;
		stackEntry.offsetAddr = gstate_c.offsetAddr;
		// The base address is NOT saved/restored for a regular call.
		UpdatePC(currentList->pc, target - 4);
		currentList->pc = target - 4;	// pc will be increased after we return, counteract that
	}
}

void GPUCommon::Execute_Ret(u32 op, u32 diff) {
	easy_guard guard(listLock);
	if (currentList->stackptr == 0) {
		DEBUG_LOG_REPORT(G3D, "RET: Stack empty!");
	} else {
		auto &stackEntry = currentList->stack[--currentList->stackptr];
		gstate_c.offsetAddr = stackEntry.offsetAddr;
		// We always clear the top (uncached/etc.) bits
		const u32 target = stackEntry.pc & 0x0FFFFFFF;
		UpdatePC(currentList->pc, target - 4);
		currentList->pc = target - 4;
		if (!Memory::IsValidAddress(currentList->pc)) {
			ERROR_LOG_REPORT(G3D, "Invalid DL PC %08x on return", currentList->pc);
			UpdateState(GPUSTATE_ERROR);
		}
	}
}

void GPUCommon::Execute_End(u32 op, u32 diff) {
	easy_guard guard(listLock);
	const u32 prev = Memory::ReadUnchecked_U32(currentList->pc - 4);
	UpdatePC(currentList->pc);
	// Count in a few extra cycles on END.
	cyclesExecuted += 60;

	switch (prev >> 24) {
	case GE_CMD_SIGNAL:
		{
			// TODO: see http://code.google.com/p/jpcsp/source/detail?r=2935#
			SignalBehavior behaviour = static_cast<SignalBehavior>((prev >> 16) & 0xFF);
			const int signal = prev & 0xFFFF;
			const int enddata = op & 0xFFFF;
			bool trigger = true;
			currentList->subIntrToken = signal;

			switch (behaviour) {
			case PSP_GE_SIGNAL_HANDLER_SUSPEND:
				// Suspend the list, and call the signal handler.  When it's done, resume.
				// Before sdkver 0x02000010, listsync should return paused.
				if (sceKernelGetCompiledSdkVersion() <= 0x02000010)
					currentList->state = PSP_GE_DL_STATE_PAUSED;
				currentList->signal = behaviour;
				DEBUG_LOG(G3D, "Signal with wait. signal/end: %04x %04x", signal, enddata);
				break;
			case PSP_GE_SIGNAL_HANDLER_CONTINUE:
				// Resume the list right away, then call the handler.
				currentList->signal = behaviour;
				DEBUG_LOG(G3D, "Signal without wait. signal/end: %04x %04x", signal, enddata);
				break;
			case PSP_GE_SIGNAL_HANDLER_PAUSE:
				// Pause the list instead of ending at the next FINISH.
				// Call the handler with the PAUSE signal value at that FINISH.
				// Technically, this ought to trigger an interrupt, but it won't do anything.
				// But right now, signal is always reset by interrupts, so that causes pause to not work.
				trigger = false;
				currentList->signal = behaviour;
				DEBUG_LOG(G3D, "Signal with Pause. signal/end: %04x %04x", signal, enddata);
				break;
			case PSP_GE_SIGNAL_SYNC:
				// Acts as a memory barrier, never calls any user code.
				// Technically, this ought to trigger an interrupt, but it won't do anything.
				// Triggering here can cause incorrect rescheduling, which breaks 3rd Birthday.
				// However, this is likely a bug in how GE signal interrupts are handled.
				trigger = false;
				currentList->signal = behaviour;
				DEBUG_LOG(G3D, "Signal with Sync. signal/end: %04x %04x", signal, enddata);
				break;
			case PSP_GE_SIGNAL_JUMP:
				{
					trigger = false;
					currentList->signal = behaviour;
					// pc will be increased after we return, counteract that.
					u32 target = ((signal << 16) | enddata) - 4;
					if (!Memory::IsValidAddress(target)) {
						ERROR_LOG_REPORT(G3D, "Signal with Jump: bad address. signal/end: %04x %04x", signal, enddata);
					} else {
						UpdatePC(currentList->pc, target);
						currentList->pc = target;
						DEBUG_LOG(G3D, "Signal with Jump. signal/end: %04x %04x", signal, enddata);
					}
				}
				break;
			case PSP_GE_SIGNAL_CALL:
				{
					trigger = false;
					currentList->signal = behaviour;
					// pc will be increased after we return, counteract that.
					u32 target = ((signal << 16) | enddata) - 4;
					if (currentList->stackptr == ARRAY_SIZE(currentList->stack)) {
						ERROR_LOG_REPORT(G3D, "Signal with Call: stack full. signal/end: %04x %04x", signal, enddata);
					} else if (!Memory::IsValidAddress(target)) {
						ERROR_LOG_REPORT(G3D, "Signal with Call: bad address. signal/end: %04x %04x", signal, enddata);
					} else {
						// TODO: This might save/restore other state...
						auto &stackEntry = currentList->stack[currentList->stackptr++];
						stackEntry.pc = currentList->pc;
						stackEntry.offsetAddr = gstate_c.offsetAddr;
						stackEntry.baseAddr = gstate.base;
						UpdatePC(currentList->pc, target);
						currentList->pc = target;
						DEBUG_LOG(G3D, "Signal with Call. signal/end: %04x %04x", signal, enddata);
					}
				}
				break;
			case PSP_GE_SIGNAL_RET:
				{
					trigger = false;
					currentList->signal = behaviour;
					if (currentList->stackptr == 0) {
						ERROR_LOG_REPORT(G3D, "Signal with Return: stack empty. signal/end: %04x %04x", signal, enddata);
					} else {
						// TODO: This might save/restore other state...
						auto &stackEntry = currentList->stack[--currentList->stackptr];
						gstate_c.offsetAddr = stackEntry.offsetAddr;
						gstate.base = stackEntry.baseAddr;
						UpdatePC(currentList->pc, stackEntry.pc);
						currentList->pc = stackEntry.pc;
						DEBUG_LOG(G3D, "Signal with Return. signal/end: %04x %04x", signal, enddata);
					}
				}
				break;
			default:
				ERROR_LOG_REPORT(G3D, "UNKNOWN Signal UNIMPLEMENTED %i ! signal/end: %04x %04x", behaviour, signal, enddata);
				break;
			}
			// TODO: Technically, jump/call/ret should generate an interrupt, but before the pc change maybe?
			if (currentList->interruptsEnabled && trigger) {
				if (__GeTriggerInterrupt(currentList->id, currentList->pc, startingTicks + cyclesExecuted)) {
					currentList->pendingInterrupt = true;
					UpdateState(GPUSTATE_INTERRUPT);
				}
			}
		}
		break;
	case GE_CMD_FINISH:
		switch (currentList->signal) {
		case PSP_GE_SIGNAL_HANDLER_PAUSE:
			currentList->state = PSP_GE_DL_STATE_PAUSED;
			if (currentList->interruptsEnabled) {
				if (__GeTriggerInterrupt(currentList->id, currentList->pc, startingTicks + cyclesExecuted)) {
					currentList->pendingInterrupt = true;
					UpdateState(GPUSTATE_INTERRUPT);
				}
			}
			break;

		case PSP_GE_SIGNAL_SYNC:
			currentList->signal = PSP_GE_SIGNAL_NONE;
			// TODO: Technically this should still cause an interrupt.  Probably for memory sync.
			break;

		default:
			currentList->subIntrToken = prev & 0xFFFF;
			UpdateState(GPUSTATE_DONE);
			if (currentList->interruptsEnabled && __GeTriggerInterrupt(currentList->id, currentList->pc, startingTicks + cyclesExecuted)) {
				currentList->pendingInterrupt = true;
			} else {
				currentList->state = PSP_GE_DL_STATE_COMPLETED;
				currentList->waitTicks = startingTicks + cyclesExecuted;
				busyTicks = std::max(busyTicks, currentList->waitTicks);
				__GeTriggerSync(GPU_SYNC_LIST, currentList->id, currentList->waitTicks);
				if (currentList->started && currentList->context.IsValid()) {
					gstate.Restore(currentList->context);
					ReapplyGfxStateInternal();
				}
			}
			break;
		}
		break;
	default:
		DEBUG_LOG(G3D,"Ah, not finished: %06x", prev & 0xFFFFFF);
		break;
	}
}

void GPUCommon::ExecuteOp(u32 op, u32 diff) {
	const u32 cmd = op >> 24;

	// Handle control and drawing commands here directly. The others we delegate.
	switch (cmd) {
	case GE_CMD_NOP:
		break;

	case GE_CMD_OFFSETADDR:
		Execute_OffsetAddr(op, diff);
		break;

	case GE_CMD_ORIGIN:
		Execute_Origin(op, diff);
		break;

	case GE_CMD_JUMP:
		Execute_Jump(op, diff);
		break;

	case GE_CMD_BJUMP:
		Execute_BJump(op, diff);
		break;

	case GE_CMD_CALL:
		Execute_Call(op, diff);
		break;

	case GE_CMD_RET:
		Execute_Ret(op, diff);
		break;

	case GE_CMD_SIGNAL:
	case GE_CMD_FINISH:
		// Processed in GE_END.
		break;

	case GE_CMD_END:
		Execute_End(op, diff);
		break;

	default:
		DEBUG_LOG(G3D,"DL Unknown: %08x @ %08x", op, currentList == NULL ? 0 : currentList->pc);
		break;
	}
}

void GPUCommon::FastLoadBoneMatrix(u32 target) {
	gstate.FastLoadBoneMatrix(target);
}

struct DisplayListOld {
	int id;
	u32 startpc;
	u32 pc;
	u32 stall;
	DisplayListState state;
	SignalBehavior signal;
	int subIntrBase;
	u16 subIntrToken;
	DisplayListStackEntry stack[32];
	int stackptr;
	bool interrupted;
	u64 waitTicks;
	bool interruptsEnabled;
	bool pendingInterrupt;
	bool started;
	size_t contextPtr;
	u32 offsetAddr;
	bool bboxResult;
};

void GPUCommon::DoState(PointerWrap &p) {
	easy_guard guard(listLock);

	auto s = p.Section("GPUCommon", 1, 2);
	if (!s)
		return;

	p.Do<int>(dlQueue);
	if (s >= 2) {
		p.DoArray(dls, ARRAY_SIZE(dls));
	} else {
		// Can only be in read mode here.
		for (size_t i = 0; i < ARRAY_SIZE(dls); ++i) {
			DisplayListOld oldDL;
			p.Do(oldDL);
			// On 32-bit, they're the same, on 64-bit oldDL is bigger.
			memcpy(&dls[i], &oldDL, sizeof(DisplayList));
			// Fix the other fields.  Let's hope context wasn't important, it was a pointer.
			dls[i].context = 0;
			dls[i].offsetAddr = oldDL.offsetAddr;
			dls[i].bboxResult = oldDL.bboxResult;
		}
	}
	int currentID = 0;
	if (currentList != NULL) {
		ptrdiff_t off = currentList - &dls[0];
		currentID = (int) (off / sizeof(DisplayList));
	}
	p.Do(currentID);
	if (currentID == 0) {
		currentList = NULL;
	} else {
		currentList = &dls[currentID];
	}
	p.Do(interruptRunning);
	p.Do(gpuState);
	p.Do(isbreak);
	p.Do(drawCompleteTicks);
	p.Do(busyTicks);
}

void GPUCommon::InterruptStart(int listid) {
	interruptRunning = true;
}
void GPUCommon::InterruptEnd(int listid) {
	easy_guard guard(listLock);
	interruptRunning = false;
	isbreak = false;

	DisplayList &dl = dls[listid];
	dl.pendingInterrupt = false;
	// TODO: Unless the signal handler could change it?
	if (dl.state == PSP_GE_DL_STATE_COMPLETED || dl.state == PSP_GE_DL_STATE_NONE) {
		if (dl.started && dl.context.IsValid()) {
			gstate.Restore(dl.context);
			ReapplyGfxState();
		}
		dl.waitTicks = 0;
		__GeTriggerWait(GPU_SYNC_LIST, listid);
	}

	guard.unlock();
	ProcessDLQueue();
}

// TODO: Maybe cleaner to keep this in GE and trigger the clear directly?
void GPUCommon::SyncEnd(GPUSyncType waitType, int listid, bool wokeThreads) {
	easy_guard guard(listLock);
	if (waitType == GPU_SYNC_DRAW && wokeThreads)
	{
		for (int i = 0; i < DisplayListMaxCount; ++i) {
			if (dls[i].state == PSP_GE_DL_STATE_COMPLETED) {
				dls[i].state = PSP_GE_DL_STATE_NONE;
			}
		}
	}
}

bool GPUCommon::GetCurrentDisplayList(DisplayList &list) {
	easy_guard guard(listLock);
	if (!currentList) {
		return false;
	}
	list = *currentList;
	return true;
}

std::vector<DisplayList> GPUCommon::ActiveDisplayLists() {
	std::vector<DisplayList> result;

	easy_guard guard(listLock);
	for (auto it = dlQueue.begin(), end = dlQueue.end(); it != end; ++it) {
		result.push_back(dls[*it]);
	}

	return result;
}

void GPUCommon::ResetListPC(int listID, u32 pc) {
	if (listID < 0 || listID >= DisplayListMaxCount) {
		_dbg_assert_msg_(G3D, false, "listID out of range: %d", listID);
		return;
	}

	easy_guard guard(listLock);
	dls[listID].pc = pc;
}

void GPUCommon::ResetListStall(int listID, u32 stall) {
	if (listID < 0 || listID >= DisplayListMaxCount) {
		_dbg_assert_msg_(G3D, false, "listID out of range: %d", listID);
		return;
	}

	easy_guard guard(listLock);
	dls[listID].stall = stall;
}

void GPUCommon::ResetListState(int listID, DisplayListState state) {
	if (listID < 0 || listID >= DisplayListMaxCount) {
		_dbg_assert_msg_(G3D, false, "listID out of range: %d", listID);
		return;
	}

	easy_guard guard(listLock);
	dls[listID].state = state;
}

GPUDebugOp GPUCommon::DissassembleOp(u32 pc, u32 op) {
	char buffer[1024];
	GeDisassembleOp(pc, op, Memory::Read_U32(pc - 4), buffer);

	GPUDebugOp info;
	info.pc = pc;
	info.cmd = op >> 24;
	info.op = op;
	info.desc = buffer;
	return info;
}

std::vector<GPUDebugOp> GPUCommon::DissassembleOpRange(u32 startpc, u32 endpc) {
	char buffer[1024];
	std::vector<GPUDebugOp> result;
	GPUDebugOp info;

	// Don't trigger a pause.
	u32 prev = Memory::IsValidAddress(startpc - 4) ? Memory::Read_U32(startpc - 4) : 0;
	for (u32 pc = startpc; pc < endpc; pc += 4) {
		u32 op = Memory::IsValidAddress(pc) ? Memory::Read_U32(pc) : 0;
		GeDisassembleOp(pc, op, prev, buffer);
		prev = op;

		info.pc = pc;
		info.cmd = op >> 24;
		info.op = op;
		info.desc = buffer;
		result.push_back(info);
	}
	return result;
}

u32 GPUCommon::GetRelativeAddress(u32 data) {
	return gstate_c.getRelativeAddress(data);
}

u32 GPUCommon::GetVertexAddress() {
	return gstate_c.vertexAddr;
}

u32 GPUCommon::GetIndexAddress() {
	return gstate_c.indexAddr;
}

GPUgstate GPUCommon::GetGState() {
	return gstate;
}

void GPUCommon::SetCmdValue(u32 op) {
	u32 cmd = op >> 24;
	u32 diff = op ^ gstate.cmdmem[cmd];

	PreExecuteOp(op, diff);
	gstate.cmdmem[cmd] = op;
	ExecuteOp(op, diff);
}
