#include "ppsspp_config.h"

#if defined(_M_SSE)
#include <emmintrin.h>
#endif
#if PPSSPP_ARCH(ARM_NEON)
#if defined(_MSC_VER) && PPSSPP_ARCH(ARM64)
#include <arm64_neon.h>
#else
#include <arm_neon.h>
#endif
#endif

#include <algorithm>

#include "Common/Profiler/Profiler.h"

#include "Common/GraphicsContext.h"
#include "Common/LogReporting.h"
#include "Common/Serialize/Serializer.h"
#include "Common/Serialize/SerializeFuncs.h"
#include "Common/Serialize/SerializeList.h"
#include "Common/TimeUtil.h"
#include "GPU/GeDisasm.h"
#include "GPU/GPU.h"
#include "GPU/GPUCommon.h"
#include "GPU/GPUState.h"
#include "Core/Config.h"
#include "Core/CoreTiming.h"
#include "Core/Debugger/MemBlockInfo.h"
#include "Core/MemMap.h"
#include "Core/Reporting.h"
#include "Core/HLE/HLE.h"
#include "Core/HLE/sceKernelMemory.h"
#include "Core/HLE/sceKernelInterrupt.h"
#include "Core/HLE/sceKernelThread.h"
#include "Core/HLE/sceGe.h"
#include "Core/HW/Display.h"
#include "Core/Util/PPGeDraw.h"
#include "Core/MemMapHelpers.h"
#include "GPU/Common/DrawEngineCommon.h"
#include "GPU/Common/FramebufferManagerCommon.h"
#include "GPU/Common/TextureCacheCommon.h"
#include "GPU/Debugger/Debugger.h"
#include "GPU/Debugger/Record.h"

void GPUCommon::Flush() {
	drawEngineCommon_->DispatchFlush();
}

void GPUCommon::DispatchFlush() {
	drawEngineCommon_->DispatchFlush();
}

GPUCommon::GPUCommon(GraphicsContext *gfxCtx, Draw::DrawContext *draw) :
	gfxCtx_(gfxCtx),
	draw_(draw)
{
	// This assert failed on GCC x86 32-bit (but not MSVC 32-bit!) before adding the
	// "padding" field at the end. This is important for save state compatibility.
	// The compiler was not rounding the struct size up to an 8 byte boundary, which
	// you'd expect due to the int64 field, but the Linux ABI apparently does not require that.
	static_assert(sizeof(DisplayList) == 456, "Bad DisplayList size");

	Reinitialize();
	gstate.Reset();
	gstate_c.Reset();
	gpuStats.Reset();

	PPGeSetDrawContext(draw);
	ResetMatrices();
}

void GPUCommon::BeginHostFrame() {
	ReapplyGfxState();

	// TODO: Assume config may have changed - maybe move to resize.
	gstate_c.Dirty(DIRTY_ALL);

	UpdateCmdInfo();

	UpdateMSAALevel(draw_);
	CheckConfigChanged();
	CheckDisplayResized();
	CheckRenderResized();
}

void GPUCommon::EndHostFrame() {
	// Probably not necessary.
	if (draw_) {
		draw_->Invalidate(InvalidationFlags::CACHED_RENDER_STATE);
	}
}

void GPUCommon::Reinitialize() {
	memset(dls, 0, sizeof(dls));
	for (int i = 0; i < DisplayListMaxCount; ++i) {
		dls[i].state = PSP_GE_DL_STATE_NONE;
		dls[i].waitTicks = 0;
	}

	nextListID = 0;
	currentList = nullptr;
	isbreak = false;
	drawCompleteTicks = 0;
	busyTicks = 0;
	timeSpentStepping_ = 0.0;
	interruptsEnabled_ = true;

	if (textureCache_)
		textureCache_->Clear(true);
	if (framebufferManager_)
		framebufferManager_->DestroyAllFBOs();
}

int GPUCommon::EstimatePerVertexCost() {
	// TODO: This is transform cost, also account for rasterization cost somehow... although it probably
	// runs in parallel with transform.

	// Also, this is all pure guesswork. If we can find a way to do measurements, that would be great.

	// GTA wants a low value to run smooth, GoW wants a high value (otherwise it thinks things
	// went too fast and starts doing all the work over again).

	int cost = 20;
	if (gstate.isLightingEnabled()) {
		cost += 10;

		for (int i = 0; i < 4; i++) {
			if (gstate.isLightChanEnabled(i))
				cost += 7;
		}
	}

	if (gstate.getUVGenMode() != GE_TEXMAP_TEXTURE_COORDS) {
		cost += 20;
	}
	int morphCount = gstate.getNumMorphWeights();
	if (morphCount > 1) {
		cost += 5 * morphCount;
	}
	return cost;
}

void GPUCommon::PopDLQueue() {
	if(!dlQueue.empty()) {
		dlQueue.pop_front();
		if(!dlQueue.empty()) {
			bool running = currentList->state == PSP_GE_DL_STATE_RUNNING;
			currentList = &dls[dlQueue.front()];
			if (running)
				currentList->state = PSP_GE_DL_STATE_RUNNING;
		} else {
			currentList = nullptr;
		}
	}
}

bool GPUCommon::BusyDrawing() {
	u32 state = DrawSync(1);
	if (state == PSP_GE_LIST_DRAWING || state == PSP_GE_LIST_STALLING) {
		if (currentList && currentList->state != PSP_GE_DL_STATE_PAUSED) {
			return true;
		}
	}
	return false;
}

void GPUCommon::NotifyConfigChanged() {
	configChanged_ = true;
}

void GPUCommon::NotifyRenderResized() {
	renderResized_ = true;
}

void GPUCommon::NotifyDisplayResized() {
	displayResized_ = true;
}

void GPUCommon::DumpNextFrame() {
	dumpNextFrame_ = true;
}

u32 GPUCommon::DrawSync(int mode) {
	gpuStats.numDrawSyncs++;

	if (mode < 0 || mode > 1)
		return SCE_KERNEL_ERROR_INVALID_MODE;

	if (mode == 0) {
		if (!__KernelIsDispatchEnabled()) {
			return SCE_KERNEL_ERROR_CAN_NOT_WAIT;
		}
		if (__IsInInterrupt()) {
			return SCE_KERNEL_ERROR_ILLEGAL_CONTEXT;
		}

		if (drawCompleteTicks > CoreTiming::GetTicks()) {
			__GeWaitCurrentThread(GPU_SYNC_DRAW, 1, "GeDrawSync");
		} else {
			for (int i = 0; i < DisplayListMaxCount; ++i) {
				if (dls[i].state == PSP_GE_DL_STATE_COMPLETED) {
					dls[i].state = PSP_GE_DL_STATE_NONE;
				}
			}
		}
		return 0;
	}

	// If there's no current list, it must be complete.
	DisplayList *top = NULL;
	for (int i : dlQueue) {
		if (dls[i].state != PSP_GE_DL_STATE_COMPLETED) {
			top = &dls[i];
			break;
		}
	}
	if (!top || top->state == PSP_GE_DL_STATE_COMPLETED)
		return PSP_GE_LIST_COMPLETED;

	if (currentList->pc == currentList->stall)
		return PSP_GE_LIST_STALLING;

	return PSP_GE_LIST_DRAWING;
}

void GPUCommon::CheckDrawSync() {
	if (dlQueue.empty()) {
		for (int i = 0; i < DisplayListMaxCount; ++i)
			dls[i].state = PSP_GE_DL_STATE_NONE;
	}
}

int GPUCommon::ListSync(int listid, int mode) {
	gpuStats.numListSyncs++;

	if (listid < 0 || listid >= DisplayListMaxCount)
		return SCE_KERNEL_ERROR_INVALID_ID;

	if (mode < 0 || mode > 1)
		return SCE_KERNEL_ERROR_INVALID_MODE;

	DisplayList& dl = dls[listid];
	if (mode == 1) {
		switch (dl.state) {
		case PSP_GE_DL_STATE_QUEUED:
			if (dl.interrupted)
				return PSP_GE_LIST_PAUSED;
			return PSP_GE_LIST_QUEUED;

		case PSP_GE_DL_STATE_RUNNING:
			if (dl.pc == dl.stall)
				return PSP_GE_LIST_STALLING;
			return PSP_GE_LIST_DRAWING;

		case PSP_GE_DL_STATE_COMPLETED:
			return PSP_GE_LIST_COMPLETED;

		case PSP_GE_DL_STATE_PAUSED:
			return PSP_GE_LIST_PAUSED;

		default:
			return SCE_KERNEL_ERROR_INVALID_ID;
		}
	}

	if (!__KernelIsDispatchEnabled()) {
		return SCE_KERNEL_ERROR_CAN_NOT_WAIT;
	}
	if (__IsInInterrupt()) {
		return SCE_KERNEL_ERROR_ILLEGAL_CONTEXT;
	}

	if (dl.waitTicks > CoreTiming::GetTicks()) {
		__GeWaitCurrentThread(GPU_SYNC_LIST, listid, "GeListSync");
	}
	return PSP_GE_LIST_COMPLETED;
}

int GPUCommon::GetStack(int index, u32 stackPtr) {
	if (!currentList) {
		// Seems like it doesn't return an error code?
		return 0;
	}

	if (currentList->stackptr <= index) {
		return SCE_KERNEL_ERROR_INVALID_INDEX;
	}

	if (index >= 0) {
		auto stack = PSPPointer<u32_le>::Create(stackPtr);
		if (stack.IsValid()) {
			auto entry = currentList->stack[index];
			// Not really sure what most of these values are.
			stack[0] = 0;
			stack[1] = entry.pc + 4;
			stack[2] = entry.offsetAddr;
			stack[7] = entry.baseAddr;
		}
	}

	return currentList->stackptr;
}

static void CopyMatrix24(u32_le *result, const float *mtx, u32 count, u32 cmdbits) {
	// Screams out for simple SIMD, but probably not called often enough to be worth it.
	for (u32 i = 0; i < count; ++i) {
		result[i] = toFloat24(mtx[i]) | cmdbits;
	}
}

