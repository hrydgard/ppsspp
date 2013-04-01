#include "base/timeutil.h"
#include "GeDisasm.h"
#include "GPUCommon.h"
#include "GPUState.h"
#include "ChunkFile.h"
#include "Core/MemMap.h"
#include "Core/Host.h"
#include "Core/HLE/sceKernelThread.h"
#include "Core/HLE/sceKernelInterrupt.h"
#include "Core/HLE/sceGe.h"

static int dlIdGenerator = 1;

void init() {
	dlIdGenerator = 1;
}

u32 GPUCommon::DrawSync(int mode) {
	if (mode < 0 || mode > 1)
		return SCE_KERNEL_ERROR_INVALID_MODE;

	return 0;
}

int GPUCommon::ListSync(int listid, int mode)
{
	if (mode < 0 || mode > 1)
		return SCE_KERNEL_ERROR_INVALID_MODE;

	// TODO
	if (mode == 0)
		return 0;

	return 0;
	for(DisplayListQueue::iterator it(dlQueue.begin()); it != dlQueue.end(); ++it)
	{
		if(it->id == listid)
		{
			return it->status;
		}
	}
	return 0x80000100; // INVALID_ID
}

u32 GPUCommon::EnqueueList(u32 listpc, u32 stall, int subIntrBase, bool head)
{
	DisplayList dl;
	dl.id = dlIdGenerator++;
	dl.startpc = listpc & 0xFFFFFFF;
	dl.pc = listpc & 0xFFFFFFF;
	dl.stall = stall & 0xFFFFFFF;
	dl.status = PSP_GE_LIST_QUEUED;
	dl.subIntrBase = subIntrBase;
	dl.stackptr = 0;
	if(head)
		dlQueue.push_front(dl);
    else
		dlQueue.push_back(dl);
	ProcessDLQueue();
	return dl.id;
}

u32 GPUCommon::DequeueList(int listid)
{
	// TODO
	return 0;
}

u32 GPUCommon::UpdateStall(int listid, u32 newstall)
{
	for (auto iter = dlQueue.begin(); iter != dlQueue.end(); ++iter)
	{
		DisplayList &cur = *iter;
		if (cur.id == listid)
		{
			cur.stall = newstall & 0xFFFFFFF;
		}
	}
	
	ProcessDLQueue();

	return 0;
}

u32 GPUCommon::Continue()
{
	// TODO
	return 0;
}

u32 GPUCommon::Break(int mode)
{
	if (mode < 0 || mode > 1)
		return SCE_KERNEL_ERROR_INVALID_MODE;

	// TODO
	return 0;
}

bool GPUCommon::InterpretList(DisplayList &list)
{
	time_update();
	double start = time_now_d();
	currentList = &list;
	u32 op = 0;
	prev = 0;
	finished = false;

	// I don't know if this is the correct place to zero this, but something
	// need to do it. See Sol Trigger title screen.
	gstate_c.offsetAddr = 0;

	if (!Memory::IsValidAddress(list.pc)) {
		ERROR_LOG(G3D, "DL PC = %08x WTF!!!!", list.pc);
		return true;
	}
#if defined(USING_QT_UI)
		if(host->GpuStep())
		{
			host->SendGPUStart();
		}
#endif

	while (!finished)
	{
		list.status = PSP_GE_LIST_DRAWING;
		if (list.pc == list.stall)
		{
			list.status = PSP_GE_LIST_STALL_REACHED;
			return false;
		}

		op = Memory::ReadUnchecked_U32(list.pc); //read from memory
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
			GeDisassembleOp(list.pc, op, prev, temp);
			NOTICE_LOG(HLE, "%s", temp);
		}
		gstate.cmdmem[cmd] = op;	 // crashes if I try to put the whole op there??
		
		ExecuteOp(op, diff);
		
		list.pc += 4;
		prev = op;
	}
	time_update();
	gpuStats.msProcessingDisplayLists += time_now_d() - start;
	return true;
}

