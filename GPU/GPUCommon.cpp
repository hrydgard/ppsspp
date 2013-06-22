#include <algorithm>
#include "base/timeutil.h"
#include "GeDisasm.h"
#include "GPUCommon.h"
#include "GPUState.h"
#include "ChunkFile.h"
#include "Core/Config.h"
#include "Core/CoreTiming.h"
#include "Core/MemMap.h"
#include "Core/Host.h"
#include "Core/Reporting.h"
#include "Core/HLE/sceKernelInterrupt.h"
#include "Core/HLE/sceKernelMemory.h"
#include "Core/HLE/sceKernelThread.h"
#include "Core/HLE/sceGe.h"

GPUCommon::GPUCommon() :
	currentList(NULL),
	isbreak(false),
	drawCompleteTicks(0),
	busyTicks(0),
	dumpNextFrame_(false),
	dumpThisFrame_(false),
	interruptsEnabled_(true)
{
	memset(dls, 0, sizeof(dls));
	for (int i = 0; i < DisplayListMaxCount; ++i) {
		dls[i].state = PSP_GE_DL_STATE_NONE;
		dls[i].waitTicks = 0;
	}
}

void GPUCommon::PopDLQueue() {
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
	if (mode < 0 || mode > 1)
		return SCE_KERNEL_ERROR_INVALID_MODE;

	if (mode == 0) {
		// TODO: What if dispatch / interrupts disabled?
		if (drawCompleteTicks > CoreTiming::GetTicks()) {
			__KernelWaitCurThread(WAITTYPE_GEDRAWSYNC, 1, 0, 0, false, "GeDrawSync");
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

void GPUCommon::CheckDrawSync()
{
	if (dlQueue.empty()) {
		for (int i = 0; i < DisplayListMaxCount; ++i)
			dls[i].state = PSP_GE_DL_STATE_NONE;
	}
}

int GPUCommon::ListSync(int listid, int mode)
{
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

	if (dl.waitTicks > CoreTiming::GetTicks()) {
		__KernelWaitCurThread(WAITTYPE_GELISTSYNC, listid, 0, 0, false, "GeListSync");
	}
	return PSP_GE_LIST_COMPLETED;
}

u32 GPUCommon::EnqueueList(u32 listpc, u32 stall, int subIntrBase, bool head)
{
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
		if (dls[i].state == PSP_GE_DL_STATE_NONE)
		{
			// Prefer a list that isn't used
			id = i;
			break;
		}
		if (id < 0 && dls[i].state == PSP_GE_DL_STATE_COMPLETED && dls[i].waitTicks < currentTicks)
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
	dl.startpc = listpc & 0xFFFFFFF;
	dl.pc = listpc & 0xFFFFFFF;
	dl.stall = stall & 0xFFFFFFF;
	dl.subIntrBase = std::max(subIntrBase, -1);
	dl.stackptr = 0;
	dl.signal = PSP_GE_SIGNAL_NONE;
	dl.interrupted = false;
	dl.waitTicks = (u64)-1;

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
		ProcessDLQueue();
	}

	return id;
}

u32 GPUCommon::DequeueList(int listid)
{
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
	__KernelTriggerWait(WAITTYPE_GELISTSYNC, listid, 0, "GeListSync");

	CheckDrawSync();

	return 0;
}

u32 GPUCommon::UpdateStall(int listid, u32 newstall)
{
	if (listid < 0 || listid >= DisplayListMaxCount || dls[listid].state == PSP_GE_DL_STATE_NONE)
		return SCE_KERNEL_ERROR_INVALID_ID;

	dls[listid].stall = newstall & 0xFFFFFFF;

	if (dls[listid].signal == PSP_GE_SIGNAL_HANDLER_PAUSE)
		dls[listid].signal = PSP_GE_SIGNAL_HANDLER_SUSPEND;
	
	ProcessDLQueue();

	return 0;
}

u32 GPUCommon::Continue()
{
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

	ProcessDLQueue();
	return 0;
}

u32 GPUCommon::Break(int mode)
{
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

bool GPUCommon::InterpretList(DisplayList &list)
{
	// Initialized to avoid a race condition with bShowDebugStats changing.
	double start = 0.0;
	if (g_Config.bShowDebugStats)
	{
		time_update();
		start = time_now_d();
	}

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
	if(host->GpuStep())
	{
		host->SendGPUStart();
	}
#endif

	cycleLastPC = list.pc;
	downcount = list.stall == 0 ? 0xFFFFFFF : (list.stall - list.pc) / 4;
	list.state = PSP_GE_DL_STATE_RUNNING;
	list.interrupted = false;

	gpuState = list.pc == list.stall ? GPUSTATE_STALL : GPUSTATE_RUNNING;

	const bool dumpThisFrame = dumpThisFrame_;
	// TODO: Add check for displaylist debugger.
	const bool useFastRunLoop = !dumpThisFrame;
	while (gpuState == GPUSTATE_RUNNING)
	{
		if (list.pc == list.stall)
		{
			gpuState = GPUSTATE_STALL;
			downcount = 0;
		}

		if (useFastRunLoop)
			FastRunLoop(list);
		else
			SlowRunLoop(list);

		downcount = list.stall == 0 ? 0xFFFFFFF : (list.stall - list.pc) / 4;
	}

	// We haven't run the op at list.pc, so it shouldn't count.
	if (cycleLastPC != list.pc)
		UpdatePC(list.pc - 4, list.pc);

	if (g_Config.bShowDebugStats)
	{
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
			NOTICE_LOG(HLE, "%s", temp);
		}
		gstate.cmdmem[cmd] = op;

		ExecuteOp(op, diff);

		list.pc += 4;
		--downcount;
	}
}

// The newPC parameter is used for jumps, we don't count cycles between.
inline void GPUCommon::UpdatePC(u32 currentPC, u32 newPC)
{
	// Rough estimate, 2 CPU ticks (it's double the clock rate) per GPU instruction.
	cyclesExecuted += 2 * (currentPC - cycleLastPC) / 4;
	gpuStats.otherGPUCycles += 2 * (currentPC - cycleLastPC) / 4;
	cycleLastPC = newPC == 0 ? currentPC : newPC;

	// Exit the runloop and recalculate things.  This isn't common.
	downcount = 0;
}

inline void GPUCommon::UpdateState(GPUState state)
{
	gpuState = state;
	if (state != GPUSTATE_RUNNING)
		downcount = 0;
}

bool GPUCommon::ProcessDLQueue()
{
	startingTicks = CoreTiming::GetTicks();
	cyclesExecuted = 0;

	if (startingTicks < busyTicks)
	{
		DEBUG_LOG(HLE, "Can't execute a list yet, still busy for %lld ticks", busyTicks - startingTicks);
		return false;
	}

	DisplayListQueue::iterator iter = dlQueue.begin();
	while (iter != dlQueue.end())
	{
		DisplayList &l = dls[*iter];
		DEBUG_LOG(G3D,"Okay, starting DL execution at %08x - stall = %08x", l.pc, l.stall);
		if (!InterpretList(l))
		{
			return false;
		}
		else
		{
			//At the end, we can remove it from the queue and continue
			dlQueue.erase(iter);
			//this invalidated the iterator, let's fix it
			iter = dlQueue.begin();
		}
	}
	currentList = NULL;

	drawCompleteTicks = startingTicks + cyclesExecuted;
	busyTicks = std::max(busyTicks, drawCompleteTicks);
	__GeTriggerSync(WAITTYPE_GEDRAWSYNC, 1, drawCompleteTicks);

	return true; //no more lists!
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
		gstate_c.offsetAddr = currentList->pc;
		break;

	case GE_CMD_JUMP:
		{
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
					ERROR_LOG(G3D, "Signal with Wait UNIMPLEMENTED! signal/end: %04x %04x", signal, enddata);
					break;
				case PSP_GE_SIGNAL_HANDLER_CONTINUE:
					currentList->signal = behaviour;
					ERROR_LOG(G3D, "Signal without wait. signal/end: %04x %04x", signal, enddata);
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
				if (interruptsEnabled_ && trigger) {
					if (__GeTriggerInterrupt(currentList->id, currentList->pc, startingTicks + cyclesExecuted))
						UpdateState(GPUSTATE_INTERRUPT);
				}
			}
			break;
		case GE_CMD_FINISH:
			switch (currentList->signal) {
			case PSP_GE_SIGNAL_HANDLER_PAUSE:
				if (interruptsEnabled_) {
					if (__GeTriggerInterrupt(currentList->id, currentList->pc, startingTicks + cyclesExecuted))
						UpdateState(GPUSTATE_INTERRUPT);
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
				if (!interruptsEnabled_ || !__GeTriggerInterrupt(currentList->id, currentList->pc, startingTicks + cyclesExecuted)) {
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
	u32 prev;  // TODO: kill. just didn't want to break states right now...
	p.Do(prev);
	p.Do(gpuState);
	p.Do(isbreak);
	p.Do(drawCompleteTicks);
	p.Do(busyTicks);
	p.DoMarker("GPUCommon");
}

void GPUCommon::InterruptStart(int listid)
{
	interruptRunning = true;
}
void GPUCommon::InterruptEnd(int listid)
{
	interruptRunning = false;
	isbreak = false;

	DisplayList &dl = dls[listid];
	// TODO: Unless the signal handler could change it?
	if (dl.state == PSP_GE_DL_STATE_COMPLETED || dl.state == PSP_GE_DL_STATE_NONE) {
		dl.waitTicks = 0;
		__KernelTriggerWait(WAITTYPE_GELISTSYNC, listid, 0, "GeListSync", true);
	}

	if (dl.signal == PSP_GE_SIGNAL_HANDLER_PAUSE)
		dl.signal = PSP_GE_SIGNAL_HANDLER_SUSPEND;

	ProcessDLQueue();
}

// TODO: Maybe cleaner to keep this in GE and trigger the clear directly?
void GPUCommon::SyncEnd(WaitType waitType, int listid, bool wokeThreads)
{
	if (waitType == WAITTYPE_GEDRAWSYNC && wokeThreads)
	{
		for (int i = 0; i < DisplayListMaxCount; ++i) {
			if (dls[i].state == PSP_GE_DL_STATE_COMPLETED) {
				dls[i].state = PSP_GE_DL_STATE_NONE;
			}
		}
	}
}