bool GPUCommon::GetMatrix24(GEMatrixType type, u32_le *result, u32 cmdbits) {
	switch (type) {
	case GE_MTX_BONE0:
	case GE_MTX_BONE1:
	case GE_MTX_BONE2:
	case GE_MTX_BONE3:
	case GE_MTX_BONE4:
	case GE_MTX_BONE5:
	case GE_MTX_BONE6:
	case GE_MTX_BONE7:
		CopyMatrix24(result, gstate.boneMatrix + (type - GE_MTX_BONE0) * 12, 12, cmdbits);
		break;
	case GE_MTX_TEXGEN:
		CopyMatrix24(result, gstate.tgenMatrix, 12, cmdbits);
		break;
	case GE_MTX_WORLD:
		CopyMatrix24(result, gstate.worldMatrix, 12, cmdbits);
		break;
	case GE_MTX_VIEW:
		CopyMatrix24(result, gstate.viewMatrix, 12, cmdbits);
		break;
	case GE_MTX_PROJECTION:
		CopyMatrix24(result, gstate.projMatrix, 16, cmdbits);
		break;
	default:
		return false;
	}
	return true;
}

void GPUCommon::ResetMatrices() {
	// This means we restored a context, so update the visible matrix data.
	for (size_t i = 0; i < ARRAY_SIZE(gstate.boneMatrix); ++i)
		matrixVisible.bone[i] = toFloat24(gstate.boneMatrix[i]);
	for (size_t i = 0; i < ARRAY_SIZE(gstate.worldMatrix); ++i)
		matrixVisible.world[i] = toFloat24(gstate.worldMatrix[i]);
	for (size_t i = 0; i < ARRAY_SIZE(gstate.viewMatrix); ++i)
		matrixVisible.view[i] = toFloat24(gstate.viewMatrix[i]);
	for (size_t i = 0; i < ARRAY_SIZE(gstate.projMatrix); ++i)
		matrixVisible.proj[i] = toFloat24(gstate.projMatrix[i]);
	for (size_t i = 0; i < ARRAY_SIZE(gstate.tgenMatrix); ++i)
		matrixVisible.tgen[i] = toFloat24(gstate.tgenMatrix[i]);

	// Assume all the matrices changed, so dirty things related to them.
	gstate_c.Dirty(DIRTY_WORLDMATRIX | DIRTY_VIEWMATRIX | DIRTY_PROJMATRIX | DIRTY_TEXMATRIX | DIRTY_FRAGMENTSHADER_STATE | DIRTY_BONE_UNIFORMS);
}

u32 GPUCommon::EnqueueList(u32 listpc, u32 stall, int subIntrBase, PSPPointer<PspGeListArgs> args, bool head) {
	// TODO Check the stack values in missing arg and ajust the stack depth

	// Check alignment
	// TODO Check the context and stack alignement too
	if (((listpc | stall) & 3) != 0 || !Memory::IsValidAddress(listpc)) {
		ERROR_LOG_REPORT(Log::G3D, "sceGeListEnqueue: invalid address %08x", listpc);
		return SCE_KERNEL_ERROR_INVALID_POINTER;
	}

	// If args->size is below 16, it's the old struct without stack info.
	if (args.IsValid() && args->size >= 16 && args->numStacks >= 256) {
		return hleLogError(Log::G3D, SCE_KERNEL_ERROR_INVALID_SIZE, "invalid stack depth %d", args->numStacks);
	}

	int id = -1;
	u64 currentTicks = CoreTiming::GetTicks();
	u32 stackAddr = args.IsValid() && args->size >= 16 ? (u32)args->stackAddr : 0;
	// Check compatibility
	if (sceKernelGetCompiledSdkVersion() > 0x01FFFFFF) {
		//numStacks = 0;
		//stack = NULL;
		for (int i = 0; i < DisplayListMaxCount; ++i) {
			if (dls[i].state != PSP_GE_DL_STATE_NONE && dls[i].state != PSP_GE_DL_STATE_COMPLETED) {
				// Logically, if the CPU has not interrupted yet, it hasn't seen the latest pc either.
				// Exit enqueues right after an END, which fails without ignoring pendingInterrupt lists.
				if (dls[i].pc == listpc && !dls[i].pendingInterrupt) {
					ERROR_LOG(Log::G3D, "sceGeListEnqueue: can't enqueue, list address %08X already used", listpc);
					return 0x80000021;
				} else if (stackAddr != 0 && dls[i].stackAddr == stackAddr && !dls[i].pendingInterrupt) {
					ERROR_LOG(Log::G3D, "sceGeListEnqueue: can't enqueue, stack address %08X already used", stackAddr);
					return 0x80000021;
				}
			}
		}
	}
	// TODO Check if list stack dls[i].stack already used then return 0x80000021 as above

	for (int i = 0; i < DisplayListMaxCount; ++i) {
		int possibleID = (i + nextListID) % DisplayListMaxCount;
		auto possibleList = dls[possibleID];
		if (possibleList.pendingInterrupt) {
			continue;
		}

		if (possibleList.state == PSP_GE_DL_STATE_NONE) {
			id = possibleID;
			break;
		}
		if (possibleList.state == PSP_GE_DL_STATE_COMPLETED && possibleList.waitTicks < currentTicks) {
			id = possibleID;
		}
	}
	if (id < 0) {
		ERROR_LOG_REPORT(Log::G3D, "No DL ID available to enqueue");
		for (int i : dlQueue) {
			DisplayList &dl = dls[i];
			DEBUG_LOG(Log::G3D, "DisplayList %d status %d pc %08x stall %08x", i, dl.state, dl.pc, dl.stall);
		}
		return SCE_KERNEL_ERROR_OUT_OF_MEMORY;
	}
	nextListID = id + 1;

	DisplayList &dl = dls[id];
	dl.id = id;
	dl.startpc = listpc & 0x0FFFFFFF;
	dl.pc = listpc & 0x0FFFFFFF;
	dl.stall = stall & 0x0FFFFFFF;
	dl.subIntrBase = std::max(subIntrBase, -1);
	dl.stackptr = 0;
	dl.signal = PSP_GE_SIGNAL_NONE;
	dl.interrupted = false;
	dl.waitTicks = (u64)-1;
	dl.interruptsEnabled = interruptsEnabled_;
	dl.started = false;
	dl.offsetAddr = 0;
	dl.bboxResult = false;
	dl.stackAddr = stackAddr;

	if (args.IsValid() && args->context.IsValid())
		dl.context = args->context;
	else
		dl.context = 0;

	if (head) {
		if (currentList) {
			if (currentList->state != PSP_GE_DL_STATE_PAUSED)
				return SCE_KERNEL_ERROR_INVALID_VALUE;
			currentList->state = PSP_GE_DL_STATE_QUEUED;
			// Make sure we clear the signal so we don't try to pause it again.
			currentList->signal = PSP_GE_SIGNAL_NONE;
		}

		dl.state = PSP_GE_DL_STATE_PAUSED;

		currentList = &dl;
		dlQueue.push_front(id);
	} else if (currentList) {
		dl.state = PSP_GE_DL_STATE_QUEUED;
		dlQueue.push_back(id);
	} else {
		dl.state = PSP_GE_DL_STATE_RUNNING;
		currentList = &dl;
		dlQueue.push_front(id);

		drawCompleteTicks = (u64)-1;

		// TODO save context when starting the list if param is set
		ProcessDLQueue();
	}

	return id;
}

u32 GPUCommon::DequeueList(int listid) {
	if (listid < 0 || listid >= DisplayListMaxCount || dls[listid].state == PSP_GE_DL_STATE_NONE)
		return SCE_KERNEL_ERROR_INVALID_ID;

	auto &dl = dls[listid];
	if (dl.started)
		return SCE_KERNEL_ERROR_BUSY;

	dl.state = PSP_GE_DL_STATE_NONE;

	if (listid == dlQueue.front())
		PopDLQueue();
	else
		dlQueue.remove(listid);

	dl.waitTicks = 0;
	__GeTriggerWait(GPU_SYNC_LIST, listid);

	CheckDrawSync();

	return 0;
}

u32 GPUCommon::UpdateStall(int listid, u32 newstall) {
	if (listid < 0 || listid >= DisplayListMaxCount || dls[listid].state == PSP_GE_DL_STATE_NONE)
		return SCE_KERNEL_ERROR_INVALID_ID;
	auto &dl = dls[listid];
	if (dl.state == PSP_GE_DL_STATE_COMPLETED)
		return SCE_KERNEL_ERROR_ALREADY;

	dl.stall = newstall & 0x0FFFFFFF;
	
	ProcessDLQueue();

	return 0;
}

u32 GPUCommon::Continue() {
	if (!currentList)
		return 0;

	if (currentList->state == PSP_GE_DL_STATE_PAUSED)
	{
		if (!isbreak) {
			// TODO: Supposedly this returns SCE_KERNEL_ERROR_BUSY in some case, previously it had
			// currentList->signal == PSP_GE_SIGNAL_HANDLER_PAUSE, but it doesn't reproduce.

			currentList->state = PSP_GE_DL_STATE_RUNNING;
			currentList->signal = PSP_GE_SIGNAL_NONE;

			// TODO Restore context of DL is necessary
			// TODO Restore BASE

			// We have a list now, so it's not complete.
			drawCompleteTicks = (u64)-1;
		} else {
			currentList->state = PSP_GE_DL_STATE_QUEUED;
			currentList->signal = PSP_GE_SIGNAL_NONE;
		}
	}
	else if (currentList->state == PSP_GE_DL_STATE_RUNNING)
	{
		if (sceKernelGetCompiledSdkVersion() >= 0x02000000)
			return 0x80000020;
		return -1;
	}
	else
	{
		if (sceKernelGetCompiledSdkVersion() >= 0x02000000)
			return 0x80000004;
		return -1;
	}

	ProcessDLQueue();
	return 0;
}

