#include "base/timeutil.h"
#include "../Core/MemMap.h"
#include "GeDisasm.h"
#include "GPUCommon.h"
#include "GPUState.h"
#include "ChunkFile.h"


static int dlIdGenerator = 1;

void init() {
	dlIdGenerator = 1;
}

int GPUCommon::listStatus(int listid)
{
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
	dl.pc = listpc & 0xFFFFFFF;
	dl.stall = stall & 0xFFFFFFF;
	dl.status = PSP_GE_LIST_QUEUED;
	dl.subIntrBase = subIntrBase;
	if(head)
		dlQueue.push_front(dl);
    else
		dlQueue.push_back(dl);
	ProcessDLQueue();
	return dl.id;
}

void GPUCommon::UpdateStall(int listid, u32 newstall)
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
}

bool GPUCommon::InterpretList(DisplayList &list)
{
	time_update();
	double start = time_now_d();
	currentList = &list;
	// Reset stackptr for safety
	stackptr = 0;
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

void GPUCommon::DoState(PointerWrap &p) {
	p.Do(dlIdGenerator);
	p.Do<DisplayList>(dlQueue);
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
