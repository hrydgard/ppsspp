#pragma once

// Plugin hook: lets an application injected into the emulator (for example a
// rain droplet overlay) draw into a game's frame between its world and its UI.
//
// Guest side: the game (a PSP plugin) calls
//   sceIoDevctl("emulator:", 0x35, &counter, 4, (void *)listPos, 0)
// between drawing its world and its UI. listPos is where the commands it has
// written into its display list end at that moment, or 0 if it cannot tell.
// The u32 counter is incremented once the GE has executed the list up to that
// point, and the host callback runs there.
//
// Host side: a DLL loaded into the emulator looks up the exported
// PPSSPP_RegisterBeforeUIDrawDraw and registers a callback, which receives a
// PPSSPPBeforeUIDrawTarget.
//
// This header is included only by GPU/GPUCommon.cpp. All of its state is local
// to that translation unit; ProcessDLQueue calls three hooks:
//   BeforeUIDraw::Enter   when a list starts or resumes running,
//   BeforeUIDraw::Reached at the top of the run loop,
//   BeforeUIDraw::Leave   after the run loop.

#include <cstdint>

#include "Common/GPU/thin3d.h"
#include "Core/MemMap.h"
#include "Core/System.h"
#include "GPU/GPUCommon.h"
#include "GPU/GPUState.h"
#include "GPU/Common/FramebufferManagerCommon.h"
#include "GPU/Common/TextureCacheCommon.h"

// Plain struct so that a host application needs no emulator headers. The two
// pointers are a Draw::DrawContext and a Draw::Framebuffer (Common/GPU/thin3d.h).
struct PPSSPPBeforeUIDrawTarget {
	void *draw = nullptr;
	void *frame = nullptr;
	int width = 0;        // size of that framebuffer, in pixels
	int height = 0;
	int shownWidth = 0;   // size the frame is shown at in the window
	int shownHeight = 0;
	void *memory = nullptr;       // PSP memory base
	uint32_t reportAddress = 0;   // the counter address the guest reported with
	void (*release)(void *object) = nullptr;  // releases thin3d objects the host made
};

typedef void (*PPSSPPBeforeUIDrawDrawFn)(const PPSSPPBeforeUIDrawTarget *target);