u32 GPUCommon::Break(int mode) {
	if (mode < 0 || mode > 1)
		return SCE_KERNEL_ERROR_INVALID_MODE;

	if (!currentList)
		return SCE_KERNEL_ERROR_ALREADY;

	if (mode == 1)
	{
		// Clear the queue
		dlQueue.clear();
		for (int i = 0; i < DisplayListMaxCount; ++i)
		{
			dls[i].state = PSP_GE_DL_STATE_NONE;
			dls[i].signal = PSP_GE_SIGNAL_NONE;
		}

		nextListID = 0;
		currentList = NULL;
		return 0;
	}

	if (currentList->state == PSP_GE_DL_STATE_NONE || currentList->state == PSP_GE_DL_STATE_COMPLETED)
	{
		if (sceKernelGetCompiledSdkVersion() >= 0x02000000)
			return 0x80000004;
		return -1;
	}

	if (currentList->state == PSP_GE_DL_STATE_PAUSED)
	{
		if (sceKernelGetCompiledSdkVersion() > 0x02000010)
		{
			if (currentList->signal == PSP_GE_SIGNAL_HANDLER_PAUSE)
			{
				ERROR_LOG_REPORT(Log::G3D, "sceGeBreak: can't break signal-pausing list");
			}
			else
				return SCE_KERNEL_ERROR_ALREADY;
		}
		return SCE_KERNEL_ERROR_BUSY;
	}

	if (currentList->state == PSP_GE_DL_STATE_QUEUED)
	{
		currentList->state = PSP_GE_DL_STATE_PAUSED;
		return currentList->id;
	}

	// TODO Save BASE
	// TODO Adjust pc to be just before SIGNAL/END

	// TODO: Is this right?
	if (currentList->signal == PSP_GE_SIGNAL_SYNC)
		currentList->pc += 8;

	currentList->interrupted = true;
	currentList->state = PSP_GE_DL_STATE_PAUSED;
	currentList->signal = PSP_GE_SIGNAL_HANDLER_SUSPEND;
	isbreak = true;

	return currentList->id;
}

void GPUCommon::NotifySteppingEnter() {
	if (coreCollectDebugStats) {
		timeSteppingStarted_ = time_now_d();
	}
}
void GPUCommon::NotifySteppingExit() {
	if (coreCollectDebugStats) {
		if (timeSteppingStarted_ <= 0.0) {
			ERROR_LOG(Log::G3D, "Mismatched stepping enter/exit.");
		}
		double total = time_now_d() - timeSteppingStarted_;
		_dbg_assert_msg_(total >= 0.0, "Time spent stepping became negative");
		timeSpentStepping_ += total;
		timeSteppingStarted_ = 0.0;
	}
}

bool GPUCommon::InterpretList(DisplayList &list) {
	// Initialized to avoid a race condition with bShowDebugStats changing.
	double start = 0.0;
	if (coreCollectDebugStats) {
		start = time_now_d();
	}

	if (list.state == PSP_GE_DL_STATE_PAUSED)
		return false;
	currentList = &list;

	if (!list.started && list.context.IsValid()) {
		gstate.Save(list.context);
	}
	list.started = true;

	gstate_c.offsetAddr = list.offsetAddr;

	if (!Memory::IsValidAddress(list.pc)) {
		ERROR_LOG_REPORT(Log::G3D, "DL PC = %08x WTF!!!!", list.pc);
		return true;
	}

	cycleLastPC = list.pc;
	cyclesExecuted += 60;
	downcount = list.stall == 0 ? 0x0FFFFFFF : (list.stall - list.pc) / 4;
	list.state = PSP_GE_DL_STATE_RUNNING;
	list.interrupted = false;

	gpuState = list.pc == list.stall ? GPUSTATE_STALL : GPUSTATE_RUNNING;

	// To enable breakpoints, we don't do fast matrix loads while debugger active.
	debugRecording_ = GPUDebug::IsActive() || GPURecord::IsActive();
	const bool useFastRunLoop = !dumpThisFrame_ && !debugRecording_;
	while (gpuState == GPUSTATE_RUNNING) {
		{
			if (list.pc == list.stall) {
				gpuState = GPUSTATE_STALL;
				downcount = 0;
			}
		}

		if (useFastRunLoop) {
			FastRunLoop(list);
		} else {
			SlowRunLoop(list);
		}

		{
			downcount = list.stall == 0 ? 0x0FFFFFFF : (list.stall - list.pc) / 4;

			if (gpuState == GPUSTATE_STALL && list.stall != list.pc) {
				// Unstalled.
				gpuState = GPUSTATE_RUNNING;
			}
		}
	}

	FinishDeferred();
	if (debugRecording_)
		GPURecord::NotifyCPU();

	// We haven't run the op at list.pc, so it shouldn't count.
	if (cycleLastPC != list.pc) {
		UpdatePC(list.pc - 4, list.pc);
	}

	list.offsetAddr = gstate_c.offsetAddr;

	if (coreCollectDebugStats) {
		double total = time_now_d() - start - timeSpentStepping_;
		_dbg_assert_msg_(total >= 0.0, "Time spent DL processing became negative");
		hleSetSteppingTime(timeSpentStepping_);
		DisplayNotifySleep(timeSpentStepping_);
		timeSpentStepping_ = 0.0;
		gpuStats.msProcessingDisplayLists += total;
	}
	return gpuState == GPUSTATE_DONE || gpuState == GPUSTATE_ERROR;
}

void GPUCommon::PSPFrame() {
	immCount_ = 0;
	if (dumpNextFrame_) {
		NOTICE_LOG(Log::G3D, "DUMPING THIS FRAME");
		dumpThisFrame_ = true;
		dumpNextFrame_ = false;
	} else if (dumpThisFrame_) {
		dumpThisFrame_ = false;
	}
	GPUDebug::NotifyBeginFrame();
	GPURecord::NotifyBeginFrame();
}

bool GPUCommon::PresentedThisFrame() const {
	return framebufferManager_ ? framebufferManager_->PresentedThisFrame() : true;
}

void GPUCommon::SlowRunLoop(DisplayList &list) {
	const bool dumpThisFrame = dumpThisFrame_;
	while (downcount > 0) {
		bool process = GPUDebug::NotifyCommand(list.pc);
		if (process) {
			GPURecord::NotifyCommand(list.pc);
			u32 op = Memory::ReadUnchecked_U32(list.pc);
			u32 cmd = op >> 24;

			u32 diff = op ^ gstate.cmdmem[cmd];
			PreExecuteOp(op, diff);
			if (dumpThisFrame) {
				char temp[256];
				u32 prev;
				if (Memory::IsValidAddress(list.pc - 4)) {
					prev = Memory::ReadUnchecked_U32(list.pc - 4);
				} else {
					prev = 0;
				}
				GeDisassembleOp(list.pc, op, prev, temp, 256);
				NOTICE_LOG(Log::G3D, "%08x: %s", op, temp);
			}
			gstate.cmdmem[cmd] = op;

			ExecuteOp(op, diff);
		}

		list.pc += 4;
		--downcount;
	}
}

// The newPC parameter is used for jumps, we don't count cycles between.
void GPUCommon::UpdatePC(u32 currentPC, u32 newPC) {
	// Rough estimate, 2 CPU ticks (it's double the clock rate) per GPU instruction.
	u32 executed = (currentPC - cycleLastPC) / 4;
	cyclesExecuted += 2 * executed;
	cycleLastPC = newPC;

	// Exit the runloop and recalculate things.  This happens a lot in some games.
	if (currentList)
		downcount = currentList->stall == 0 ? 0x0FFFFFFF : (currentList->stall - newPC) / 4;
	else
		downcount = 0;
}

void GPUCommon::ReapplyGfxState() {
	// The commands are embedded in the command memory so we can just reexecute the words. Convenient.
	// To be safe we pass 0xFFFFFFFF as the diff.

	// TODO: Consider whether any of this should really be done. We might be able to get all the way
	// by simplying dirtying the appropriate gstate_c dirty flags.

	for (int i = GE_CMD_VERTEXTYPE; i < GE_CMD_BONEMATRIXNUMBER; i++) {
		if (i != GE_CMD_ORIGIN && i != GE_CMD_OFFSETADDR) {
			ExecuteOp(gstate.cmdmem[i], 0xFFFFFFFF);
		}
	}

	// Can't write to bonematrixnumber here

	for (int i = GE_CMD_MORPHWEIGHT0; i <= GE_CMD_PATCHFACING; i++) {
		ExecuteOp(gstate.cmdmem[i], 0xFFFFFFFF);
	}

	// There are a few here in the middle that we shouldn't execute...

	// 0x42 to 0xEA
	for (int i = GE_CMD_VIEWPORTXSCALE; i < GE_CMD_TRANSFERSTART; i++) {
		switch (i) {
		case GE_CMD_LOADCLUT:
		case GE_CMD_TEXSYNC:
		case GE_CMD_TEXFLUSH:
			break;
		default:
			ExecuteOp(gstate.cmdmem[i], 0xFFFFFFFF);
			break;
		}
	}

	// Let's just skip the transfer size stuff, it's just values.
}

uint32_t GPUCommon::SetAddrTranslation(uint32_t value) {
	std::swap(edramTranslation_, value);
	return value;
}

uint32_t GPUCommon::GetAddrTranslation() {
	return edramTranslation_;
}

inline void GPUCommon::UpdateState(GPURunState state) {
	gpuState = state;
	if (state != GPUSTATE_RUNNING)
		downcount = 0;
}

int GPUCommon::GetNextListIndex() {
	auto iter = dlQueue.begin();
	if (iter != dlQueue.end()) {
		return *iter;
	} else {
		return -1;
	}
}

void GPUCommon::ProcessDLQueue() {
	startingTicks = CoreTiming::GetTicks();
	cyclesExecuted = 0;

	// Seems to be correct behaviour to process the list anyway?
	if (startingTicks < busyTicks) {
		DEBUG_LOG(Log::G3D, "Can't execute a list yet, still busy for %lld ticks", busyTicks - startingTicks);
		//return;
	}

	for (int listIndex = GetNextListIndex(); listIndex != -1; listIndex = GetNextListIndex()) {
		DisplayList &l = dls[listIndex];
		DEBUG_LOG(Log::G3D, "Starting DL execution at %08x - stall = %08x", l.pc, l.stall);
		if (!InterpretList(l)) {
			return;
		} else {
			// Some other list could've taken the spot while we dilly-dallied around.
			if (l.state != PSP_GE_DL_STATE_QUEUED) {
				// At the end, we can remove it from the queue and continue.
				dlQueue.erase(std::remove(dlQueue.begin(), dlQueue.end(), listIndex), dlQueue.end());
			}
		}
	}

	currentList = nullptr;

	if (coreCollectDebugStats) {
		gpuStats.otherGPUCycles += cyclesExecuted;
	}

	drawCompleteTicks = startingTicks + cyclesExecuted;
	busyTicks = std::max(busyTicks, drawCompleteTicks);
	__GeTriggerSync(GPU_SYNC_DRAW, 1, drawCompleteTicks);
	// Since the event is in CoreTiming, we're in sync.  Just set 0 now.
}