bool GPUCommon::ProcessDLQueue()
{
	DisplayListQueue::iterator iter = dlQueue.begin();
	while (iter != dlQueue.end())
	{
		DisplayList &l = *iter;
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
		// ???
		break;

	case GE_CMD_ORIGIN:
		gstate_c.offsetAddr = currentList->pc;
		break;

	case GE_CMD_JUMP:
		{
			u32 target = gstate_c.getRelativeAddress(data);
			if (Memory::IsValidAddress(target)) {
				currentList->pc = target - 4; // pc will be increased after we return, counteract that
			} else {
				ERROR_LOG(G3D, "JUMP to illegal address %08x - ignoring! data=%06x", target, data);
			}
		}
		break;

	case GE_CMD_CALL:
		{
			// Saint Seiya needs correct support for relative calls.
			u32 retval = currentList->pc + 4;
			u32 target = gstate_c.getRelativeAddress(data);
			if (currentList->stackptr == ARRAY_SIZE(currentList->stack)) {
				ERROR_LOG(G3D, "CALL: Stack full!");
			} else if (!Memory::IsValidAddress(target)) {
				ERROR_LOG(G3D, "CALL to illegal address %08x - ignoring! data=%06x", target, data);
			} else {
				currentList->stack[currentList->stackptr++] = retval;
				currentList->pc = target - 4;	// pc will be increased after we return, counteract that
			}
		}
		break;

	case GE_CMD_RET:
		{
			if (currentList->stackptr == 0) {
				ERROR_LOG(G3D, "RET: Stack empty!");
			} else {
				u32 target = (currentList->pc & 0xF0000000) | (currentList->stack[--currentList->stackptr] & 0x0FFFFFFF);
				//target = (target + gstate_c.originAddr) & 0xFFFFFFF;
				currentList->pc = target - 4;
				if (!Memory::IsValidAddress(currentList->pc)) {
					ERROR_LOG(G3D, "Invalid DL PC %08x on return", currentList->pc);
					finished = true;
				}
			}
		}
		break;

	case GE_CMD_SIGNAL:
		{
			// Processed in GE_END. Has data.
			currentList->subIntrToken = data & 0xFFFF;
		}
		break;

	case GE_CMD_FINISH:
		currentList->subIntrToken = data & 0xFFFF;
		// TODO: Should this run while interrupts are suspended?
		if (interruptsEnabled_)
			__GeTriggerInterrupt(currentList->id, currentList->pc, currentList->subIntrBase, currentList->subIntrToken);
		break;

	case GE_CMD_END:
		switch (prev >> 24) {
		case GE_CMD_SIGNAL:
			{
				currentList->status = PSP_GE_LIST_END_REACHED;
				// TODO: see http://code.google.com/p/jpcsp/source/detail?r=2935#
				int behaviour = (prev >> 16) & 0xFF;
				int signal = prev & 0xFFFF;
				int enddata = data & 0xFFFF;
				// We should probably defer to sceGe here, no sense in implementing this stuff in every GPU
				switch (behaviour) {
				case 1:  // Signal with Wait
					ERROR_LOG(G3D, "Signal with Wait UNIMPLEMENTED! signal/end: %04x %04x", signal, enddata);
					break;
				case 2:
					ERROR_LOG(G3D, "Signal without wait. signal/end: %04x %04x", signal, enddata);
					break;
				case 3:
					ERROR_LOG(G3D, "Signal with Pause UNIMPLEMENTED! signal/end: %04x %04x", signal, enddata);
					break;
				case 0x10:
					ERROR_LOG(G3D, "Signal with Jump UNIMPLEMENTED! signal/end: %04x %04x", signal, enddata);
					break;
				case 0x11:
					ERROR_LOG(G3D, "Signal with Call UNIMPLEMENTED! signal/end: %04x %04x", signal, enddata);
					break;
				case 0x12:
					ERROR_LOG(G3D, "Signal with Return UNIMPLEMENTED! signal/end: %04x %04x", signal, enddata);
					break;
				default:
					ERROR_LOG(G3D, "UNKNOWN Signal UNIMPLEMENTED %i ! signal/end: %04x %04x", behaviour, signal, enddata);
					break;
				}
				// TODO: Should this run while interrupts are suspended?
				if (interruptsEnabled_)
					__GeTriggerInterrupt(currentList->id, currentList->pc, currentList->subIntrBase, currentList->subIntrToken);
			}
			break;
		case GE_CMD_FINISH:
			currentList->status = PSP_GE_LIST_DONE;
			finished = true;
			break;
		default:
			DEBUG_LOG(G3D,"Ah, not finished: %06x", prev & 0xFFFFFF);
			break;
		}
		break;

	default:
		DEBUG_LOG(G3D,"DL Unknown: %08x @ %08x", op, currentList == NULL ? 0 : currentList->pc);
		break;
	}
}

void GPUCommon::DoState(PointerWrap &p) {
	p.Do(dlIdGenerator);
	p.Do<DisplayList>(dlQueue);
	int currentID = currentList == NULL ? 0 : currentList->id;
	p.Do(currentID);
	if (currentID == 0) {
		currentList = 0;
	} else {
		for (auto it = dlQueue.begin(), end = dlQueue.end(); it != end; ++it) {
			if (it->id == currentID) {
				currentList = &*it;
				break;
			}
		}
	}
	p.Do(interruptRunning);
	p.Do(prev);
	p.Do(finished);
	p.DoMarker("GPUCommon");
}

void GPUCommon::InterruptStart()
{
	interruptRunning = true;
}
void GPUCommon::InterruptEnd()
{
	interruptRunning = false;
	ProcessDLQueue();
}
