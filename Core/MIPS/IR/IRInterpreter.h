#pragma once

#include "Common/CommonTypes.h"

class MIPSState;
struct IRInst;

inline static u32 ReverseBits32(u32 v) {
	// http://graphics.stanford.edu/~seander/bithacks.html#ReverseParallel
	// swap odd and even bits
	v = ((v >> 1) & 0x55555555) | ((v & 0x55555555) << 1);
	// swap consecutive pairs
	v = ((v >> 2) & 0x33333333) | ((v & 0x33333333) << 2);
	// swap nibbles ...
	v = ((v >> 4) & 0x0F0F0F0F) | ((v & 0x0F0F0F0F) << 4);
	// swap bytes
	v = ((v >> 8) & 0x00FF00FF) | ((v & 0x00FF00FF) << 8);
	// swap 2-byte long pairs
	v = ( v >> 16             ) | ( v               << 16);
	return v;
}

u32 IRRunBreakpoint(u32 pc);
u32 IRRunMemCheck(u32 pc, u32 addr);

u32 IRInterpret(MIPSState *ms, const IRInst *inst);