void GPUCommon::Execute_OffsetAddr(u32 op, u32 diff) {
	gstate_c.offsetAddr = op << 8;
}

void GPUCommon::Execute_Vaddr(u32 op, u32 diff) {
	gstate_c.vertexAddr = gstate_c.getRelativeAddress(op & 0x00FFFFFF);
}

void GPUCommon::Execute_Iaddr(u32 op, u32 diff) {
	gstate_c.indexAddr = gstate_c.getRelativeAddress(op & 0x00FFFFFF);
}

void GPUCommon::Execute_Origin(u32 op, u32 diff) {
	if (currentList)
		gstate_c.offsetAddr = currentList->pc;
}

void GPUCommon::Execute_Jump(u32 op, u32 diff) {
	const u32 target = gstate_c.getRelativeAddress(op & 0x00FFFFFC);
	if (!Memory::IsValidAddress(target)) {
		ERROR_LOG(Log::G3D, "JUMP to illegal address %08x - ignoring! data=%06x", target, op & 0x00FFFFFF);
		UpdateState(GPUSTATE_ERROR);
		return;
	}
	UpdatePC(currentList->pc, target - 4);
	currentList->pc = target - 4; // pc will be increased after we return, counteract that
}

void GPUCommon::Execute_BJump(u32 op, u32 diff) {
	if (!currentList->bboxResult) {
		// bounding box jump.
		const u32 target = gstate_c.getRelativeAddress(op & 0x00FFFFFC);
		gpuStats.numBBOXJumps++;
		if (Memory::IsValidAddress(target)) {
			UpdatePC(currentList->pc, target - 4);
			currentList->pc = target - 4; // pc will be increased after we return, counteract that
		} else {
			ERROR_LOG(Log::G3D, "BJUMP to illegal address %08x - ignoring! data=%06x", target, op & 0x00FFFFFF);
			UpdateState(GPUSTATE_ERROR);
		}
	}
}

void GPUCommon::Execute_Call(u32 op, u32 diff) {
	PROFILE_THIS_SCOPE("gpu_call");

	const u32 target = gstate_c.getRelativeAddress(op & 0x00FFFFFC);
	if (!Memory::IsValidAddress(target)) {
		ERROR_LOG(Log::G3D, "CALL to illegal address %08x - ignoring! data=%06x", target, op & 0x00FFFFFF);
		if (g_Config.bIgnoreBadMemAccess) {
			return;
		}
		UpdateState(GPUSTATE_ERROR);
		return;
	}
	DoExecuteCall(target);
}

void GPUCommon::DoExecuteCall(u32 target) {
	// Bone matrix optimization - many games will CALL a bone matrix (!).
	// We don't optimize during recording - so the matrix data gets recorded.
	if (!debugRecording_ && Memory::IsValidRange(target, 13 * 4) && (Memory::ReadUnchecked_U32(target) >> 24) == GE_CMD_BONEMATRIXDATA) {
		// Check for the end
		if ((Memory::ReadUnchecked_U32(target + 11 * 4) >> 24) == GE_CMD_BONEMATRIXDATA &&
			(Memory::ReadUnchecked_U32(target + 12 * 4) >> 24) == GE_CMD_RET &&
			(gstate.boneMatrixNumber & 0x00FFFFFF) <= 96 - 12) {
			// Yep, pretty sure this is a bone matrix call.  Double check stall first.
			if (target > currentList->stall || target + 12 * 4 < currentList->stall) {
				FastLoadBoneMatrix(target);
				return;
			}
		}
	}

	if (currentList->stackptr == ARRAY_SIZE(currentList->stack)) {
		ERROR_LOG(Log::G3D, "CALL: Stack full!");
		// TODO: UpdateState(GPUSTATE_ERROR) ?
	} else {
		auto &stackEntry = currentList->stack[currentList->stackptr++];
		stackEntry.pc = currentList->pc + 4;
		stackEntry.offsetAddr = gstate_c.offsetAddr;
		// The base address is NOT saved/restored for a regular call.
		UpdatePC(currentList->pc, target - 4);
		currentList->pc = target - 4;	// pc will be increased after we return, counteract that
	}
}

void GPUCommon::Execute_Ret(u32 op, u32 diff) {
	if (currentList->stackptr == 0) {
		DEBUG_LOG(Log::G3D, "RET: Stack empty!");
	} else {
		auto &stackEntry = currentList->stack[--currentList->stackptr];
		gstate_c.offsetAddr = stackEntry.offsetAddr;
		// We always clear the top (uncached/etc.) bits
		const u32 target = stackEntry.pc & 0x0FFFFFFF;
		UpdatePC(currentList->pc, target - 4);
		currentList->pc = target - 4;
#ifdef _DEBUG
		if (!Memory::IsValidAddress(currentList->pc)) {
			ERROR_LOG_REPORT(Log::G3D, "Invalid DL PC %08x on return", currentList->pc);
			UpdateState(GPUSTATE_ERROR);
		}
#endif
	}
}

void GPUCommon::Execute_End(u32 op, u32 diff) {
	if (flushOnParams_)
		Flush();

	const u32 prev = Memory::ReadUnchecked_U32(currentList->pc - 4);
	UpdatePC(currentList->pc, currentList->pc);
	// Count in a few extra cycles on END.
	cyclesExecuted += 60;

	switch (prev >> 24) {
	case GE_CMD_SIGNAL:
		{
			// TODO: see http://code.google.com/p/jpcsp/source/detail?r=2935#
			SignalBehavior behaviour = static_cast<SignalBehavior>((prev >> 16) & 0xFF);
			const int signal = prev & 0xFFFF;
			const int enddata = op & 0xFFFF;
			bool trigger = true;
			currentList->subIntrToken = signal;

			switch (behaviour) {
			case PSP_GE_SIGNAL_HANDLER_SUSPEND:
				// Suspend the list, and call the signal handler.  When it's done, resume.
				// Before sdkver 0x02000010, listsync should return paused.
				if (sceKernelGetCompiledSdkVersion() <= 0x02000010)
					currentList->state = PSP_GE_DL_STATE_PAUSED;
				currentList->signal = behaviour;
				DEBUG_LOG(Log::G3D, "Signal with wait. signal/end: %04x %04x", signal, enddata);
				break;
			case PSP_GE_SIGNAL_HANDLER_CONTINUE:
				// Resume the list right away, then call the handler.
				currentList->signal = behaviour;
				DEBUG_LOG(Log::G3D, "Signal without wait. signal/end: %04x %04x", signal, enddata);
				break;
			case PSP_GE_SIGNAL_HANDLER_PAUSE:
				// Pause the list instead of ending at the next FINISH.
				// Call the handler with the PAUSE signal value at that FINISH.
				// Technically, this ought to trigger an interrupt, but it won't do anything.
				// But right now, signal is always reset by interrupts, so that causes pause to not work.
				trigger = false;
				currentList->signal = behaviour;
				DEBUG_LOG(Log::G3D, "Signal with Pause. signal/end: %04x %04x", signal, enddata);
				break;
			case PSP_GE_SIGNAL_SYNC:
				// Acts as a memory barrier, never calls any user code.
				// Technically, this ought to trigger an interrupt, but it won't do anything.
				// Triggering here can cause incorrect rescheduling, which breaks 3rd Birthday.
				// However, this is likely a bug in how GE signal interrupts are handled.
				trigger = false;
				currentList->signal = behaviour;
				DEBUG_LOG(Log::G3D, "Signal with Sync. signal/end: %04x %04x", signal, enddata);
				break;
			case PSP_GE_SIGNAL_JUMP:
			case PSP_GE_SIGNAL_RJUMP:
			case PSP_GE_SIGNAL_OJUMP:
				{
					trigger = false;
					currentList->signal = behaviour;
					// pc will be increased after we return, counteract that.
					u32 target = (((signal << 16) | enddata) & 0xFFFFFFFC) - 4;
					const char *targetType = "absolute";
					if (behaviour == PSP_GE_SIGNAL_RJUMP) {
						target += currentList->pc - 4;
						targetType = "relative";
					} else if (behaviour == PSP_GE_SIGNAL_OJUMP) {
						target = gstate_c.getRelativeAddress(target);
						targetType = "origin";
					}

					if (!Memory::IsValidAddress(target)) {
						ERROR_LOG_REPORT(Log::G3D, "Signal with Jump (%s): bad address. signal/end: %04x %04x", targetType, signal, enddata);
						UpdateState(GPUSTATE_ERROR);
					} else {
						UpdatePC(currentList->pc, target);
						currentList->pc = target;
						DEBUG_LOG(Log::G3D, "Signal with Jump (%s). signal/end: %04x %04x", targetType, signal, enddata);
					}
				}
				break;
			case PSP_GE_SIGNAL_CALL:
			case PSP_GE_SIGNAL_RCALL:
			case PSP_GE_SIGNAL_OCALL:
				{
					trigger = false;
					currentList->signal = behaviour;
					// pc will be increased after we return, counteract that.
					u32 target = (((signal << 16) | enddata) & 0xFFFFFFFC) - 4;
					const char *targetType = "absolute";
					if (behaviour == PSP_GE_SIGNAL_RCALL) {
						target += currentList->pc - 4;
						targetType = "relative";
					} else if (behaviour == PSP_GE_SIGNAL_OCALL) {
						target = gstate_c.getRelativeAddress(target);
						targetType = "origin";
					}

					if (currentList->stackptr == ARRAY_SIZE(currentList->stack)) {
						ERROR_LOG_REPORT(Log::G3D, "Signal with Call (%s): stack full. signal/end: %04x %04x", targetType, signal, enddata);
					} else if (!Memory::IsValidAddress(target)) {
						ERROR_LOG_REPORT(Log::G3D, "Signal with Call (%s): bad address. signal/end: %04x %04x", targetType, signal, enddata);
						UpdateState(GPUSTATE_ERROR);
					} else {
						// TODO: This might save/restore other state...
						auto &stackEntry = currentList->stack[currentList->stackptr++];
						stackEntry.pc = currentList->pc;
						stackEntry.offsetAddr = gstate_c.offsetAddr;
						stackEntry.baseAddr = gstate.base;
						UpdatePC(currentList->pc, target);
						currentList->pc = target;
						DEBUG_LOG(Log::G3D, "Signal with Call (%s). signal/end: %04x %04x", targetType, signal, enddata);
					}
				}
				break;
			case PSP_GE_SIGNAL_RET:
				{
					trigger = false;
					currentList->signal = behaviour;
					if (currentList->stackptr == 0) {
						ERROR_LOG_REPORT(Log::G3D, "Signal with Return: stack empty. signal/end: %04x %04x", signal, enddata);
					} else {
						// TODO: This might save/restore other state...
						auto &stackEntry = currentList->stack[--currentList->stackptr];
						gstate_c.offsetAddr = stackEntry.offsetAddr;
						gstate.base = stackEntry.baseAddr;
						UpdatePC(currentList->pc, stackEntry.pc);
						currentList->pc = stackEntry.pc;
						DEBUG_LOG(Log::G3D, "Signal with Return. signal/end: %04x %04x", signal, enddata);
					}
				}
				break;
			default:
				ERROR_LOG_REPORT(Log::G3D, "UNKNOWN Signal UNIMPLEMENTED %i ! signal/end: %04x %04x", behaviour, signal, enddata);
				break;
			}
			// TODO: Technically, jump/call/ret should generate an interrupt, but before the pc change maybe?
			if (currentList->interruptsEnabled && trigger) {
				if (__GeTriggerInterrupt(currentList->id, currentList->pc, startingTicks + cyclesExecuted)) {
					currentList->pendingInterrupt = true;
					UpdateState(GPUSTATE_INTERRUPT);
				}
			}
		}
		break;
	case GE_CMD_FINISH:
		switch (currentList->signal) {
		case PSP_GE_SIGNAL_HANDLER_PAUSE:
			currentList->state = PSP_GE_DL_STATE_PAUSED;
			if (currentList->interruptsEnabled) {
				if (__GeTriggerInterrupt(currentList->id, currentList->pc, startingTicks + cyclesExecuted)) {
					currentList->pendingInterrupt = true;
					UpdateState(GPUSTATE_INTERRUPT);
				}
			}
			break;

		case PSP_GE_SIGNAL_SYNC:
			currentList->signal = PSP_GE_SIGNAL_NONE;
			// TODO: Technically this should still cause an interrupt.  Probably for memory sync.
			break;

		default:
			FlushImm();
			currentList->subIntrToken = prev & 0xFFFF;
			UpdateState(GPUSTATE_DONE);
			// Since we marked done, we have to restore the context now before the next list runs.
			if (currentList->started && currentList->context.IsValid()) {
				gstate.Restore(currentList->context);
				ReapplyGfxState();
				// Don't restore the context again.
				currentList->started = false;
			}

			if (currentList->interruptsEnabled && __GeTriggerInterrupt(currentList->id, currentList->pc, startingTicks + cyclesExecuted)) {
				currentList->pendingInterrupt = true;
			} else {
				currentList->state = PSP_GE_DL_STATE_COMPLETED;
				currentList->waitTicks = startingTicks + cyclesExecuted;
				busyTicks = std::max(busyTicks, currentList->waitTicks);
				__GeTriggerSync(GPU_SYNC_LIST, currentList->id, currentList->waitTicks);
			}
			break;
		}
		break;
	default:
		DEBUG_LOG(Log::G3D,"Ah, not finished: %06x", prev & 0xFFFFFF);
		break;
	}
}

