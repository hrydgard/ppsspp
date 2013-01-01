#include <algorithm>
#include "../Core/MemMap.h"
#include "../Core/HLE/sceKernelThread.h"
#include "../Core/HLE/sceKernelInterrupt.h"
#include "GeDisasm.h"
#include "GPUCommon.h"
#include "GPUState.h"

void init() {
}

u32 GPUCommon::DrawSync(int mode) {
	if(mode < 0 || mode > 1)
		return 0x80000107;

	if(mode == 0) {
		// Sould be post wait
		// Clear the queue
		dlQueue.clear();
		for(int i = 0; i < DisplayListMaxCount; ++i)
			dls[i].queued = false;

		return 0;
	}

	if(dlQueue.empty())
		return 0;

	if(currentList()->stalled())
		return PSP_GE_LIST_STALLING;

	return PSP_GE_LIST_DRAWING;
}

int GPUCommon::ListStatus(int listid)
{
	if(listid < 0 || listid >= DisplayListMaxCount || !dls[listid].queued)
		return 0x80000100; // INVALID_ID

	int res = dls[listid].status;
	if(res == PSP_GE_LIST_QUEUED && dls[listid].interrupted == true)
		res = PSP_GE_LIST_PAUSED;

	if(res == PSP_GE_LIST_DRAWING && dls[listid].stalled())
		res = PSP_GE_LIST_STALLING;

	return res;
}	

u32 GPUCommon::EnqueueList(u32 listpc, u32 stall, int subIntrBase, bool head)
{
	DisplayList* current = currentList();
	if(head && current && current->status != PSP_GE_LIST_PAUSED)
	{
		// Can't enqueue head if the first list isn't paused
		return 0x800001FE;
	}

	int id = -1;
	for(int i = 0; i < DisplayListMaxCount; ++i)
	{
		if(dls[i].queued == false)
		{
			id = i;
			break;
		}
	}
	if(id < 0)
	{
		ERROR_LOG(G3D, "No DL ID available to enqueue");
		return -1; // TODO find error code
	}

	DisplayList &dl = dls[id];
	dl.pc = listpc & 0xFFFFFFF;
	dl.stall = stall & 0xFFFFFFF;
	dl.subIntrBase = subIntrBase;
	dl.stackptr = 0;
	dl.interrupted = false;
	dl.queued = true;

	if(head)
	{
		dl.status = PSP_GE_LIST_PAUSED;
		if(current)
			current->status = PSP_GE_LIST_QUEUED;
		dlQueue.push_front(id);
	}
    else
	{
		dl.status = PSP_GE_LIST_QUEUED;
		dlQueue.push_back(id);
	}

	ProcessDLQueue();

	return id;
}

u32 GPUCommon::DequeueList(int listid)
{
	if(listid < 0 || listid >= DisplayListMaxCount || !dls[listid].queued)
		return 0x80000100;
	
	if(dls[listid].status == PSP_GE_LIST_DRAWING || dls[listid].status == PSP_GE_LIST_PAUSED)
		return 0x80000021;

	dls[listid].queued = false;
	dlQueue.remove(listid);

	return 0;
}

u32 GPUCommon::UpdateStall(int listid, u32 newstall)
{
	if(listid < 0 || listid >= DisplayListMaxCount || !dls[listid].queued)
		return 0x80000100;

	dls[listid].stall = newstall & 0xFFFFFFF;
		
	ProcessDLQueue();

	return 0;
}

u32 GPUCommon::Continue()
{
	DisplayList* current = currentList();
	if(!current)
		return 0;

	if(current->status == PSP_GE_LIST_PAUSED)
	{
		current->status = PSP_GE_LIST_QUEUED;
	} else if(current->status == PSP_GE_LIST_DRAWING) {
		//TODO Different error code depending on the SDK
		//if(sdkVer >= 0x02000000)
        //	return 0x80000020;
        //else
		//	return -1;
		return 0x80000020;
    } else {
		//TODO Different error code depending on the SDK
        //if(sdkVer >= 0x02000000)
        //	return 0x80000004;
        //else
        //	return -1;
		return 0x80000004;
    }

	ProcessDLQueue();

	return 0;
}

u32 GPUCommon::Break(int mode)
{
	if (mode < 0 || mode >= 2)
        return 0x80000107;

	DisplayList* current = currentList();
	if(!current)
		return 0x80000020;

	if(mode == 1)
	{
		// Clear the queue
		dlQueue.clear();
		for(int i = 0; i < DisplayListMaxCount; ++i)
			dls[i].queued = false;

		return 0;
	}

	if(current->status == PSP_GE_LIST_DONE)
		return 0x80000020;

	if(current->status == PSP_GE_LIST_PAUSED)
	{
		//TODO Different error code depending on the SDK and signal handling
		//if(sdkVer > 0x02000010 && signal != SCE_GE_DL_SIGNAL_PAUSE) {
		//	return 0x80000020;
		//}
		return 0x80000021;
	}

	if(current->status == PSP_GE_LIST_DRAWING)
		current->interrupted = true;

	current->status = PSP_GE_LIST_PAUSED;

	return dlQueue.front();
}