namespace BeforeUIDraw {

static PPSSPPBeforeUIDrawDrawFn g_draw = nullptr;
static const GPUCommon *g_owner = nullptr;  // GPU the point below belongs to
static u32 g_counter = 0;   // counter still to be incremented at the point, 0 if none
static u32 g_report = 0;    // counter address of the last report, for the host
static u32 g_pos = 0;       // end of the world in the display list; kept across
                            // reports that pass 0, so the host drawing does not
                            // come and go on frames the guest cannot place
static bool g_fired = true; // the point of the last report has been handled
static bool g_split = false;
static u32 g_realStall = 0;

// The GE may read a list through an uncached mirror (0x48...) while the game
// reports a cached address (0x08...): compare the bits both share.
static u32 Offset(u32 addr) {
	return addr & 0x0FFFFFFF;
}

static void Increment(u32 counterAddr) {
	Memory::WriteUnchecked_U32(Memory::ReadUnchecked_U32(counterAddr) + 1, counterAddr);
}

static void ReleaseObject(void *object) {
	if (object)
		static_cast<Draw::RefCountedObject *>(object)->Release();
}

// The world of the frame is drawn and its UI is not.
template <typename FinishFn>
static void Fire(GPUCommon *gpuCommon, FinishFn finishDeferred) {
	finishDeferred();
	gpuCommon->Flush();

	if (g_counter) {
		Increment(g_counter);
		g_counter = 0;
	}
	if (!g_draw)
		return;

	PPSSPPBeforeUIDrawTarget target{};
	FramebufferManagerCommon *framebuffers = gpuCommon->GetFramebufferManagerCommon();
	const VirtualFramebuffer *vfb = framebuffers ? framebuffers->GetCurrentRenderVFB() : nullptr;
	if (vfb && vfb->fbo) {
		target.draw = (void *)gpuCommon->GetDrawContext();
		target.frame = (void *)vfb->fbo;
		target.width = vfb->renderWidth;
		target.height = vfb->renderHeight;
		target.shownWidth = PSP_CoreParameter().pixelWidth;
		target.shownHeight = PSP_CoreParameter().pixelHeight;
		target.memory = (void *)Memory::base;
		target.reportAddress = g_report;
		target.release = &ReleaseObject;
	}
	g_draw(&target);

	// The host bound its own pipelines, textures, viewport and scissor behind the
	// GPU caches, so the rest of the frame sets all of its state again.
	gstate_c.Dirty(DIRTY_ALL);
	if (TextureCacheCommon *textures = gpuCommon->GetTextureCacheCommon())
		textures->ForgetLastTexture();
}

// The run has reached the point: put the real stall back, fire, and let the
// list carry on to the UI. Returns true if it fired.
template <typename FinishFn>
static bool Reached(GPUCommon *gpuCommon, DisplayList &list, int &downcount, GPURunState &state, FinishFn finishDeferred) {
	if (!g_split || list.pc != list.stall)
		return false;
	g_split = false;
	g_fired = true;
	list.stall = g_realStall;
	Fire(gpuCommon, finishDeferred);
	downcount = list.stall == 0 ? 0x0FFFFFFF : (list.stall - list.pc) / 4;
	state = list.pc == list.stall ? GPUSTATE_STALL : GPUSTATE_RUNNING;
	return true;
}

// A list starts or resumes running: if the point is ahead of it (and not past
// its real stall), make the point its stall for this run.
template <typename FinishFn>
static void Enter(GPUCommon *gpuCommon, DisplayList &list, int &downcount, GPURunState &state, FinishFn finishDeferred) {
	if (g_owner != gpuCommon) {
		// A new GPU (another game was started): forget the old point.
		g_owner = gpuCommon;
		g_pos = 0;
		g_fired = true;
		g_split = false;
		g_counter = 0;
	}
	if (g_split || g_fired || g_pos == 0)
		return;
	const u32 pos = Offset(g_pos);
	if (Offset(list.pc) > pos || (list.stall != 0 && pos > Offset(list.stall)))
		return;
	g_realStall = list.stall;
	g_split = true;
	// Use the mirror the GE is reading the list through, so the run stops exactly there.
	list.stall = ((list.stall ? list.stall : list.pc) & 0xF0000000) | pos;
	downcount = (list.stall - list.pc) / 4;
	state = list.pc == list.stall ? GPUSTATE_STALL : GPUSTATE_RUNNING;
	// The list may already be at the point.
	Reached(gpuCommon, list, downcount, state, finishDeferred);
}

// The list left the run loop before reaching the point (a signal interrupt,
// END/FINISH, an error): never leave the point as its stall. It is set up again
// when the list resumes. (A debugger break returns before this and keeps it.)
static void Leave(DisplayList &list) {
	if (g_split) {
		g_split = false;
		list.stall = g_realStall;
	}
}

// sceIoDevctl 0x35, see the top of this file.
static void Report(u32 counterAddr, u32 listPos) {
	g_report = counterAddr;
	g_fired = false;
	if (listPos == 0) {
		// The point is not known for this frame: count now, and draw at the
		// point the guest last gave.
		g_counter = 0;
		Increment(counterAddr);
		return;
	}
	// A report whose list never ran is dropped rather than counted a frame late.
	g_counter = counterAddr;
	g_pos = listPos;
}

}  // namespace BeforeUIDraw

// Called from sceIoDevctl (Core/HLE/sceIo.cpp).
void BeforeUIDrawReport(u32 counterAddr, u32 listPos) {
	if (Memory::IsValidRange(counterAddr, 4))
		BeforeUIDraw::Report(counterAddr, listPos);
}

#if defined(_WIN32)
// Looked up by host plugins with GetProcAddress on the emulator executable.
extern "C" __declspec(dllexport) void PPSSPP_RegisterBeforeUIDrawDraw(PPSSPPBeforeUIDrawDrawFn fn) {
	BeforeUIDraw::g_draw = fn;
}
#endif