void GPUCommon::Execute_BoundingBox(u32 op, u32 diff) {
	// Just resetting, nothing to check bounds for.
	const u32 count = op & 0xFFFF;
	if (count == 0) {
		currentList->bboxResult = false;
		return;
	}

	// Approximate based on timings of several counts on a PSP.
	cyclesExecuted += count * 22;

	const bool useInds = (gstate.vertType & GE_VTYPE_IDX_MASK) != 0;
	VertexDecoder *dec = drawEngineCommon_->GetVertexDecoder(gstate.vertType);
	int bytesRead = (useInds ? 1 : dec->VertexSize()) * count;

	if (Memory::IsValidRange(gstate_c.vertexAddr, bytesRead)) {
		const void *control_points = Memory::GetPointerUnchecked(gstate_c.vertexAddr);
		if (!control_points) {
			ERROR_LOG_REPORT_ONCE(boundingbox, Log::G3D, "Invalid verts in bounding box check");
			currentList->bboxResult = true;
			return;
		}

		const void *inds = nullptr;
		if (useInds) {
			int indexShift = ((gstate.vertType & GE_VTYPE_IDX_MASK) >> GE_VTYPE_IDX_SHIFT) - 1;
			inds = Memory::GetPointerUnchecked(gstate_c.indexAddr);
			if (!inds || !Memory::IsValidRange(gstate_c.indexAddr, count << indexShift)) {
				ERROR_LOG_REPORT_ONCE(boundingboxInds, Log::G3D, "Invalid inds in bounding box check");
				currentList->bboxResult = true;
				return;
			}
		}

		// Test if the bounding box is within the drawing region.
		// The PSP only seems to vary the result based on a single range of 0x100.
		if (count > 0x200) {
			// The second to last set of 0x100 is checked (even for odd counts.)
			size_t skipSize = (count - 0x200) * dec->VertexSize();
			currentList->bboxResult = drawEngineCommon_->TestBoundingBox((const uint8_t *)control_points + skipSize, inds, 0x100, gstate.vertType);
		} else if (count > 0x100) {
			int checkSize = count - 0x100;
			currentList->bboxResult = drawEngineCommon_->TestBoundingBox(control_points, inds, checkSize, gstate.vertType);
		} else {
			currentList->bboxResult = drawEngineCommon_->TestBoundingBox(control_points, inds, count, gstate.vertType);
		}
		AdvanceVerts(gstate.vertType, count, bytesRead);
	} else {
		ERROR_LOG_REPORT_ONCE(boundingbox, Log::G3D, "Bad bounding box data: %06x", count);
		// Data seems invalid. Let's assume the box test passed.
		currentList->bboxResult = true;
	}
}

void GPUCommon::Execute_MorphWeight(u32 op, u32 diff) {
	gstate_c.morphWeights[(op >> 24) - GE_CMD_MORPHWEIGHT0] = getFloat24(op);
}

void GPUCommon::Execute_ImmVertexAlphaPrim(u32 op, u32 diff) {
	// Safety check.
	if (immCount_ >= MAX_IMMBUFFER_SIZE) {
		// Only print once for each overrun.
		if (immCount_ == MAX_IMMBUFFER_SIZE) {
			ERROR_LOG_REPORT_ONCE(exceed_imm_buffer, Log::G3D, "Exceeded immediate draw buffer size. gstate.imm_ap=%06x , prim=%d", gstate.imm_ap & 0xFFFFFF, (int)immPrim_);
		}
		if (immCount_ < 0x7fffffff)  // Paranoia :)
			immCount_++;
		return;
	}

	int prim = (op >> 8) & 0x7;
	if (prim != GE_PRIM_KEEP_PREVIOUS) {
		// Flush before changing the prim type.  Only continue can be used to continue a prim.
		FlushImm();
	}

	TransformedVertex &v = immBuffer_[immCount_++];

	// ThrillVille does a clear with this, additional parameters found via tests.
	// The current vtype affects how the coordinate is processed.
	if (gstate.isModeThrough()) {
		v.x = ((int)(gstate.imm_vscx & 0xFFFF) - 0x8000) / 16.0f;
		v.y = ((int)(gstate.imm_vscy & 0xFFFF) - 0x8000) / 16.0f;
	} else {
		int offsetX = gstate.getOffsetX16();
		int offsetY = gstate.getOffsetY16();
		v.x = ((int)(gstate.imm_vscx & 0xFFFF) - offsetX) / 16.0f;
		v.y = ((int)(gstate.imm_vscy & 0xFFFF) - offsetY) / 16.0f;
	}
	v.z = gstate.imm_vscz & 0xFFFF;
	v.pos_w = 1.0f;
	v.u = getFloat24(gstate.imm_vtcs);
	v.v = getFloat24(gstate.imm_vtct);
	v.uv_w = getFloat24(gstate.imm_vtcq);
	v.color0_32 = (gstate.imm_cv & 0xFFFFFF) | (gstate.imm_ap << 24);
	// TODO: When !gstate.isModeThrough(), direct fog coefficient (0 = entirely fog), ignore fog flag (also GE_IMM_FOG.)
	v.fog = (gstate.imm_fc & 0xFF) / 255.0f;
	// TODO: Apply if gstate.isUsingSecondaryColor() && !gstate.isModeThrough(), ignore lighting flag.
	v.color1_32 = gstate.imm_scv & 0xFFFFFF;
	if (prim != GE_PRIM_KEEP_PREVIOUS) {
		immPrim_ = (GEPrimitiveType)prim;
		// Flags seem to only be respected from the first prim.
		immFlags_ = op & 0x00FFF800;
		immFirstSent_ = false;
	} else if (prim == GE_PRIM_KEEP_PREVIOUS && immPrim_ != GE_PRIM_INVALID) {
		static constexpr int flushPrimCount[] = { 1, 2, 0, 3, 0, 0, 2, 0 };
		// Instead of finding a proper point to flush, we just emit prims when we can.
		if (immCount_ == flushPrimCount[immPrim_ & 7])
			FlushImm();
	} else {
		ERROR_LOG_REPORT_ONCE(imm_draw_prim, Log::G3D, "Immediate draw: Unexpected primitive %d at count %d", prim, immCount_);
	}
}

