#pragma once

#include "GPUInterface.h"

class GPUCommon : public GPUInterface
{
public:
	GPUCommon() :
		dlIdGenerator(1),
		currentList(NULL)
	{}
	
	virtual bool ProcessDLQueue() = 0;
	virtual void UpdateStall(int listid, u32 newstall);
	virtual u32  EnqueueList(u32 listpc, u32 stall, bool head);
	virtual int  listStatus(int listid);

protected:
	typedef std::deque<DisplayList> DisplayListQueue;
	
	int dlIdGenerator;
	DisplayList *currentList;
	DisplayListQueue dlQueue;
};