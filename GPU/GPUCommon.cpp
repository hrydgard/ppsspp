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
#include "Core/HLE/sceKernelMemory.h"
#include "Core/HLE/sceKernelInterrupt.h"
#include "Core/HLE/sceGe.h"

GPUCommon::GPUCommon() :
	currentList(NULL),
	isbreak(false),
	drawCompleteTicks(0),
	busyTicks(0),
	dumpNextFrame_(false),
	dumpThisFrame_(false),
	interruptsEnabled_(true),
	curTickEst_(0)
{
	memset(dls, 0, sizeof(dls));
	for (int i = 0; i < DisplayListMaxCount; ++i) {
		dls[i].state = PSP_GE_DL_STATE_NONE;
		dls[i].waitTicks = 0;
	}
	SetThreadEnabled(g_Config.bSeparateCPUThread);
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

u32 GPUCommon::DrawSync(int mode) {
	// FIXME: Workaround for displaylists sometimes hanging unprocessed.  Not yet sure of the cause.
	if (g_Config.bSeparateCPUThread) {
		// FIXME: Workaround for displaylists sometimes hanging unprocessed.  Not yet sure of the cause.
		ScheduleEvent(GPU_EVENT_PROCESS_QUEUE);
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
			__GeWaitCurrentThread(WAITTYPE_GEDRAWSYNC, 1, "GeDrawSync");
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
		// FIXME: Workaround for displaylists sometimes hanging unprocessed.  Not yet sure of the cause.
		ScheduleEvent(GPU_EVENT_PROCESS_QUEUE);
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
		__GeWaitCurrentThread(WAITTYPE_GELISTSYNC, listid, "GeListSync");
	}
	return PSP_GE_LIST_COMPLETED;
}

u32 GPUCommon::EnqueueList(u32 listpc, u32 stall, int subIntrBase, bool head) {
	easy_guard guard(listLock);
	// TODO Check the stack values in missing arg and ajust the stack depth

	// Check alignment
	// TODO Check the context and stack alignement too
	if (((listpc | stall) & 3) != 0)
		return 0x80000103;

	int id = -1;
	bool oldCompatibility = true;
	if (sceKernelGetCompiledSdkVersion() > 0x01FFFFFF) {
		//numStacks = 0;
		//stack = NULL;
		oldCompatibility = false;
	}

	u64 currentTicks = CoreTiming::GetTicks();
	for (int i = 0; i < DisplayListMaxCount; ++i)
	{
		if (dls[i].state != PSP_GE_DL_STATE_NONE && dls[i].state != PSP_GE_DL_STATE_COMPLETED) {
			if (dls[i].pc == listpc && !oldCompatibility) {
				ERROR_LOG(G3D, "sceGeListEnqueue: can't enqueue, list address %08X already used", listpc);
				return 0x80000021;
			}
			//if(dls[i].stack == stack) {
			//	ERROR_LOG(G3D, "sceGeListEnqueue: can't enqueue, list stack %08X already used", context);
			//	return 0x80000021;
			//}
		}
		if (dls[i].state == PSP_GE_DL_STATE_NONE && !dls[i].pendingInterrupt)
		{
			// Prefer a list that isn't used
			id = i;
			break;
		}
		if (id < 0 && dls[i].state == PSP_GE_DL_STATE_COMPLETED && !dls[i].pendingInterrupt && dls[i].waitTicks < currentTicks)
		{
			id = i;
		}
	}
	if (id < 0)
	{
		ERROR_LOG_REPORT(G3D, "No DL ID available to enqueue");
		for(auto it = dlQueue.begin(); it != dlQueue.end(); ++it) {
			DisplayList &dl = dls[*it];
			DEBUG_LOG(G3D, "DisplayList %d status %d pc %08x stall %08x", *it, dl.state, dl.pc, dl.stall);
		}
		return SCE_KERNEL_ERROR_OUT_OF_MEMORY;
	}

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

	if (dls[listid].state == PSP_GE_DL_STATE_RUNNING || dls[listid].state == PSP_GE_DL_STATE_PAUSED)
		return 0x80000021;

	dls[listid].state = PSP_GE_DL_STATE_NONE;

	if (listid == dlQueue.front())
		PopDLQueue();
	else
		dlQueue.remove(listid);

	dls[listid].waitTicks = 0;
	__GeTriggerWait(WAITTYPE_GELISTSYNC, listid);

	CheckDrawSync();

	return 0;
}

u32 GPUCommon::UpdateStall(int listid, u32 newstall) {
	easy_guard guard(listLock);
	if (listid < 0 || listid >= DisplayListMaxCount || dls[listid].state == PSP_GE_DL_STATE_NONE)
		return SCE_KERNEL_ERROR_INVALID_ID;

	dls[listid].stall = newstall & 0x0FFFFFFF;

	if (dls[listid].signal == PSP_GE_SIGNAL_HANDLER_PAUSE)
		dls[listid].signal = PSP_GE_SIGNAL_HANDLER_SUSPEND;
	
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
			if (currentList->signal == PSP_GE_SIGNAL_HANDLER_PAUSE)
				return 0x80000021;

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
		return 0x80000020;

	if (mode == 1)
	{
		// Clear the queue
		dlQueue.clear();
		for (int i = 0; i < DisplayListMaxCount; ++i)
		{
			dls[i].state = PSP_GE_DL_STATE_NONE;
			dls[i].signal = PSP_GE_SIGNAL_NONE;
		}

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
				return 0x80000020;
		}
		return 0x80000021;
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

bool GPUCommon::InterpretList(DisplayList &list) {
	// Initialized to avoid a race condition with bShowDebugStats changing.
	double start = 0.0;
	if (g_Config.bShowDebugStats) {
		time_update();
		start = time_now_d();
	}

	easy_guard guard(listLock);

	// TODO: This has to be right... but it freezes right now?
	//if (list.state == PSP_GE_DL_STATE_PAUSED)
	//	return false;
	currentList = &list;

	// I don't know if this is the correct place to zero this, but something
	// need to do it. See Sol Trigger title screen.
	// TODO: Maybe this is per list?  Should a stalled list remember the old value?
	gstate_c.offsetAddr = 0;

	if (!Memory::IsValidAddress(list.pc)) {
		ERROR_LOG_REPORT(G3D, "DL PC = %08x WTF!!!!", list.pc);
		return true;
	}

#if defined(USING_QT_UI)
	if (host->GpuStep()) {
		host->SendGPUStart();
	}
#endif

	cycleLastPC = list.pc;
	downcount = list.stall == 0 ? 0x0FFFFFFF : (list.stall - list.pc) / 4;
	list.state = PSP_GE_DL_STATE_RUNNING;
	list.interrupted = false;

	gpuState = list.pc == list.stall ? GPUSTATE_STALL : GPUSTATE_RUNNING;
	guard.unlock();

	const bool dumpThisFrame = dumpThisFrame_;
	// TODO: Add check for displaylist debugger.
	const bool useFastRunLoop = !dumpThisFrame;
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

	if (g_Config.bShowDebugStats) {
		time_update();
		gpuStats.msProcessingDisplayLists += time_now_d() - start;
	}
	return gpuState == GPUSTATE_DONE || gpuState == GPUSTATE_ERROR;
}

void GPUCommon::SlowRunLoop(DisplayList &list)
{
	const bool dumpThisFrame = dumpThisFrame_;
	while (downcount > 0)
	{
		u32 op = Memory::ReadUnchecked_U32(list.pc);
		u32 cmd = op >> 24;

#if defined(USING_QT_UI)
		if (host->GpuStep())
			host->SendGPUWait(cmd, list.pc, &gstate);
#endif

		u32 diff = op ^ gstate.cmdmem[cmd];
		PreExecuteOp(op, diff);
		if (dumpThisFrame) {
			char temp[256];
			u32 prev = Memory::ReadUnchecked_U32(list.pc - 4);
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
inline void GPUCommon::UpdatePC(u32 currentPC, u32 newPC) {
	// Rough estimate, 2 CPU ticks (it's double the clock rate) per GPU instruction.
	int executed = (currentPC - cycleLastPC) / 4;
	cyclesExecuted += 2 * executed;
	gpuStats.otherGPUCycles += 2 * executed;
	cycleLastPC = newPC == 0 ? currentPC : newPC;

	gpuStats.gpuCommandsAtCallLevel[std::min(currentList->stackptr, 3)] += executed;

	// Exit the runloop and recalculate things.  This isn't common.
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
	/*
	ExecuteOp(gstate.cmdmem[GE_CMD_ALPHABLENDENABLE], 0xFFFFFFFF);
	ExecuteOp(gstate.cmdmem[GE_CMD_ALPHATESTENABLE], 0xFFFFFFFF);
	ExecuteOp(gstate.cmdmem[GE_CMD_BLENDMODE], 0xFFFFFFFF);
	ExecuteOp(gstate.cmdmem[GE_CMD_ZTEST], 0xFFFFFFFF);
	ExecuteOp(gstate.cmdmem[GE_CMD_ZTESTENABLE], 0xFFFFFFFF);
	ExecuteOp(gstate.cmdmem[GE_CMD_CULL], 0xFFFFFFFF);
	ExecuteOp(gstate.cmdmem[GE_CMD_CULLFACEENABLE], 0xFFFFFFFF);
	ExecuteOp(gstate.cmdmem[GE_CMD_SCISSOR1], 0xFFFFFFFF);
	ExecuteOp(gstate.cmdmem[GE_CMD_SCISSOR2], 0xFFFFFFFF);
	*/

	for (int i = GE_CMD_VERTEXTYPE; i < GE_CMD_BONEMATRIXNUMBER; i++) {
		if (i != GE_CMD_ORIGIN) {
			ExecuteOp(gstate.cmdmem[i], 0xFFFFFFFF);
		}
	}

	// Can't write to bonematrixnumber here

	for (int i = GE_CMD_MORPHWEIGHT0; i < GE_CMD_PATCHFACING; i++) {
		ExecuteOp(gstate.cmdmem[i], 0xFFFFFFFF);
	}

	// There are a few here in the middle that we shouldn't execute...

	for (int i = GE_CMD_VIEWPORTX1; i < GE_CMD_TRANSFERSTART; i++) {
		ExecuteOp(gstate.cmdmem[i], 0xFFFFFFFF);
	}

	// TODO: there's more...
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
			// At the end, we can remove it from the queue and continue.
			dlQueue.erase(std::remove(dlQueue.begin(), dlQueue.end(), listIndex), dlQueue.end());
			UpdateTickEstimate(std::max(busyTicks, startingTicks + cyclesExecuted));
		}
	}

	easy_guard guard(listLock);
	currentList = NULL;

	drawCompleteTicks = startingTicks + cyclesExecuted;
	busyTicks = std::max(busyTicks, drawCompleteTicks);
	__GeTriggerSync(WAITTYPE_GEDRAWSYNC, 1, drawCompleteTicks);
	// Since the event is in CoreTiming, we're in sync.  Just set 0 now.
	UpdateTickEstimate(0);
}

void GPUCommon::PreExecuteOp(u32 op, u32 diff) {
	// Nothing to do
}

void GPUCommon::ExecuteOp(u32 op, u32 diff) {
	u32 cmd = op >> 24;
	u32 data = op & 0xFFFFFF;

	// Handle control and drawing commands here directly. The others we delegate.
	switch (cmd) {
	case GE_CMD_NOP:
		break;

	case GE_CMD_OFFSETADDR:
		gstate_c.offsetAddr = data << 8;
		break;

	case GE_CMD_ORIGIN:
		{
			easy_guard guard(listLock);
			gstate_c.offsetAddr = currentList->pc;
		}
		break;

	case GE_CMD_JUMP:
		{
			easy_guard guard(listLock);
			u32 target = gstate_c.getRelativeAddress(data);
			if (Memory::IsValidAddress(target)) {
				UpdatePC(currentList->pc, target - 4);
				currentList->pc = target - 4; // pc will be increased after we return, counteract that
			} else {
				ERROR_LOG_REPORT(G3D, "JUMP to illegal address %08x - ignoring! data=%06x", target, data);
			}
		}
		break;

	case GE_CMD_CALL:
		{
			easy_guard guard(listLock);
			// Saint Seiya needs correct support for relative calls.
			u32 retval = currentList->pc + 4;
			u32 target = gstate_c.getRelativeAddress(data);
			if (currentList->stackptr == ARRAY_SIZE(currentList->stack)) {
				ERROR_LOG_REPORT(G3D, "CALL: Stack full!");
			} else if (!Memory::IsValidAddress(target)) {
				ERROR_LOG_REPORT(G3D, "CALL to illegal address %08x - ignoring! data=%06x", target, data);
			} else {
				auto &stackEntry = currentList->stack[currentList->stackptr++];
				stackEntry.pc = retval;
				stackEntry.offsetAddr = gstate_c.offsetAddr;
				UpdatePC(currentList->pc, target - 4);
				currentList->pc = target - 4;	// pc will be increased after we return, counteract that
			}
		}
		break;

	case GE_CMD_RET:
		{
			easy_guard guard(listLock);
			if (currentList->stackptr == 0) {
				ERROR_LOG_REPORT(G3D, "RET: Stack empty!");
			} else {
				auto &stackEntry = currentList->stack[--currentList->stackptr];
				gstate_c.offsetAddr = stackEntry.offsetAddr;
				u32 target = (currentList->pc & 0xF0000000) | (stackEntry.pc & 0x0FFFFFFF);
				UpdatePC(currentList->pc, target - 4);
				currentList->pc = target - 4;
				if (!Memory::IsValidAddress(currentList->pc)) {
					ERROR_LOG_REPORT(G3D, "Invalid DL PC %08x on return", currentList->pc);
					UpdateState(GPUSTATE_ERROR);
				}
			}
		}
		break;

	case GE_CMD_SIGNAL:
	case GE_CMD_FINISH:
		// Processed in GE_END.
		break;

	case GE_CMD_END: {
		easy_guard guard(listLock);
		u32 prev = Memory::ReadUnchecked_U32(currentList->pc - 4);
		UpdatePC(currentList->pc);
		switch (prev >> 24) {
		case GE_CMD_SIGNAL:
			{
				// TODO: see http://code.google.com/p/jpcsp/source/detail?r=2935#
				SignalBehavior behaviour = static_cast<SignalBehavior>((prev >> 16) & 0xFF);
				int signal = prev & 0xFFFF;
				int enddata = data & 0xFFFF;
				bool trigger = true;
				currentList->subIntrToken = signal;

				switch (behaviour) {
				case PSP_GE_SIGNAL_HANDLER_SUSPEND:
					if (sceKernelGetCompiledSdkVersion() <= 0x02000010)
						currentList->state = PSP_GE_DL_STATE_PAUSED;
					currentList->signal = behaviour;
					DEBUG_LOG(G3D, "Signal with Wait UNIMPLEMENTED! signal/end: %04x %04x", signal, enddata);
					break;
				case PSP_GE_SIGNAL_HANDLER_CONTINUE:
					currentList->signal = behaviour;
					DEBUG_LOG(G3D, "Signal without wait. signal/end: %04x %04x", signal, enddata);
					break;
				case PSP_GE_SIGNAL_HANDLER_PAUSE:
					currentList->state = PSP_GE_DL_STATE_PAUSED;
					currentList->signal = behaviour;
					ERROR_LOG_REPORT(G3D, "Signal with Pause UNIMPLEMENTED! signal/end: %04x %04x", signal, enddata);
					break;
				case PSP_GE_SIGNAL_SYNC:
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
				currentList->state = PSP_GE_DL_STATE_COMPLETED;
				UpdateState(GPUSTATE_DONE);
				if (currentList->interruptsEnabled && __GeTriggerInterrupt(currentList->id, currentList->pc, startingTicks + cyclesExecuted)) {
					currentList->pendingInterrupt = true;
				} else {
					currentList->waitTicks = startingTicks + cyclesExecuted;
					busyTicks = std::max(busyTicks, currentList->waitTicks);
					__GeTriggerSync(WAITTYPE_GELISTSYNC, currentList->id, currentList->waitTicks);
				}
				break;
			}
			break;
		default:
			DEBUG_LOG(G3D,"Ah, not finished: %06x", prev & 0xFFFFFF);
			break;
		}
		break;
	}

	default:
		DEBUG_LOG(G3D,"DL Unknown: %08x @ %08x", op, currentList == NULL ? 0 : currentList->pc);
		break;
	}
}

void GPUCommon::DoState(PointerWrap &p) {
	easy_guard guard(listLock);

	p.Do<int>(dlQueue);
	p.DoArray(dls, ARRAY_SIZE(dls));
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
	p.DoMarker("GPUCommon");
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
		dl.waitTicks = 0;
		__GeTriggerWait(WAITTYPE_GELISTSYNC, listid);
	}

	if (dl.signal == PSP_GE_SIGNAL_HANDLER_PAUSE)
		dl.signal = PSP_GE_SIGNAL_HANDLER_SUSPEND;

	guard.unlock();
	ProcessDLQueue();
}

// TODO: Maybe cleaner to keep this in GE and trigger the clear directly?
void GPUCommon::SyncEnd(WaitType waitType, int listid, bool wokeThreads) {
	easy_guard guard(listLock);
	if (waitType == WAITTYPE_GEDRAWSYNC && wokeThreads)
	{
		for (int i = 0; i < DisplayListMaxCount; ++i) {
			if (dls[i].state == PSP_GE_DL_STATE_COMPLETED) {
				dls[i].state = PSP_GE_DL_STATE_NONE;
			}
		}
	}
}
