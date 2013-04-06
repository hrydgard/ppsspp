#pragma once

#include "GPUInterface.h"

class GPUCommon : public GPUInterface
{
public:
	GPUCommon();
	virtual ~GPUCommon() {}

	virtual void InterruptStart(int listid);
	virtual void InterruptEnd(int listid);
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
	void PopDLQueue();
	void CheckDrawSync();

	typedef std::list<int> DisplayListQueue;

	DisplayList dls[DisplayListMaxCount];
	DisplayList *currentList;
	DisplayListQueue dlQueue;

	bool interruptRunning;
	u32 prev;
	GPUState gpuState;
	bool isbreak;
	bool drawComplete;

	u64 startingTicks;
	u32 cycleLastPC;
	int cyclesExecuted;

	bool dumpNextFrame_;
	bool dumpThisFrame_;
	bool interruptsEnabled_;

public:
	virtual DisplayList* getList(int listid)
	{
		return &dls[listid];
	}

	const std::list<int>& GetDisplayLists()
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
