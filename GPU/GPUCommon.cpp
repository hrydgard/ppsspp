#include <algorithm>
#include "base/timeutil.h"
#include "GeDisasm.h"
#include "GPUCommon.h"
#include "GPUState.h"
#include "ChunkFile.h"
#include "Core/CoreTiming.h"
#include "Core/MemMap.h"
#include "Core/Host.h"
#include "Core/Reporting.h"
#include "Core/HLE/sceKernelInterrupt.h"
#include "Core/HLE/sceKernelMemory.h"
#include "Core/HLE/sceKernelThread.h"
#include "Core/HLE/sceGe.h"

void init() {
}

GPUCommon::GPUCommon() :
	interruptRunning(false),
	drawSyncWait(false),
	dumpNextFrame_(false),
	dumpThisFrame_(false),
	running(true),
	isbreak(false),
	pc(0),
	stall(0),
	currentDisplayList(NULL)
{
	for (int i = 0; i < DisplayListMaxCount; ++i)
		dls[i].state = PSP_GE_DL_STATE_NONE;
}

void GPUCommon::PopDLQueue() {
	if(!dlQueue.empty()) {
		dlQueue.pop_front();
		if(!dlQueue.empty()) {
			currentDisplayList = &dls[dlQueue.front()];
			if (running)
				currentDisplayList->state = PSP_GE_DL_STATE_RUNNING;
			pc = currentDisplayList->pc;
			stall = currentDisplayList->stall;
		} else {
			currentDisplayList = NULL;
			pc = 0;
			stall = 0;
		}
	}
}

u32 GPUCommon::DrawSync(int mode) {
	if (mode < 0 || mode > 1)
		return SCE_KERNEL_ERROR_INVALID_MODE;

	while (!dlQueue.empty() && currentList()->state == PSP_GE_DL_STATE_COMPLETED)
		PopDLQueue();

	CheckDrawSync();

	if (mode == 0) {
		// geman uses an event flag here to wait for an empty queue
		if(dlQueue.empty()) {
			for(int i = 0; i < DisplayListMaxCount; ++i)
			{
				dls[i].state = PSP_GE_DL_STATE_NONE;
			}
			return 0;
		}

		drawSyncWait = true;
		__KernelWaitCurThread(WAITTYPE_GEDRAWSYNC, 0, 0, 0, false, "GeDrawSync");
		ERROR_LOG(HLE, "Blocking thread on DrawSync");

		return 0;
	}

	if (!currentDisplayList)
		return PSP_GE_LIST_COMPLETED;

	if (pc == currentDisplayList->stall)
		return PSP_GE_LIST_STALLING;

	return PSP_GE_LIST_DRAWING;
}

void GPUCommon::CheckDrawSync()
{
	if(dlQueue.empty() && drawSyncWait) {
		drawSyncWait = false;
		__KernelTriggerWait(WAITTYPE_GEDRAWSYNC, 0, 0, false);
		for(int i = 0; i < DisplayListMaxCount; ++i)
		{
			dls[i].state = PSP_GE_DL_STATE_NONE;
		}
	}
}