void GPUCommon::ProcessDLQueue()
{
	DisplayListQueue::iterator iter = dlQueue.begin();
	
	u32 op;

	while(!dlQueue.empty())
	{
		DisplayList &list = *currentList();
		//DEBUG_LOG(G3D,"Okay, starting DL execution at %08x - stall = %08x", l.pc, l.stall);
		
		if (list.status == PSP_GE_LIST_PAUSED)
			break;
		
		list.status = PSP_GE_LIST_DRAWING;
		list.interrupted = false;
		
		if (list.stalled())
			break;
		
		if (!Memory::IsValidAddress(list.pc)) {
			ERROR_LOG(G3D, "DL invalid address PC = %08x", list.pc);
			break;
		}

		op = Memory::ReadUnchecked_U32(list.pc); //read from memory
		u32 cmd = op >> 24;
		u32 diff = op ^ gstate.cmdmem[cmd];
		PreExecuteOp(op, diff);

		// TODO: Add a compiler flag to remove stuff like this at very-final build time.
		if (dumpThisFrame_) {
			char temp[256];
			GeDisassembleOp(list.pc, op, prev, temp);
			NOTICE_LOG(G3D, "%s", temp);
		}

		gstate.cmdmem[cmd] = op;	 // crashes if I try to put the whole op there??
		
		ExecuteOp(op, diff);
		
		list.pc += 4;
		prev = op;

		if(list.status == PSP_GE_LIST_DONE)
		{			
			dlQueue.erase(iter);
			// TODO: Should this run while interrupts are suspended?
			if (interruptEnabled)
				__TriggerInterruptWithArg(PSP_INTR_HLE, PSP_GE_INTR, list.subIntrBase | PSP_GE_SUBINTR_FINISH, 0);
		}
	}
}

void GPUCommon::PreExecuteOp(u32 op, u32 diff) {
	// Nothing to do
}

void GPUCommon::ExecuteOp(u32 op, u32 diff) {
	u32 cmd = op >> 24;
	u32 data = op & 0xFFFFFF;

	// Handle control and drawing commands here directly. The others we delegate.
	switch (cmd) {
	case GE_CMD_JUMP:
		{
			u32 target = (((gstate.base & 0x00FF0000) << 8) | (op & 0xFFFFFC)) & 0x0FFFFFFF;
			if (Memory::IsValidAddress(target)) {
				currentList()->pc = target - 4; // pc will be increased after we return, counteract that
			} else {
				ERROR_LOG(G3D, "JUMP to illegal address %08x - ignoring??", target);
			}
		}
		break;

	case GE_CMD_CALL:
		{
			u32 retval = currentList()->pc + 4;
			if (currentList()->stackptr == ARRAY_SIZE(currentList()->stack)) {
				ERROR_LOG(G3D, "CALL: Stack full!");
			} else {
				currentList()->stack[currentList()->stackptr++] = retval;
				u32 target = (((gstate.base & 0x00FF0000) << 8) | (op & 0xFFFFFC)) & 0xFFFFFFF;
				currentList()->pc = target - 4;	// pc will be increased after we return, counteract that
			}
		}
		break;

	case GE_CMD_RET:
		{
			u32 target = (currentList()->pc & 0xF0000000) | (currentList()->stack[--currentList()->stackptr] & 0x0FFFFFFF);
			currentList()->pc = target - 4;
		}
		break;

	case GE_CMD_SIGNAL:
		{
			// Processed in GE_END. Has data.
		}
		break;

	case GE_CMD_FINISH:
		// TODO: Should this run while interrupts are suspended?
		break;

	case GE_CMD_END:
		switch (prev >> 24) {
		case GE_CMD_SIGNAL:
			{
				// TODO: see http://code.google.com/p/jpcsp/source/detail?r=2935#
				int behaviour = (prev >> 16) & 0xFF;
				int signal = prev & 0xFFFF;
				int enddata = data & 0xFFFF;
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
				if (interruptEnabled)
					__TriggerInterruptWithArg(PSP_INTR_HLE, PSP_GE_INTR, currentList()->subIntrBase | PSP_GE_SUBINTR_SIGNAL, signal);
			}
			break;
		case GE_CMD_FINISH:
			currentList()->status = PSP_GE_LIST_DONE;
			break;
		default:
			DEBUG_LOG(G3D,"Ah, not finished: %06x", prev & 0xFFFFFF);
			break;
		}
		break;
	default:
		ERROR_LOG(G3D, "DL Unknown: %08x @ %08x", op, currentList() == NULL ? 0 : currentList()->pc);
	}
}

void GPUCommon::DoState(PointerWrap &p) {
	p.Do(dls);
	p.Do(dlQueue);
	p.DoMarker("GPUCommon");
}
