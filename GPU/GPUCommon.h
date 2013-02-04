#pragma once

#include "GPUInterface.h"

class GPUCommon : public GPUInterface
{
public:
	GPUCommon();
	virtual ~GPUCommon() {}

	virtual void InterruptStart();
	virtual void InterruptEnd();

	virtual void PreExecuteOp(u32 op, u32 diff);
	virtual void ExecuteOp(u32 op, u32 diff);
	virtual void ProcessDLQueue();
	virtual u32  UpdateStall(int listid, u32 newstall);
	virtual u32  EnqueueList(u32 listpc, u32 stall, int subIntrBase, bool head);
	virtual u32  DequeueList(int listid);
	virtual int  ListSync(int listid, int mode);
	virtual u32  DrawSync(int mode);
	virtual void DoState(PointerWrap &p);
	virtual u32  Continue();
	virtual u32  Break(int mode);

protected:
	typedef std::list<int> DisplayListQueue;

	DisplayList dls[DisplayListMaxCount];
	DisplayListQueue dlQueue;

	bool interruptRunning;

	bool drawSyncWait;

	bool dumpNextFrame_;
	bool dumpThisFrame_;

	bool running;
	bool isbreak;
	u32 pc;
	u32 stall;
	DisplayList *currentDisplayList;

	void PopDLQueue();
	void CheckDrawSync();

public:
	virtual DisplayList* getList(int listid)
	{
		return &dls[listid];
	}

	DisplayList* currentList()
	{
		return currentDisplayList;
		/*if(dlQueue.empty())
			return NULL;
		return &dls[dlQueue.front()];*/
	}
};