void GPUCommon::FlushImm() {
	if (immCount_ == 0 || immPrim_ == GE_PRIM_INVALID)
		return;

	SetDrawType(DRAW_PRIM, immPrim_);
	VirtualFramebuffer *vfb = nullptr;
	if (framebufferManager_)
		vfb = framebufferManager_->SetRenderFrameBuffer(gstate_c.IsDirty(DIRTY_FRAMEBUF), gstate_c.skipDrawReason);
	if (gstate_c.skipDrawReason & (SKIPDRAW_SKIPFRAME | SKIPDRAW_NON_DISPLAYED_FB)) {
		// No idea how many cycles to skip, heh.
		immCount_ = 0;
		return;
	}
	gstate_c.UpdateUVScaleOffset();
	if (vfb) {
		CheckDepthUsage(vfb);
	}

	bool antialias = (immFlags_ & GE_IMM_ANTIALIAS) != 0;
	bool prevAntialias = gstate.isAntiAliasEnabled();
	bool shading = (immFlags_ & GE_IMM_SHADING) != 0;
	bool prevShading = gstate.getShadeMode() == GE_SHADE_GOURAUD;
	bool cullEnable = (immFlags_ & GE_IMM_CULLENABLE) != 0;
	bool prevCullEnable = gstate.isCullEnabled();
	int cullMode = (immFlags_ & GE_IMM_CULLFACE) != 0 ? 1 : 0;
	bool texturing = (immFlags_ & GE_IMM_TEXTURE) != 0;
	bool prevTexturing = gstate.isTextureMapEnabled();
	bool fog = (immFlags_ & GE_IMM_FOG) != 0;
	bool prevFog = gstate.isFogEnabled();
	bool dither = (immFlags_ & GE_IMM_DITHER) != 0;
	bool prevDither = gstate.isDitherEnabled();

	if ((immFlags_ & GE_IMM_CLIPMASK) != 0) {
		WARN_LOG_REPORT_ONCE(geimmclipvalue, Log::G3D, "Imm vertex used clip value, flags=%06x", immFlags_);
	}

	bool changed = texturing != prevTexturing || cullEnable != prevCullEnable || dither != prevDither;
	changed = changed || prevShading != shading || prevFog != fog;
	if (changed) {
		DispatchFlush();
		gstate.antiAliasEnable = (GE_CMD_ANTIALIASENABLE << 24) | (int)antialias;
		gstate.shademodel = (GE_CMD_SHADEMODE << 24) | (int)shading;
		gstate.cullfaceEnable = (GE_CMD_CULLFACEENABLE << 24) | (int)cullEnable;
		gstate.textureMapEnable = (GE_CMD_TEXTUREMAPENABLE << 24) | (int)texturing;
		gstate.fogEnable = (GE_CMD_FOGENABLE << 24) | (int)fog;
		gstate.ditherEnable = (GE_CMD_DITHERENABLE << 24) | (int)dither;
		gstate_c.Dirty(DIRTY_VERTEXSHADER_STATE | DIRTY_FRAGMENTSHADER_STATE | DIRTY_RASTER_STATE | DIRTY_VIEWPORTSCISSOR_STATE | DIRTY_UVSCALEOFFSET | DIRTY_CULLRANGE);
	}

	drawEngineCommon_->DispatchSubmitImm(immPrim_, immBuffer_, immCount_, cullMode, immFirstSent_);
	immCount_ = 0;
	immFirstSent_ = true;

	if (changed) {
		DispatchFlush();
		gstate.antiAliasEnable = (GE_CMD_ANTIALIASENABLE << 24) | (int)prevAntialias;
		gstate.shademodel = (GE_CMD_SHADEMODE << 24) | (int)prevShading;
		gstate.cullfaceEnable = (GE_CMD_CULLFACEENABLE << 24) | (int)prevCullEnable;
		gstate.textureMapEnable = (GE_CMD_TEXTUREMAPENABLE << 24) | (int)prevTexturing;
		gstate.fogEnable = (GE_CMD_FOGENABLE << 24) | (int)prevFog;
		gstate.ditherEnable = (GE_CMD_DITHERENABLE << 24) | (int)prevDither;
		gstate_c.Dirty(DIRTY_VERTEXSHADER_STATE | DIRTY_FRAGMENTSHADER_STATE | DIRTY_RASTER_STATE | DIRTY_VIEWPORTSCISSOR_STATE | DIRTY_UVSCALEOFFSET | DIRTY_CULLRANGE);
	}
}

void GPUCommon::Execute_Unknown(u32 op, u32 diff) {
	if ((op & 0xFFFFFF) != 0)
		WARN_LOG_REPORT_ONCE(unknowncmd, Log::G3D, "Unknown GE command : %08x ", op);
}

void GPUCommon::FastLoadBoneMatrix(u32 target) {
	const u32 num = gstate.boneMatrixNumber & 0x7F;
	_dbg_assert_msg_(num + 12 <= 96, "FastLoadBoneMatrix would corrupt memory");
	const u32 mtxNum = num / 12;
	u32 uniformsToDirty = DIRTY_BONEMATRIX0 << mtxNum;
	if (num != 12 * mtxNum) {
		uniformsToDirty |= DIRTY_BONEMATRIX0 << ((mtxNum + 1) & 7);
	}

	if (!g_Config.bSoftwareSkinning) {
		if (flushOnParams_)
			Flush();
		gstate_c.Dirty(uniformsToDirty);
	} else {
		gstate_c.deferredVertTypeDirty |= uniformsToDirty;
	}
	gstate.FastLoadBoneMatrix(target);

	cyclesExecuted += 2 * 14;  // one to reset the counter, 12 to load the matrix, and a return.

	if (coreCollectDebugStats) {
		gpuStats.otherGPUCycles += 2 * 14;
	}
}

struct DisplayList_v1 {
	int id;
	u32 startpc;
	u32 pc;
	u32 stall;
	DisplayListState state;
	SignalBehavior signal;
	int subIntrBase;
	u16 subIntrToken;
	DisplayListStackEntry stack[32];
	int stackptr;
	bool interrupted;
	u64 waitTicks;
	bool interruptsEnabled;
	bool pendingInterrupt;
	bool started;
	size_t contextPtr;
	u32 offsetAddr;
	bool bboxResult;
};

struct DisplayList_v2 {
	int id;
	u32 startpc;
	u32 pc;
	u32 stall;
	DisplayListState state;
	SignalBehavior signal;
	int subIntrBase;
	u16 subIntrToken;
	DisplayListStackEntry stack[32];
	int stackptr;
	bool interrupted;
	u64 waitTicks;
	bool interruptsEnabled;
	bool pendingInterrupt;
	bool started;
	PSPPointer<u32_le> context;
	u32 offsetAddr;
	bool bboxResult;
};

void GPUCommon::DoState(PointerWrap &p) {
	auto s = p.Section("GPUCommon", 1, 6);
	if (!s)
		return;

	Do<int>(p, dlQueue);
	if (s >= 4) {
		DoArray(p, dls, ARRAY_SIZE(dls));
	} else if (s >= 3) {
		// This may have been saved with or without padding, depending on platform.
		// We need to upconvert it to our consistently-padded struct.
		static const size_t DisplayList_v3_size = 452;
		static const size_t DisplayList_v4_size = 456;
		static_assert(DisplayList_v4_size == sizeof(DisplayList), "Make sure to change here when updating DisplayList");

		p.DoVoid(&dls[0], DisplayList_v3_size);
		dls[0].padding = 0;

		const u8 *savedPtr = *p.GetPPtr();
		const u32 *savedPtr32 = (const u32 *)savedPtr;
		// Here's the trick: the first member (id) is always the same as the index.
		// The second member (startpc) is always an address, or 0, never 1.  So we can see the padding.
		const bool hasPadding = savedPtr32[1] == 1;
		if (hasPadding) {
			u32 padding;
			Do(p, padding);
		}

		for (size_t i = 1; i < ARRAY_SIZE(dls); ++i) {
			p.DoVoid(&dls[i], DisplayList_v3_size);
			dls[i].padding = 0;
			if (hasPadding) {
				u32 padding;
				Do(p, padding);
			}
		}
	} else if (s >= 2) {
		for (size_t i = 0; i < ARRAY_SIZE(dls); ++i) {
			DisplayList_v2 oldDL;
			Do(p, oldDL);
			// Copy over everything except the last, new member (stackAddr.)
			memcpy(&dls[i], &oldDL, sizeof(DisplayList_v2));
			dls[i].stackAddr = 0;
		}
	} else {
		// Can only be in read mode here.
		for (size_t i = 0; i < ARRAY_SIZE(dls); ++i) {
			DisplayList_v1 oldDL;
			Do(p, oldDL);
			// On 32-bit, they're the same, on 64-bit oldDL is bigger.
			memcpy(&dls[i], &oldDL, sizeof(DisplayList_v1));
			// Fix the other fields.  Let's hope context wasn't important, it was a pointer.
			dls[i].context = 0;
			dls[i].offsetAddr = oldDL.offsetAddr;
			dls[i].bboxResult = oldDL.bboxResult;
			dls[i].stackAddr = 0;
		}
	}
	int currentID = 0;
	if (currentList != nullptr) {
		currentID = (int)(currentList - &dls[0]);
	}
	Do(p, currentID);
	if (currentID == 0) {
		currentList = nullptr;
	} else {
		currentList = &dls[currentID];
	}
	Do(p, interruptRunning);
	Do(p, gpuState);
	Do(p, isbreak);
	Do(p, drawCompleteTicks);
	Do(p, busyTicks);

	if (s >= 5) {
		Do(p, matrixVisible.all);
	}
	if (s >= 6) {
		Do(p, edramTranslation_);
	}
}

void GPUCommon::InterruptStart(int listid) {
	interruptRunning = true;
}
void GPUCommon::InterruptEnd(int listid) {
	interruptRunning = false;
	isbreak = false;

	DisplayList &dl = dls[listid];
	dl.pendingInterrupt = false;
	// TODO: Unless the signal handler could change it?
	if (dl.state == PSP_GE_DL_STATE_COMPLETED || dl.state == PSP_GE_DL_STATE_NONE) {
		if (dl.started && dl.context.IsValid()) {
			gstate.Restore(dl.context);
			ReapplyGfxState();
		}
		dl.waitTicks = 0;
		__GeTriggerWait(GPU_SYNC_LIST, listid);

		// Make sure the list isn't still queued since it's now completed.
		if (!dlQueue.empty()) {
			if (listid == dlQueue.front())
				PopDLQueue();
			else
				dlQueue.remove(listid);
		}
	}

	ProcessDLQueue();
}

