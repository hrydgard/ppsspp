#include "GPUCommon.h"

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

u32 GPUCommon::EnqueueList(u32 listpc, u32 stall, bool head)
{
	DisplayList dl;
	dl.id = dlIdGenerator++;
	dl.pc = listpc & 0xFFFFFFF;
	dl.stall = stall & 0xFFFFFFF;
	dl.status = PSP_GE_LIST_QUEUED;
	if(head)
		dlQueue.push_front(dl);
    else
		dlQueue.push_back(dl);
	ProcessDLQueue();
	return dl.id;
}

void GPUCommon::UpdateStall(int listid, u32 newstall)
{
	// this needs improvement....
	for (DisplayListQueue::iterator iter = dlQueue.begin(); iter != dlQueue.end(); iter++)
	{
		DisplayList &l = *iter;
		if (l.id == listid)
		{
			l.stall = newstall & 0xFFFFFFF;
		}
	}
	
	ProcessDLQueue();
}