int GPUCommon::ListSync(int listid, int mode)
{
	if (listid < 0 || listid >= DisplayListMaxCount)
		return SCE_KERNEL_ERROR_INVALID_ID;

	if (mode < 0 || mode > 1)
		return SCE_KERNEL_ERROR_INVALID_MODE;

	if (mode == 1) {
		DisplayList& dl = dls[listid];

		switch (dl.state) {
		case PSP_GE_DL_STATE_QUEUED:
			if (dl.interrupted)
				return PSP_GE_LIST_PAUSED;
			return PSP_GE_LIST_QUEUED;

		case PSP_GE_DL_STATE_RUNNING:
			if (pc == dl.stall)
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

	if(dls[listid].state != PSP_GE_DL_STATE_COMPLETED)
	{
		dls[listid].threadWaiting = true;
		__KernelWaitCurThread(WAITTYPE_GELISTSYNC, listid, 0, 0, false, "GeListSync");
		ERROR_LOG(HLE, "Blocking thread on ListSync");
	}

	return PSP_GE_LIST_COMPLETED;
}

u32 GPUCommon::EnqueueList(u32 listpc, u32 stalladdr, int subIntrBase, bool head)
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

	// Uncached address
	listpc = listpc & 0x1FFFFFFF;

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
		if (id < 0 && dls[i].state == PSP_GE_DL_STATE_COMPLETED)
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
	dl.startpc = listpc;
	dl.pc = listpc;
	dl.stall = stalladdr & 0x1FFFFFFF;
	dl.subIntrBase = std::max(subIntrBase, -1);
	dl.stackptr = 0;
	dl.signal = PSP_GE_SIGNAL_NONE;
	dl.interrupted = false;
	dl.threadWaiting = false;

	if (head) {
		if (currentDisplayList) {
			if (currentDisplayList->state != PSP_GE_DL_STATE_PAUSED)
				return SCE_KERNEL_ERROR_INVALID_VALUE;
			currentDisplayList->state = PSP_GE_DL_STATE_QUEUED;
		}

		dl.state = PSP_GE_DL_STATE_PAUSED;

		currentDisplayList = &dls[id];
		dlQueue.push_front(id);
	} else if (currentDisplayList) {
		dl.state = PSP_GE_DL_STATE_QUEUED;
		dlQueue.push_back(id);
	} else {
		dl.state = PSP_GE_DL_STATE_RUNNING;
		currentDisplayList = &dls[id];
		dlQueue.push_front(id);

		pc = dl.pc;
		stall = dl.stall;
		running = true;

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

	if(dls[listid].threadWaiting) {
		dls[listid].threadWaiting = false;
		__KernelTriggerWait(WAITTYPE_GELISTSYNC, listid, "GeDrawSync");
	}

	CheckDrawSync();

	return 0;
}

u32 GPUCommon::UpdateStall(int listid, u32 newstall)
{
	if (listid < 0 || listid >= DisplayListMaxCount || dls[listid].state == PSP_GE_DL_STATE_NONE)
		return SCE_KERNEL_ERROR_INVALID_ID;

	if (dls[listid].state == PSP_GE_DL_STATE_COMPLETED)
		return 0x80000020;

	newstall = newstall & 0x1FFFFFFF;

	if(dls[listid].state == PSP_GE_DL_STATE_RUNNING) {
		stall = newstall;
		dls[listid].stall = newstall;
	} else {
		// List is PAUSED or QUEUED
		// When the list is PAUSED because of PSP_GE_SIGNAL_HANDLER_SUSPEND,
		// we don't refresh the global stall, this is an intentional compatibility bug
		dls[listid].stall = newstall;
	}

	ProcessDLQueue();

	return 0;
}

u32 GPUCommon::Continue()
{
	if (!currentDisplayList)
		return 0;

	if (currentDisplayList->state == PSP_GE_DL_STATE_PAUSED)
	{
		if (true || !isbreak)
		{
			if (currentDisplayList->signal == PSP_GE_SIGNAL_HANDLER_PAUSE)
				return 0x80000021;

			currentDisplayList->state = PSP_GE_DL_STATE_RUNNING;
			currentDisplayList->signal = PSP_GE_SIGNAL_NONE;

			// TODO Restore context of DL is necessary
			// TODO Restore BASE
			pc = currentDisplayList->pc;
			stall = currentDisplayList->stall;
			running = true;
		}
		else
			currentDisplayList->state = PSP_GE_DL_STATE_QUEUED;
	}
	else if (currentDisplayList->state == PSP_GE_DL_STATE_RUNNING)
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

	if (!currentDisplayList)
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

		currentDisplayList = NULL;
		pc = 0;
		stall = 0;
		running = false;

		return 0;
	}

	if (currentDisplayList->state == PSP_GE_DL_STATE_NONE || currentDisplayList->state == PSP_GE_DL_STATE_COMPLETED)
	{
		if (sceKernelGetCompiledSdkVersion() >= 0x02000000)
			return 0x80000004;
		return -1;
	}

	if (currentDisplayList->state == PSP_GE_DL_STATE_PAUSED)
	{
		if (sceKernelGetCompiledSdkVersion() > 0x02000010)
		{
			if (currentDisplayList->signal == PSP_GE_SIGNAL_HANDLER_PAUSE)
			{
				ERROR_LOG_REPORT(G3D, "sceGeBreak: can't break signal-pausing list");
			}
			else
				return 0x80000020;
		}
		return 0x80000021;
	}

	if (currentDisplayList->state == PSP_GE_DL_STATE_QUEUED)
	{
		currentDisplayList->state = PSP_GE_DL_STATE_PAUSED;
		return dlQueue.front();
	}

	running = false;
	currentDisplayList->pc = pc;
	currentDisplayList->stall = stall;

	// TODO Save BASE
	// TODO Adjust pc to be just before SIGNAL/END

	if (currentDisplayList->signal == PSP_GE_SIGNAL_SYNC)
		currentDisplayList->pc += 8;

	currentDisplayList->interrupted = true;
	currentDisplayList->state = PSP_GE_DL_STATE_PAUSED;
	currentDisplayList->signal = PSP_GE_SIGNAL_HANDLER_SUSPEND;
	stall = 0;
	pc = 0;
	running = true;
	isbreak = true;

	return dlQueue.front();
}

void GPUCommon::ProcessDLQueue()
{
	time_update();
	double start = time_now_d();

	// I don't know if this is the correct place to zero this, but something
	// need to do it. See Sol Trigger title screen.
	gstate_c.offsetAddr = 0;

#if defined(USING_QT_UI)
	if(host->GpuStep())
	{
		host->SendGPUStart();
	}
#endif

	u32 op;
	//while(!dlQueue.empty() && !interruptRunning)
	while(running && !interruptRunning && pc && pc != stall && currentDisplayList)
	{
		DisplayList &list = *currentList();
		//DEBUG_LOG(G3D,"Okay, starting DL execution at %08x - stall = %08x", l.pc, l.stall);

		if(list.state == PSP_GE_DL_STATE_COMPLETED) {
			PopDLQueue();
			continue;
		}

		//if (list.status == PSP_GE_LIST_PAUSED)
		//	break;

		//cycleLastPC = pc;
		//list.status = PSP_GE_LIST_DRAWING;
		list.interrupted = false;

		//if (list.stalled())
		//if(pc == stall)
		//	break;

		if (!Memory::IsValidAddress(pc)) {
			ERROR_LOG(G3D, "DL invalid address PC = %08x", pc);
			break;
		}

		op = Memory::ReadUnchecked_U32(pc); //read from memory
		u32 cmd = op >> 24;

#if defined(USING_QT_UI)
		if(host->GpuStep())
		{
			host->SendGPUWait(cmd, list.pc, &gstate);
		}
#endif
		u32 diff = op ^ gstate.cmdmem[cmd];
		PreExecuteOp(op, diff);

		// TODO: Add a compiler flag to remove stuff like this at very-final build time.
		if (dumpThisFrame_) {
			char temp[256];
			GeDisassembleOp(list.pc, op, Memory::ReadUnchecked_U32(pc - 4), temp);
			NOTICE_LOG(G3D, "%s", temp);
		}
		gstate.cmdmem[cmd] = op;
		
		ExecuteOp(op, diff);
		
		pc += 4;

		if(__GeHasPendingInterrupt())
			break;
	}

	//UpdateCycles(pc);
	CheckDrawSync();

	time_update();
	gpuStats.msProcessingDisplayLists += time_now_d() - start;
}

inline void GPUCommon::UpdateCycles(u32 pc, u32 newPC)
{
	cyclesExecuted += (pc - cycleLastPC) / 4;
	cycleLastPC = newPC == 0 ? pc : newPC;
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
		// ???
		break;

	case GE_CMD_ORIGIN:
		gstate_c.offsetAddr = currentList()->pc;
		break;

	case GE_CMD_JUMP:
		{
			u32 target = gstate_c.getRelativeAddress(data);
			if (Memory::IsValidAddress(target)) {
				UpdateCycles(pc, target - 4);
				pc = target - 4; // pc will be increased after we return, counteract that
			} else {
				ERROR_LOG_REPORT(G3D, "JUMP to illegal address %08x - ignoring! data=%06x", target, data);
			}
		}
		break;

	case GE_CMD_CALL:
		{
			// Saint Seiya needs correct support for relative calls.
			u32 retval = pc + 4;
			u32 target = gstate_c.getRelativeAddress(data);
			if (currentList()->stackptr == ARRAY_SIZE(currentList()->stack)) {
				ERROR_LOG_REPORT(G3D, "CALL: Stack full!");
			} else if (!Memory::IsValidAddress(target)) {
				ERROR_LOG_REPORT(G3D, "CALL to illegal address %08x - ignoring! data=%06x", target, data);
			} else {
				currentList()->stack[currentList()->stackptr++] = retval;
				UpdateCycles(pc, target - 4);
				pc = target - 4;	// pc will be increased after we return, counteract that
			}
		}
		break;

	case GE_CMD_RET:
		{
			if (currentList()->stackptr == 0) {
				ERROR_LOG_REPORT(G3D, "RET: Stack empty!");
			} else {
				u32 target = (pc & 0xF0000000) | (currentList()->stack[--currentList()->stackptr] & 0x0FFFFFFF);
				//target = (target + gstate_c.originAddr) & 0xFFFFFFF;
				UpdateCycles(pc, target - 4);
				pc = target - 4;
				if (!Memory::IsValidAddress(currentList()->pc)) {
					ERROR_LOG_REPORT(G3D, "Invalid DL PC %08x on return", pc);
				}
			}
		}
		break;

	case GE_CMD_SIGNAL:
	case GE_CMD_FINISH:
		// Processed in GE_END.
		break;

	case GE_CMD_END:
		{
			UpdateCycles(pc);
			u32 prev = Memory::ReadUnchecked_U32(pc - 4);

			// Pause is SIGNAL/END/FINISH/END but the token is from the first SIGNAL
			if(currentDisplayList->signal != PSP_GE_SIGNAL_HANDLER_PAUSE || prev >> 24 == GE_CMD_SIGNAL)
				currentList()->subIntrToken = prev & 0xFFFF;
			switch (prev >> 24) {
			case GE_CMD_SIGNAL:
				{
					// TODO: see http://code.google.com/p/jpcsp/source/detail?r=2935#
					SignalBehavior behaviour = static_cast<SignalBehavior>((prev >> 16) & 0xFF);
					int signal = prev & 0xFFFF;
					int enddata = data & 0xFFFF;

					switch (behaviour) {
					case PSP_GE_SIGNAL_HANDLER_SUSPEND:  // Signal with Wait						
						if (currentList()->subIntrBase >= 0) {
							if (sceKernelGetCompiledSdkVersion() <= 0x02000010)
								currentList()->state = PSP_GE_DL_STATE_PAUSED;

							currentList()->signal = behaviour;
							__GeTriggerInterrupt(dlQueue.front(), pc);
						}
						break;
					case PSP_GE_SIGNAL_HANDLER_CONTINUE:
						if (currentList()->subIntrBase >= 0) {
							currentList()->signal = behaviour;
							__GeTriggerInterrupt(dlQueue.front(), pc);
						}
						break;
					case PSP_GE_SIGNAL_HANDLER_PAUSE:
						currentDisplayList->state = PSP_GE_DL_STATE_PAUSED;
						currentList()->signal = behaviour;
						break;
					case PSP_GE_SIGNAL_SYNC:
						ERROR_LOG(G3D, "Signal with Sync UNIMPLEMENTED! signal/end: %04x %04x", signal, enddata);
						break;
					case PSP_GE_SIGNAL_JUMP:
						ERROR_LOG(G3D, "Signal with Jump UNIMPLEMENTED! signal/end: %04x %04x", signal, enddata);
						break;
					case PSP_GE_SIGNAL_CALL:
						ERROR_LOG(G3D, "Signal with Call UNIMPLEMENTED! signal/end: %04x %04x", signal, enddata);
						break;
					case PSP_GE_SIGNAL_RET:
						ERROR_LOG(G3D, "Signal with Return UNIMPLEMENTED! signal/end: %04x %04x", signal, enddata);
						break;
					default:
						ERROR_LOG(G3D, "UNKNOWN Signal UNIMPLEMENTED %i ! signal/end: %04x %04x", behaviour, signal, enddata);
						pc = 0;
						break;
					}
				}
				break;
			case GE_CMD_FINISH:
				{
					DEBUG_LOG(G3D, "Finish %i ! signal/end: %04x %04x", dlQueue.front(), prev & 0xFFFF, data & 0xFFFF);
					if(currentDisplayList->signal == PSP_GE_SIGNAL_HANDLER_PAUSE) {
						// TODO Save BASE?
						running = false;
					}
					else {
						currentList()->state = PSP_GE_DL_STATE_COMPLETED;
						if(currentList()->threadWaiting) {
							currentList()->threadWaiting = false;
							__KernelTriggerWait(WAITTYPE_GELISTSYNC, dlQueue.front(), "GeListSync");
						}
					}

					__GeTriggerInterrupt(dlQueue.front(), pc);
				}
				break;
			default:
				DEBUG_LOG(G3D,"Ah, not finished: %06x", prev & 0xFFFFFF);
				break;
			}
		}
		break;
	default:
		DEBUG_LOG(G3D, "DL Unknown: %08x @ %08x", op, pc);
		break;
	}
}

void GPUCommon::DoState(PointerWrap &p) {
	p.Do(dls);
	p.Do(dlQueue);
	int currentID = 0;
	if (currentDisplayList != NULL) {
		ptrdiff_t off = currentDisplayList - &dls[0];
		currentID = off / sizeof(DisplayList);
	}
	p.Do(currentID);
	if (currentID == 0) {
		currentDisplayList = NULL;
	} else {
		currentDisplayList = &dls[currentID];
	}
	p.Do(interruptRunning);
	p.Do(isbreak);
	p.DoMarker("GPUCommon");
}

void GPUCommon::InterruptStart()
{
	interruptRunning = true;
}
void GPUCommon::InterruptEnd()
{
	interruptRunning = false;
	isbreak = false;
	ProcessDLQueue();
}