// TODO: Maybe cleaner to keep this in GE and trigger the clear directly?
void GPUCommon::SyncEnd(GPUSyncType waitType, int listid, bool wokeThreads) {
	if (waitType == GPU_SYNC_DRAW && wokeThreads)
	{
		for (int i = 0; i < DisplayListMaxCount; ++i) {
			if (dls[i].state == PSP_GE_DL_STATE_COMPLETED) {
				dls[i].state = PSP_GE_DL_STATE_NONE;
			}
		}
	}
}

bool GPUCommon::GetCurrentDisplayList(DisplayList &list) {
	if (!currentList) {
		return false;
	}
	list = *currentList;
	return true;
}

std::vector<DisplayList> GPUCommon::ActiveDisplayLists() {
	std::vector<DisplayList> result;

	for (int it : dlQueue) {
		result.push_back(dls[it]);
	}

	return result;
}

void GPUCommon::ResetListPC(int listID, u32 pc) {
	if (listID < 0 || listID >= DisplayListMaxCount) {
		_dbg_assert_msg_(false, "listID out of range: %d", listID);
		return;
	}

	Reporting::NotifyDebugger();
	dls[listID].pc = pc;
	downcount = 0;
}

void GPUCommon::ResetListStall(int listID, u32 stall) {
	if (listID < 0 || listID >= DisplayListMaxCount) {
		_dbg_assert_msg_(false, "listID out of range: %d", listID);
		return;
	}

	Reporting::NotifyDebugger();
	dls[listID].stall = stall;
	downcount = 0;
}

void GPUCommon::ResetListState(int listID, DisplayListState state) {
	if (listID < 0 || listID >= DisplayListMaxCount) {
		_dbg_assert_msg_(false, "listID out of range: %d", listID);
		return;
	}

	Reporting::NotifyDebugger();
	dls[listID].state = state;
	downcount = 0;
}

GPUDebugOp GPUCommon::DissassembleOp(u32 pc, u32 op) {
	char buffer[1024];
	u32 prev = Memory::IsValidAddress(pc - 4) ? Memory::ReadUnchecked_U32(pc - 4) : 0;
	GeDisassembleOp(pc, op, prev, buffer, sizeof(buffer));

	GPUDebugOp info;
	info.pc = pc;
	info.cmd = op >> 24;
	info.op = op;
	info.desc = buffer;
	return info;
}

std::vector<GPUDebugOp> GPUCommon::DissassembleOpRange(u32 startpc, u32 endpc) {
	char buffer[1024];
	std::vector<GPUDebugOp> result;
	GPUDebugOp info;

	// Don't trigger a pause.
	u32 prev = Memory::IsValidAddress(startpc - 4) ? Memory::Read_U32(startpc - 4) : 0;
	result.reserve((endpc - startpc) / 4);
	for (u32 pc = startpc; pc < endpc; pc += 4) {
		u32 op = Memory::IsValidAddress(pc) ? Memory::Read_U32(pc) : 0;
		GeDisassembleOp(pc, op, prev, buffer, sizeof(buffer));
		prev = op;

		info.pc = pc;
		info.cmd = op >> 24;
		info.op = op;
		info.desc = buffer;
		result.push_back(info);
	}
	return result;
}

u32 GPUCommon::GetRelativeAddress(u32 data) {
	return gstate_c.getRelativeAddress(data);
}

u32 GPUCommon::GetVertexAddress() {
	return gstate_c.vertexAddr;
}

u32 GPUCommon::GetIndexAddress() {
	return gstate_c.indexAddr;
}

GPUgstate GPUCommon::GetGState() {
	return gstate;
}

void GPUCommon::SetCmdValue(u32 op) {
	u32 cmd = op >> 24;
	u32 diff = op ^ gstate.cmdmem[cmd];

	Reporting::NotifyDebugger();
	PreExecuteOp(op, diff);
	gstate.cmdmem[cmd] = op;
	ExecuteOp(op, diff);
	downcount = 0;
}

void GPUCommon::DoBlockTransfer(u32 skipDrawReason) {
	u32 srcBasePtr = gstate.getTransferSrcAddress();
	u32 srcStride = gstate.getTransferSrcStride();

	u32 dstBasePtr = gstate.getTransferDstAddress();
	u32 dstStride = gstate.getTransferDstStride();

	int srcX = gstate.getTransferSrcX();
	int srcY = gstate.getTransferSrcY();

	int dstX = gstate.getTransferDstX();
	int dstY = gstate.getTransferDstY();

	int width = gstate.getTransferWidth();
	int height = gstate.getTransferHeight();

	int bpp = gstate.getTransferBpp();

	DEBUG_LOG(Log::G3D, "Block transfer: %08x/%x -> %08x/%x, %ix%ix%i (%i,%i)->(%i,%i)", srcBasePtr, srcStride, dstBasePtr, dstStride, width, height, bpp, srcX, srcY, dstX, dstY);
	gpuStats.numBlockTransfers++;

	// For VRAM, we wrap around when outside valid memory (mirrors still work.)
	if ((srcBasePtr & 0x04800000) == 0x04800000)
		srcBasePtr &= ~0x00800000;
	if ((dstBasePtr & 0x04800000) == 0x04800000)
		dstBasePtr &= ~0x00800000;

	// Use height less one to account for width, which can be greater or less than stride, and then add it on for the last line.
	// NOTE: The sizes are only used for validity checks and memory info tracking.
	const uint32_t src = srcBasePtr + (srcY * srcStride + srcX) * bpp;
	const uint32_t dst = dstBasePtr + (dstY * dstStride + dstX) * bpp;
	const uint32_t srcSize = ((height - 1) * srcStride) + width * bpp;
	const uint32_t dstSize = ((height - 1) * dstStride) + width * bpp;

	bool srcDstOverlap = src + srcSize > dst && dst + dstSize > src;
	bool srcValid = Memory::IsValidRange(src, srcSize);
	bool dstValid = Memory::IsValidRange(dst, dstSize);
	bool srcWraps = Memory::IsVRAMAddress(srcBasePtr) && !srcValid;
	bool dstWraps = Memory::IsVRAMAddress(dstBasePtr) && !dstValid;

	char tag[128];
	size_t tagSize;

	// Tell the framebuffer manager to take action if possible. If it does the entire thing, let's just return.
	if (!framebufferManager_ || !framebufferManager_->NotifyBlockTransferBefore(dstBasePtr, dstStride, dstX, dstY, srcBasePtr, srcStride, srcX, srcY, width, height, bpp, skipDrawReason)) {
		// Do the copy! (Hm, if we detect a drawn video frame (see below) then we could maybe skip this?)
		// Can use GetPointerUnchecked because we checked the addresses above. We could also avoid them
		// entirely by walking a couple of pointers...

		// Simple case: just a straight copy, no overlap or wrapping.
		if (srcStride == dstStride && (u32)width == srcStride && !srcDstOverlap && srcValid && dstValid) {
			u32 srcLineStartAddr = srcBasePtr + (srcY * srcStride + srcX) * bpp;
			u32 dstLineStartAddr = dstBasePtr + (dstY * dstStride + dstX) * bpp;
			u32 bytesToCopy = width * height * bpp;

			const u8 *srcp = Memory::GetPointer(srcLineStartAddr);
			u8 *dstp = Memory::GetPointerWrite(dstLineStartAddr);
			memcpy(dstp, srcp, bytesToCopy);

			if (MemBlockInfoDetailed(bytesToCopy)) {
				NotifyMemInfoCopy(dst, src, bytesToCopy, "GPUBlockTransfer/");
			}
		} else if ((srcDstOverlap || srcWraps || dstWraps) && (srcValid || srcWraps) && (dstValid || dstWraps)) {
			// This path means we have either src/dst overlap, OR one or both of src and dst wrap.
			// This should be uncommon so it's the slowest path.
			u32 bytesToCopy = width * bpp;
			bool notifyDetail = MemBlockInfoDetailed(srcWraps || dstWraps ? 64 : bytesToCopy);
			bool notifyAll = !notifyDetail && MemBlockInfoDetailed(srcSize, dstSize);
			if (notifyDetail || notifyAll) {
				tagSize = FormatMemWriteTagAt(tag, sizeof(tag), "GPUBlockTransfer/", src, srcSize);
			}

			auto notifyingMemmove = [&](u32 d, u32 s, u32 sz) {
				const u8 *srcp = Memory::GetPointer(s);
				u8 *dstp = Memory::GetPointerWrite(d);
				memmove(dstp, srcp, sz);

				if (notifyDetail) {
					NotifyMemInfo(MemBlockFlags::READ, s, sz, tag, tagSize);
					NotifyMemInfo(MemBlockFlags::WRITE, d, sz, tag, tagSize);
				}
			};

			for (int y = 0; y < height; y++) {
				u32 srcLineStartAddr = srcBasePtr + ((y + srcY) * srcStride + srcX) * bpp;
				u32 dstLineStartAddr = dstBasePtr + ((y + dstY) * dstStride + dstX) * bpp;
				// If we already passed a wrap, we can use the quicker path.
				if ((srcLineStartAddr & 0x04800000) == 0x04800000)
					srcLineStartAddr &= ~0x00800000;
				if ((dstLineStartAddr & 0x04800000) == 0x04800000)
					dstLineStartAddr &= ~0x00800000;
				// These flags mean there's a wrap inside this line.
				bool srcLineWrap = !Memory::IsValidRange(srcLineStartAddr, bytesToCopy);
				bool dstLineWrap = !Memory::IsValidRange(dstLineStartAddr, bytesToCopy);

				if (!srcLineWrap && !dstLineWrap) {
					const u8 *srcp = Memory::GetPointer(srcLineStartAddr);
					u8 *dstp = Memory::GetPointerWrite(dstLineStartAddr);
					for (u32 i = 0; i < bytesToCopy; i += 64) {
						u32 chunk = i + 64 > bytesToCopy ? bytesToCopy - i : 64;
						memmove(dstp + i, srcp + i, chunk);
					}

					// If we're tracking detail, it's useful to have the gaps illustrated properly.
					if (notifyDetail) {
						NotifyMemInfo(MemBlockFlags::READ, srcLineStartAddr, bytesToCopy, tag, tagSize);
						NotifyMemInfo(MemBlockFlags::WRITE, dstLineStartAddr, bytesToCopy, tag, tagSize);
					}
				} else {
					// We can wrap at any point, so along with overlap this gets a bit complicated.
					// We're just going to do this the slow and easy way.
					u32 srcLinePos = srcLineStartAddr;
					u32 dstLinePos = dstLineStartAddr;
					for (u32 i = 0; i < bytesToCopy; i += 64) {
						u32 chunk = i + 64 > bytesToCopy ? bytesToCopy - i : 64;
						u32 srcValid = Memory::ValidSize(srcLinePos, chunk);
						u32 dstValid = Memory::ValidSize(dstLinePos, chunk);

						// First chunk, for which both are valid.
						u32 bothSize = std::min(srcValid, dstValid);
						if (bothSize != 0)
							notifyingMemmove(dstLinePos, srcLinePos, bothSize);

						// Now, whichever side has more valid (or the rest, if only one side must wrap.)
						u32 exclusiveSize = std::max(srcValid, dstValid) - bothSize;
						if (exclusiveSize != 0 && srcValid >= dstValid) {
							notifyingMemmove(PSP_GetVidMemBase(), srcLineStartAddr + bothSize, exclusiveSize);
						} else if (exclusiveSize != 0 && srcValid < dstValid) {
							notifyingMemmove(dstLineStartAddr + bothSize, PSP_GetVidMemBase(), exclusiveSize);
						}

						// Finally, if both src and dst wrapped, that portion.
						u32 wrappedSize = chunk - bothSize - exclusiveSize;
						if (wrappedSize != 0 && srcValid >= dstValid) {
							notifyingMemmove(PSP_GetVidMemBase() + exclusiveSize, PSP_GetVidMemBase(), wrappedSize);
						} else if (wrappedSize != 0 && srcValid < dstValid) {
							notifyingMemmove(PSP_GetVidMemBase(), PSP_GetVidMemBase() + exclusiveSize, wrappedSize);
						}

						srcLinePos += chunk;
						dstLinePos += chunk;
						if ((srcLinePos & 0x04800000) == 0x04800000)
							srcLinePos &= ~0x00800000;
						if ((dstLinePos & 0x04800000) == 0x04800000)
							dstLinePos &= ~0x00800000;
					}
				}
			}

			if (notifyAll) {
				if (srcWraps) {
					u32 validSize = Memory::ValidSize(src, srcSize);
					NotifyMemInfo(MemBlockFlags::READ, src, validSize, tag, tagSize);
					NotifyMemInfo(MemBlockFlags::READ, PSP_GetVidMemBase(), srcSize - validSize, tag, tagSize);
				} else {
					NotifyMemInfo(MemBlockFlags::READ, src, srcSize, tag, tagSize);
				}
				if (dstWraps) {
					u32 validSize = Memory::ValidSize(dst, dstSize);
					NotifyMemInfo(MemBlockFlags::WRITE, dst, validSize, tag, tagSize);
					NotifyMemInfo(MemBlockFlags::WRITE, PSP_GetVidMemBase(), dstSize - validSize, tag, tagSize);
				} else {
					NotifyMemInfo(MemBlockFlags::WRITE, dst, dstSize, tag, tagSize);
				}
			}
		} else if (srcValid && dstValid) {
			u32 bytesToCopy = width * bpp;
			bool notifyDetail = MemBlockInfoDetailed(bytesToCopy);
			bool notifyAll = !notifyDetail && MemBlockInfoDetailed(srcSize, dstSize);
			if (notifyDetail || notifyAll) {
				tagSize = FormatMemWriteTagAt(tag, sizeof(tag), "GPUBlockTransfer/", src, srcSize);
			}

			for (int y = 0; y < height; y++) {
				u32 srcLineStartAddr = srcBasePtr + ((y + srcY) * srcStride + srcX) * bpp;
				u32 dstLineStartAddr = dstBasePtr + ((y + dstY) * dstStride + dstX) * bpp;

				const u8 *srcp = Memory::GetPointer(srcLineStartAddr);
				u8 *dstp = Memory::GetPointerWrite(dstLineStartAddr);
				memcpy(dstp, srcp, bytesToCopy);

				// If we're tracking detail, it's useful to have the gaps illustrated properly.
				if (notifyDetail) {
					NotifyMemInfo(MemBlockFlags::READ, srcLineStartAddr, bytesToCopy, tag, tagSize);
					NotifyMemInfo(MemBlockFlags::WRITE, dstLineStartAddr, bytesToCopy, tag, tagSize);
				}
			}

			if (notifyAll) {
				NotifyMemInfo(MemBlockFlags::READ, src, srcSize, tag, tagSize);
				NotifyMemInfo(MemBlockFlags::WRITE, dst, dstSize, tag, tagSize);
			}
		} else {
			// This seems to cause the GE to require a break/reset on a PSP.
			// TODO: Handle that and figure out which bytes are still copied?
			ERROR_LOG_REPORT_ONCE(invalidtransfer, Log::G3D, "Block transfer invalid: %08x/%x -> %08x/%x, %ix%ix%i (%i,%i)->(%i,%i)", srcBasePtr, srcStride, dstBasePtr, dstStride, width, height, bpp, srcX, srcY, dstX, dstY);
		}

		if (framebufferManager_) {
			// Fixes Gran Turismo's funky text issue, since it overwrites the current texture.
			textureCache_->Invalidate(dstBasePtr + (dstY * dstStride + dstX) * bpp, height * dstStride * bpp, GPU_INVALIDATE_HINT);
			framebufferManager_->NotifyBlockTransferAfter(dstBasePtr, dstStride, dstX, dstY, srcBasePtr, srcStride, srcX, srcY, width, height, bpp, skipDrawReason);
		}
	}

	// TODO: Correct timing appears to be 1.9, but erring a bit low since some of our other timing is inaccurate.
	cyclesExecuted += ((height * width * bpp) * 16) / 10;
}

