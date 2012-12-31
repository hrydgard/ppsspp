#pragma once

#include "GPUInterface.h"

class GPUCommon : public GPUInterface
{
public:
	GPUCommon() :
		interruptEnabled(true),
		dumpNextFrame_(false),
		dumpThisFrame_(false)
	{

		for(int i = 0; i < DisplayListMaxCount; ++i)
		{
			dls[i].queued = false;
		}
	}

	virtual void EnableInterrupts(bool enable)
	{
		interruptEnabled = enable;
	}

	virtual void PreExecuteOp(u32 op, u32 diff);
	virtual void ExecuteOp(u32 op, u32 diff);
	virtual void ProcessDLQueue();
	virtual u32  UpdateStall(int listid, u32 newstall);
	virtual u32  EnqueueList(u32 listpc, u32 stall, int subIntrBase, bool head);
	virtual u32  DequeueList(int listid);
	virtual int  ListStatus(int listid);
	virtual u32  DrawSync(int mode);
	virtual void DoState(PointerWrap &p);
	virtual u32  Continue();
	virtual u32  Break(int mode);

protected:
	typedef std::list<int> DisplayListQueue;

	DisplayList dls[DisplayListMaxCount];
	DisplayListQueue dlQueue;

	u32 prev;
	bool interruptRunning;
	bool interruptEnabled;

	bool dumpNextFrame_;
	bool dumpThisFrame_;

	DisplayList* currentList()
	{
		if(dlQueue.empty())
			return NULL;
		return &dls[dlQueue.front()];
	}
};