#pragma once

#include "GPUInterface.h"

class GPUCommon : public GPUInterface
{
public:
	GPUCommon() :
		dlIdGenerator(1),
		currentList(NULL),
		isbreak(false),
		dumpNextFrame_(false),
		dumpThisFrame_(false),
		interruptsEnabled_(true)
	{}

	virtual void InterruptStart();
	virtual void InterruptEnd();
	virtual void EnableInterrupts(bool enable) {
		interruptsEnabled_ = enable;
	}

	virtual void ExecuteOp(u32 op, u32 diff);
	virtual void PreExecuteOp(u32 op, u32 diff);
	virtual bool InterpretList(DisplayList &list);
	virtual bool ProcessDLQueue();
	virtual u32  UpdateStall(int listid, u32 newstall);
	virtual u32  EnqueueList(u32 listpc, u32 stall, int subIntrBase, bool head);
	virtual u32  DequeueList(int listid);
	virtual int  ListSync(int listid, int mode);
	virtual u32  DrawSync(int mode);
	virtual void DoState(PointerWrap &p);
	virtual bool FramebufferDirty() { return true; }
	virtual u32  Continue();
	virtual u32  Break(int mode);

protected:
	void UpdateCycles(u32 pc, u32 newPC = 0);

	typedef std::deque<DisplayList> DisplayListQueue;

	int dlIdGenerator;
	DisplayList *currentList;
	DisplayListQueue dlQueue;

	bool interruptRunning;
	u32 prev;
	bool finished;
	bool isbreak;

	u64 startingTicks;
	u32 cycleLastPC;
	int cyclesExecuted;

	bool dumpNextFrame_;
	bool dumpThisFrame_;
	bool interruptsEnabled_;

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
	virtual bool DecodeTexture(u8* dest, GPUgstate state)
	{
		return false;
	}
	std::vector<FramebufferInfo> GetFramebufferList()
	{
		return std::vector<FramebufferInfo>();
	}

};