bool GPUCommon::PerformMemoryCopy(u32 dest, u32 src, int size, GPUCopyFlag flags) {
	// Track stray copies of a framebuffer in RAM. MotoGP does this.
	if (framebufferManager_->MayIntersectFramebufferColor(src) || framebufferManager_->MayIntersectFramebufferColor(dest)) {
		if (!framebufferManager_->NotifyFramebufferCopy(src, dest, size, flags, gstate_c.skipDrawReason)) {
			// We use matching values in PerformReadbackToMemory/PerformWriteColorFromMemory.
			// Since they're identical we don't need to copy.
			if (dest != src) {
				if (Memory::IsValidRange(dest, size) && Memory::IsValidRange(src, size)) {
					memcpy(Memory::GetPointerWriteUnchecked(dest), Memory::GetPointerUnchecked(src), size);
				}
				if (MemBlockInfoDetailed(size)) {
					NotifyMemInfoCopy(dest, src, size, "GPUMemcpy/");
				}
			}
		}
		InvalidateCache(dest, size, GPU_INVALIDATE_HINT);
		return true;
	}

	if (MemBlockInfoDetailed(size)) {
		NotifyMemInfoCopy(dest, src, size, "GPUMemcpy/");
	}
	InvalidateCache(dest, size, GPU_INVALIDATE_HINT);
	if (!(flags & GPUCopyFlag::DEBUG_NOTIFIED))
		GPURecord::NotifyMemcpy(dest, src, size);
	return false;
}

bool GPUCommon::PerformMemorySet(u32 dest, u8 v, int size) {
	// This may indicate a memset, usually to 0, of a framebuffer.
	if (framebufferManager_->MayIntersectFramebufferColor(dest)) {
		Memory::Memset(dest, v, size, "GPUMemset");
		if (!framebufferManager_->NotifyFramebufferCopy(dest, dest, size, GPUCopyFlag::MEMSET, gstate_c.skipDrawReason)) {
			InvalidateCache(dest, size, GPU_INVALIDATE_HINT);
		}
		return true;
	}

	NotifyMemInfo(MemBlockFlags::WRITE, dest, size, "GPUMemset");
	// Or perhaps a texture, let's invalidate.
	InvalidateCache(dest, size, GPU_INVALIDATE_HINT);
	GPURecord::NotifyMemset(dest, v, size);
	return false;
}

bool GPUCommon::PerformReadbackToMemory(u32 dest, int size) {
	if (Memory::IsVRAMAddress(dest)) {
		return PerformMemoryCopy(dest, dest, size, GPUCopyFlag::FORCE_DST_MATCH_MEM);
	}
	return false;
}

bool GPUCommon::PerformWriteColorFromMemory(u32 dest, int size) {
	if (Memory::IsVRAMAddress(dest)) {
		GPURecord::NotifyUpload(dest, size);
		return PerformMemoryCopy(dest, dest, size, GPUCopyFlag::FORCE_SRC_MATCH_MEM | GPUCopyFlag::DEBUG_NOTIFIED);
	}
	return false;
}

void GPUCommon::PerformWriteFormattedFromMemory(u32 addr, int size, int frameWidth, GEBufferFormat format) {
	if (Memory::IsVRAMAddress(addr)) {
		framebufferManager_->PerformWriteFormattedFromMemory(addr, size, frameWidth, format);
	}
	textureCache_->NotifyWriteFormattedFromMemory(addr, size, frameWidth, format);
	InvalidateCache(addr, size, GPU_INVALIDATE_SAFE);
}

bool GPUCommon::PerformWriteStencilFromMemory(u32 dest, int size, WriteStencil flags) {
	if (framebufferManager_->MayIntersectFramebufferColor(dest)) {
		framebufferManager_->PerformWriteStencilFromMemory(dest, size, flags);
		return true;
	}
	return false;
}

bool GPUCommon::GetCurrentSimpleVertices(int count, std::vector<GPUDebugVertex> &vertices, std::vector<u16> &indices) {
	gstate_c.UpdateUVScaleOffset();
	return drawEngineCommon_->GetCurrentSimpleVertices(count, vertices, indices);
}

bool GPUCommon::DescribeCodePtr(const u8 *ptr, std::string &name) {
	// The only part of GPU emulation (other than software) that jits is the vertex decoder, currently,
	// which is owned by the drawengine.
	return drawEngineCommon_->DescribeCodePtr(ptr, name);
}
