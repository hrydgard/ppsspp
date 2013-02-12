#pragma once

#include "GPUInterface.h"

class GPUCommon : public GPUInterface
{
public:
	GPUCommon() :
		dlIdGenerator(1),
		currentList(NULL),
		stackptr(0),
		dumpNextFrame_(false),
		dumpThisFrame_(false)
	{}

	virtual void InterruptStart();
	virtual void InterruptEnd();

	virtual void PreExecuteOp(u32 op, u32 diff);
	virtual bool InterpretList(DisplayList &list);
	virtual bool ProcessDLQueue();
	virtual void UpdateStall(int listid, u32 newstall);
	virtual u32  EnqueueList(u32 listpc, u32 stall, int subIntrBase, bool head);
	virtual int  listStatus(int listid);
	virtual void DoState(PointerWrap &p);

protected:
	typedef std::deque<DisplayList> DisplayListQueue;

	int dlIdGenerator;
	DisplayList *currentList;
	DisplayListQueue dlQueue;

	bool interruptRunning;
	u32 prev;
	u32 stack[2];
	u32 stackptr;
	bool finished;

	bool dumpNextFrame_;
	bool dumpThisFrame_;


public:
	virtual DisplayList* getList(int listid)
	{
		if (currentList && currentList->id == listid)
			return currentList;
		for(auto it = dlQueue.begin(); it != dlQueue.end(); ++it)
		{
			if(it->id == listid)
				return &*it;
		}
		return NULL;
	}

	const std::deque<DisplayList>& GetDisplayLists()
	{
		return dlQueue;
	}
	DisplayList* GetCurrentDisplayList()
	{
		return currentList;
	}
};
