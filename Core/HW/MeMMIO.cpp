// Copyright (c) 2012- PPSSPP Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official git repository and contact information can be found at
// https://github.com/hrydgard/ppsspp and http://www.ppsspp.org/.

#include <algorithm>
#include <cstring>
#include <vector>

#include "Common/Log.h"
#include "Core/CoreTiming.h"
#include "Core/Debugger/MemBlockInfo.h"
#include "Core/HLE/sceDisplay.h"
#include "Core/HLE/sceMpegbase.h"
#include "GPU/GPUCommon.h"
#include "GPU/ge_constants.h"
#include "Core/HW/MMIO.h"
#include "Core/HW/MeMMIO.h"
#include "Core/MemMap.h"
#include "Core/MIPS/MeCore.h"
#include "Core/MIPS/MIPS.h"

namespace MeMMIO {

using MeCore::g_meState;

static const u32 ME_INTR_CODEC_DONE = 0x20;

static bool IsME(const MIPSState *mips) {
	return mips && mips->IsMediaEngine();
}

class MeSysregDevice : public MMIODevice {
public:
	MeSysregDevice() : MMIODevice("ME sysreg", 0xBC100000, 0x100) {}

	bool Read(MIPSState *mips, u32 offset, int size, u32 *value) override {
		if (offset == 0 && g_meState) {
			// me_wrapper's bootstrap goes to the ME firmware's debug handler if this is set. Nothing
			// seems to set it.
			*value = 0;
			return true;
		}
		u32 *reg = Register(offset);
		if (!reg) {
			return false;
		}
		*value = *reg;
		return true;
	}

	bool Write(MIPSState *mips, u32 offset, int size, u32 value) override {
		if (!g_meState) {
			return false;
		}
		switch (offset) {
		case 0x44:
			// Interrupts the other CPU.
			if (value & 1) {
				if (IsME(mips)) {
					MeCore::InterruptMainCPU();
				} else {
					MeCore::RaiseInterrupt(MeCore::ME_INTR_SOFT);
				}
			}
			return true;
		case 0x48:
		{
			// Each CPU sets or clears its own bit, and can only set it while the other's is clear.
			const u32 own = IsME(mips) ? 2 : 1;
			const u32 other = own ^ 3;
			if (!(value & own)) {
				g_meState->mutex &= ~own;
			} else if (!(g_meState->mutex & other)) {
				g_meState->mutex |= own;
			}
			return true;
		}
		case 0x4C:
			g_meState->resetFlags = value;
			MeCore::SetReset((value & 4) != 0);
			return true;
		default:
			if (u32 *reg = Register(offset)) {
				*reg = value;
				return true;
			}
			return false;
		}
	}

	void Reset() override {}
	void DoState(PointerWrap &p) override {}

private:
	static u32 *Register(u32 offset) {
		if (!g_meState) {
			return nullptr;
		}
		switch (offset) {
		case 0x04: return &g_meState->nmiFlags;
		case 0x40: return &g_meState->ramConfig;
		case 0x48: return &g_meState->mutex;
		case 0x4C: return &g_meState->resetFlags;
		case 0x50: return &g_meState->busClock;
		case 0x54: return &g_meState->sysreg54;
		case 0x5C: return &g_meState->sysreg5C;
		case 0x70: return &g_meState->sysreg70;
		default: return (offset & 3) ? nullptr : &g_meState->sysregOther[offset / 4];
		}
	}
};

class MeInterruptDevice : public MMIODevice {
public:
	MeInterruptDevice() : MMIODevice("ME interrupts", 0xBC300000, 0x10) {}

	bool Read(MIPSState *mips, u32 offset, int size, u32 *value) override {
		if (!g_meState || !IsME(mips)) {
			return false;
		}
		switch (offset) {
		case 0x0: *value = g_meState->intcRaised; return true;
		case 0x8: *value = g_meState->intcEnabled; return true;
		default: return false;
		}
	}

	bool Write(MIPSState *mips, u32 offset, int size, u32 value) override {
		if (!g_meState || !IsME(mips)) {
			return false;
		}
		switch (offset) {
		case 0x0:
			// Writing a source's bit clears it.
			g_meState->intcRaised &= ~value;
			return true;
		case 0x8:
			// Source 5 is the codec hardware finishing, which Sony's ME firmware waits for in halt
			// after starting it (t2img 0x883211b4). That isn't emulated, so it's always done.
			if ((value & ~g_meState->intcEnabled) & ME_INTR_CODEC_DONE)
				g_meState->intcRaised |= ME_INTR_CODEC_DONE;
			g_meState->intcEnabled = value;
			return true;
		default:
			return false;
		}
	}

	void Reset() override {}
	void DoState(PointerWrap &p) override {}
};

class MeMemProtectDevice : public MMIODevice {
public:
	MeMemProtectDevice() : MMIODevice("ME memory protection", 0xBC000000, 0x50) {}

	bool Read(MIPSState *mips, u32 offset, int size, u32 *value) override {
		if (!g_meState || (offset & 3)) {
			return false;
		}
		*value = g_meState->memProtect[offset / 4];
		return true;
	}

	bool Write(MIPSState *mips, u32 offset, int size, u32 value) override {
		if (!g_meState || (offset & 3)) {
			return false;
		}
		g_meState->memProtect[offset / 4] = value;
		return true;
	}

	void Reset() override {}
	void DoState(PointerWrap &p) override {}
};

// A PL080-like DMA controller with the LCD and colour space conversion registers below its
// channels (4-7, at 0x180). Transfers run at once, when a channel is enabled. Addresses are physical;
// on the ME's bus (a master bit in the control word), the low ones are its eDRAM. Each linked list
// item is five words (source, destination, next, control, configuration).
class MeDmacplusDevice : public MMIODevice {
public:
	MeDmacplusDevice() : MMIODevice("DMACplus", 0xBC800000, 0x200) {}

	bool Read(MIPSState *mips, u32 offset, int size, u32 *value) override {
		u32 *reg = Register(offset);
		if (!reg)
			return false;
		*value = *reg;
		return true;
	}

	bool Write(MIPSState *mips, u32 offset, int size, u32 value) override {
		if (!g_meState || (offset & 3))
			return false;
		MeCore::MEState::Dmacplus &d = g_meState->dmacplus;
		if (offset == 0x08) {
			d.intTCStatus &= ~value;
			return true;
		}
		if (offset == 0x10) {
			// No errors to clear.
			return true;
		}
		u32 *reg = Register(offset);
		if (!reg)
			return false;
		*reg = value;
		if (offset == 0x100) {
			SetLCD(d);
		} else if (offset == 0x160 && (value & 1)) {
			RunCSC(d);
		} else if (offset >= 0x180 && (offset & 0x1F) == 0x10 && (value & 1)) {
			RunChannel(d, (offset - 0x180) / 0x20);
		}
		return true;
	}

	void Reset() override {}
	void DoState(PointerWrap &p) override {}

private:
	static u32 *Register(u32 offset) {
		if (!g_meState || (offset & 3))
			return nullptr;
		MeCore::MEState::Dmacplus &d = g_meState->dmacplus;
		if (offset == 0x00 || offset == 0x04 || offset == 0x14)
			return &d.intTCStatus;
		if (offset >= 0x100 && offset < 0x110)
			return &d.lcd[(offset - 0x100) / 4];
		if (offset >= 0x120 && offset <= 0x160)
			return &d.csc[(offset - 0x120) / 4];
		if (offset >= 0x180 && (offset & 0x1F) < 0x14)
			return &d.channels[(offset - 0x180) / 0x20][(offset & 0x1F) / 4];
		return nullptr;
	}

	// Pixel format 0 is 8888 (mcidclan's samples). The others aren't known.
	static void SetLCD(const MeCore::MEState::Dmacplus &d) {
		if (d.lcd[1] != 0) {
			WARN_LOG(Log::ME, "DMACplus: LCD pixel format %d", d.lcd[1]);
		}
		__DisplaySetFramebuf(d.lcd[0], d.lcd[3], GE_FORMAT_8888, 1);
	}

	static u8 *PhysicalPointer(u32 addr, u32 size, bool meBus) {
		addr &= 0x1FFFFFFF;
		const Memory::AddressSpace *space = g_meState->cpu.space;
		if (meBus && addr + size <= space->edramSize)
			return space->GetPointerUnchecked(addr);
		if (Memory::IsValidRange(addr, size))
			return Memory::GetPointerWriteUnchecked(addr);
		return nullptr;
	}

	// The colour space conversion, as the firmware's DMACplus driver programs it (lowio.prx 6.61,
	// sceDmacplus_driver_d126494b and 9c492b9b; dmacplus.prx 1.50 the same). The frame is two sets of
	// half its height: buffers 0, 1, 4 and 5, and 2, 3, 6 and 7. In AVC mode they're the even and odd
	// rows of a frame in the decoder's tiled layout (see UntileYCbCr); in VME mode, planar Y at 0x120
	// (0x128), Cb at 0x130 (0x138) and Cr at 0x134 (0x13C). Each set goes to its own destination, or
	// both interleave by rows into the first. Not checked on hardware: what 0x140 bits 1 and 2 do
	// (taken as chroma line replication and leaving out the second set, from mcidclan's samples),
	// and 0x15C bit 8 (taken as chroma being offset by 128).
	static void RunCSC(MeCore::MEState::Dmacplus &d) {
		const u32 *r = d.csc;
		auto reg = [&](u32 offset) { return r[(offset - 0x120) / 4]; };
		const u32 config = reg(0x140);
		const bool vme = (config & 1) != 0;
		const bool replicateChroma = (config & 2) != 0;
		const bool onlyFirst = (config & 4) != 0;
		const int width = ((config >> 8) & 0x3F) * 16;
		const int height = ((config >> 16) & 0x3F) * 16;
		const u32 output = reg(0x14C);
		const int stride = output >> 8;
		const int format = (output >> 1) & 3;
		const bool separate = (output & 1) != 0;
		d.csc[(0x160 - 0x120) / 4] &= ~1;
		d.intTCStatus |= 2;
		// A wider stride than any framebuffer would only overflow the size math below.
		if (width == 0 || height == 0 || stride < width || stride > 4096) {
			ERROR_LOG(Log::ME, "DMACplus: bad colour space conversion %dx%d, stride %d", width, height, stride);
			return;
		}

		const int setHeight = height / 2;
		const int width2 = width / 2;
		// Each set's planes, as rows: luma, and chroma at half width.
		const u8 *luma[2]{}, *cb[2]{}, *cr[2]{};
		int lumaStride = width, chromaStride = width2, chromaRowShift = 0;
		std::vector<u8> planes;
		if (vme) {
			const int chromaRows = replicateChroma ? setHeight / 2 : setHeight;
			chromaRowShift = replicateChroma ? 1 : 0;
			for (int s = 0; s < 2; ++s) {
				luma[s] = PhysicalPointer(reg(0x120 + s * 8), width * setHeight, true);
				cb[s] = PhysicalPointer(reg(0x130 + s * 8), width2 * chromaRows, true);
				cr[s] = PhysicalPointer(reg(0x134 + s * 8), width2 * chromaRows, true);
			}
		} else {
			const int blocks = (width / 16) * (height / 16);
			const int sizes[8] = { 64 * blocks, 64 * blocks, 64 * blocks, 64 * blocks, 32 * blocks, 32 * blocks, 32 * blocks, 32 * blocks };
			const u8 *src[8];
			for (int i = 0; i < 8; ++i)
				src[i] = PhysicalPointer(reg(0x120 + i * 4), sizes[i], true);
			planes.resize(width * height + 2 * width2 * (height / 2));
			u8 *frameY = planes.data();
			u8 *frameCb = frameY + width * height;
			u8 *frameCr = frameCb + width2 * (height / 2);
			if (!separate) {
				// The sets are the frame's even and odd rows, both with the frame's chroma rows.
				UntileYCbCr(frameY, frameCb, frameCr, src, sizes, width, height);
				lumaStride = width * 2;
				for (int s = 0; s < 2; ++s) {
					luma[s] = frameY + s * width;
					cb[s] = frameCb;
					cr[s] = frameCr;
				}
			} else {
				// Each set is a picture of its own, with its own chroma at half its height (as
				// mcidclan's me-avc-csc-hd lays out its data, n12ToHw.py).
				std::vector<u8> tall(width * height + 2 * width2 * (height / 2));
				for (int s = 0; s < 2; ++s) {
					const u8 *setSrc[8] = { src[s * 2], src[s * 2 + 1], nullptr, nullptr, src[4 + s * 2], src[5 + s * 2], nullptr, nullptr };
					// Untiled as the even rows of a picture twice as tall.
					UntileYCbCr(tall.data(), tall.data() + width * height, tall.data() + width * height + width2 * (height / 2), setSrc, sizes, width, height);
					u8 *setY = frameY + s * width * setHeight;
					for (int y = 0; y < setHeight; ++y)
						memcpy(setY + y * width, tall.data() + 2 * y * width, width);
					u8 *cbOut = frameCb + s * width2 * (setHeight / 2);
					u8 *crOut = frameCr + s * width2 * (setHeight / 2);
					const u8 *tallCb = tall.data() + width * height;
					const u8 *tallCr = tallCb + width2 * (height / 2);
					for (int y = 0; y < setHeight / 2; ++y) {
						memcpy(cbOut + y * width2, tallCb + 2 * y * width2, width2);
						memcpy(crOut + y * width2, tallCr + 2 * y * width2, width2);
					}
					luma[s] = setY;
					cb[s] = cbOut;
					cr[s] = crOut;
				}
				chromaRowShift = 1;
			}
		}

		auto coefficient = [](u32 value, int shift) {
			int c = (value >> shift) & 0x3FF;
			return c >= 0x200 ? c - 0x400 : c;
		};
		int coef[3][3];
		for (int c = 0; c < 3; ++c) {
			const u32 value = reg(0x150 + c * 4);
			coef[c][0] = coefficient(value, 0);
			coef[c][1] = coefficient(value, 10);
			coef[c][2] = coefficient(value, 20);
		}
		const int lumaOffset = reg(0x15C) & 0xFF;
		const int chromaOffset = (reg(0x15C) & 0x100) ? 128 : 0;

		static const GEBufferFormat formats[4] = { GE_FORMAT_8888, GE_FORMAT_565, GE_FORMAT_5551, GE_FORMAT_4444 };
		const int bpp = format == 0 ? 4 : 2;
		const u32 rows = separate ? setHeight : height;
		const u32 dstAddr[2] = { reg(0x144), reg(0x148) };
		for (int s = 0; s < 2; ++s) {
			if (separate && s == 1 && onlyFirst)
				break;
			if (!luma[s] || !cb[s] || !cr[s]) {
				ERROR_LOG(Log::ME, "DMACplus: colour space conversion source %d not readable", s);
				continue;
			}
			const u32 dst = separate ? dstAddr[s] : dstAddr[0];
			u8 *out = PhysicalPointer(dst, stride * rows * bpp, false);
			if (!out) {
				ERROR_LOG(Log::ME, "DMACplus: colour space conversion destination %08x not writable", dst);
				continue;
			}
			for (int y = 0; y < setHeight; ++y) {
				const u8 *rowY = luma[s] + y * lumaStride;
				const u8 *rowCb = cb[s] + (y >> chromaRowShift) * chromaStride;
				const u8 *rowCr = cr[s] + (y >> chromaRowShift) * chromaStride;
				const int outRow = separate ? y : y * 2 + s;
				u8 *outPixels = out + outRow * stride * bpp;
				for (int x = 0; x < width; ++x) {
					const int Y = rowY[x] - lumaOffset;
					const int Cb = rowCb[x >> 1] - chromaOffset;
					const int Cr = rowCr[x >> 1] - chromaOffset;
					int rgb[3];
					for (int c = 0; c < 3; ++c)
						rgb[c] = std::clamp((coef[c][0] * Y + coef[c][1] * Cb + coef[c][2] * Cr + 64) >> 7, 0, 255);
					switch (format) {
					case 0: ((u32_le *)outPixels)[x] = rgb[0] | (rgb[1] << 8) | (rgb[2] << 16); break;
					case 1: ((u16_le *)outPixels)[x] = (rgb[0] >> 3) | ((rgb[1] >> 2) << 5) | ((rgb[2] >> 3) << 11); break;
					case 2: ((u16_le *)outPixels)[x] = (rgb[0] >> 3) | ((rgb[1] >> 3) << 5) | ((rgb[2] >> 3) << 10); break;
					default: ((u16_le *)outPixels)[x] = (rgb[0] >> 4) | ((rgb[1] >> 4) << 4) | ((rgb[2] >> 4) << 8); break;
					}
				}
			}
			const u32 size = stride * rows * bpp;
			NotifyMemInfo(MemBlockFlags::WRITE, dst & 0x1FFFFFFF, size, "DMACplusCsc");
			if (gpu)
				gpu->PerformWriteFormattedFromMemory(dst & 0x1FFFFFFF, size, stride, formats[format], width);
		}
	}

	static void RunChannel(MeCore::MEState::Dmacplus &d, int channel) {
		u32 *regs = d.channels[channel];
		// Bounded, in case of a list that loops.
		for (int item = 0; item < 0x10000; ++item) {
			const u32 control = regs[3];
			const u32 width = 1 << std::min((control >> 18) & 7, 4U);
			const u32 bytes = (control & 0xFFF) * width;
			u8 *src = PhysicalPointer(regs[0], bytes, (control >> 24) & 1);
			u8 *dst = PhysicalPointer(regs[1], bytes, (control >> 25) & 1);
			if (src && dst) {
				memmove(dst, src, bytes);
				NotifyMemInfo(MemBlockFlags::WRITE, regs[1] & 0x1FFFFFFF, bytes, "DMACplus");
			} else {
				ERROR_LOG(Log::ME, "DMACplus: bad transfer of %d bytes from %08x to %08x", bytes, regs[0], regs[1]);
			}
			if (control & 0x80000000)
				d.intTCStatus |= 1 << (channel + 4);
			const u32 next = regs[2];
			if (next == 0)
				break;
			const u8 *lli = PhysicalPointer(next & ~3, 20, false);
			if (!lli) {
				ERROR_LOG(Log::ME, "DMACplus: bad linked list item at %08x", next);
				break;
			}
			for (int i = 0; i < 5; ++i)
				regs[i] = ((const u32_le *)lli)[i];
		}
		regs[4] &= ~1;
	}
};

// The VME's control registers. The VME itself (the ME's reconfigurable coprocessor) isn't emulated:
// writes are kept, and its status (0x10) reads as done, which homebrew's ME startup waits for
// (custom-core's meLibAVCSetTransferConfig, Yabause).
class MeVmeControlDevice : public MMIODevice {
public:
	MeVmeControlDevice() : MMIODevice("VME control", 0xBCC00000, 0x80) {}

	bool Read(MIPSState *mips, u32 offset, int size, u32 *value) override {
		if (!g_meState || (offset & 3))
			return false;
		*value = offset == 0x10 ? 0 : g_meState->vmeControl[offset / 4];
		return true;
	}

	bool Write(MIPSState *mips, u32 offset, int size, u32 value) override {
		if (!g_meState || (offset & 3))
			return false;
		g_meState->vmeControl[offset / 4] = value;
		return true;
	}

	void Reset() override {}
	void DoState(PointerWrap &p) override {}
};

// Clock dividers (0xBC200000), which the ME's startup code sets and Sony's ME firmware lowers around
// its halt. Only kept.
class MeClockDevice : public MMIODevice {
public:
	MeClockDevice() : MMIODevice("ME clocks", 0xBC200000, 0x10) {}

	bool Read(MIPSState *mips, u32 offset, int size, u32 *value) override {
		if (!g_meState || (offset & 3))
			return false;
		*value = g_meState->clocks[offset / 4];
		return true;
	}

	bool Write(MIPSState *mips, u32 offset, int size, u32 value) override {
		if (!g_meState || (offset & 3))
			return false;
		g_meState->clocks[offset / 4] = value;
		return true;
	}

	void Reset() override {}
	void DoState(PointerWrap &p) override {}
};

// The DDR controller. Writing 5 to 0x04 flushes writes to RAM, done when it reads 0 again, which
// MIST does before handing the ME a task. Writes reach RAM at once here.
class MeDdrDevice : public MMIODevice {
public:
	MeDdrDevice() : MMIODevice("DDR", 0xBD000000, 0x10) {}

	bool Read(MIPSState *mips, u32 offset, int size, u32 *value) override {
		if (!g_meState || offset != 4)
			return false;
		*value = 0;
		return true;
	}

	bool Write(MIPSState *mips, u32 offset, int size, u32 value) override {
		return g_meState && offset == 4;
	}

	void Reset() override {}
	void DoState(PointerWrap &p) override {}
};

// The audio block (0xBE000000), as POPS's ME loop (popsman.prx +2F88) drives it: it sets the block
// up, then writes one stereo sample (left in the low half) to 0x70 whenever status 0x28 bit 5 says
// the FIFO has room, sleeping in halt in between on interrupt source 12. As measured from the ME on
// a PSP-1000 (pspautotest me/audiofifo): the FIFO holds 24 samples and plays them at 44.1kHz while
// 0x04 bit 1 is set; status 0x28 has bit 4 while playing, bit 5 while there's room, and bit 1 once
// it has played out its last sample, which comes a sample after the FIFO is empty; and source 12 is a level, up while there's room, which halt
// wakes on with interrupts disabled.
class MeAudioDevice : public MMIODevice {
public:
	MeAudioDevice() : MMIODevice("audio", 0xBE000000, 0x80) {}

	bool Read(MIPSState *mips, u32 offset, int size, u32 *value) override {
		if (!g_meState || (offset & 3))
			return false;
		MeMMIO::AudioUpdate(MeCore::GetTicks());
		const MeCore::MEState::Audio &a = g_meState->audio;
		switch (offset) {
		case REG_STATUS: *value = 0; break;
		case REG_FIFO_STATUS: *value = FifoStatus(a); break;
		default: *value = a.regs[offset / 4]; break;
		}
		return true;
	}

	bool Write(MIPSState *mips, u32 offset, int size, u32 value) override {
		if (!g_meState || (offset & 3))
			return false;
		const s64 now = MeCore::GetTicks();
		MeMMIO::AudioUpdate(now);
		MeCore::MEState::Audio &a = g_meState->audio;
		if (offset != REG_DATA) {
			a.regs[offset / 4] = value;
			return true;
		}
		if (a.fifoCount == FIFO_DEPTH) {
			WARN_LOG(Log::ME, "Audio FIFO overflow");
			return true;
		}
		if (a.fifoCount == 0)
			a.nextPlayed = now + SamplePeriod();
		a.fifo[a.fifoCount++] = value;
		a.underrun = false;
		a.lastPlayed = INT64_MAX;
		MeMMIO::AudioUpdate(now);
		return true;
	}

	void Reset() override {}
	void DoState(PointerWrap &p) override {}

	static constexpr u32 FIFO_DEPTH = ARRAY_SIZE(MeCore::MEState::Audio::fifo);
	static constexpr u32 REG_CONTROL = 0x04;
	static constexpr u32 REG_STATUS = 0x0C;
	static constexpr u32 REG_FIFO_STATUS = 0x28;
	static constexpr u32 REG_DATA = 0x70;
	static constexpr u32 CONTROL_PLAY = 2;
	static constexpr u32 FIFO_ROOM = 0x20;
	static constexpr u32 FIFO_PLAYING = 0x10;
	static constexpr u32 FIFO_UNDERRUN = 0x02;
	static constexpr u32 INTR_AUDIO = 1 << 12;

	static s64 SamplePeriod() {
		return CoreTiming::GetClockFrequencyHz() / 44100;
	}

	static bool Playing(const MeCore::MEState::Audio &a) {
		return (a.regs[REG_CONTROL / 4] & CONTROL_PLAY) != 0;
	}

	static u32 FifoStatus(const MeCore::MEState::Audio &a) {
		u32 status = a.fifoCount < FIFO_DEPTH ? FIFO_ROOM : 0;
		if (Playing(a))
			status |= FIFO_PLAYING;
		if (a.underrun)
			status |= FIFO_UNDERRUN;
		return status;
	}
};

// Played samples on their way to the host's mix. Not saved: a load starts with silence.
static std::vector<u32> g_audioPlayed;
static size_t g_audioPlayedRead = 0;
// The ME plays in bursts, a PS1 frame at a time: keep a little more than a frame queued.
static const size_t AUDIO_MIX_PRIME = 1536;
static const size_t AUDIO_MIX_MAX = 8192;
static bool g_audioMixPrimed = false;

static MeAudioDevice g_audio;
static MeDdrDevice g_ddr;
static MeClockDevice g_clocks;
static MeVmeControlDevice g_vmeControl;
static MeDmacplusDevice g_dmacplus;
static MeSysregDevice g_sysreg;
static MeInterruptDevice g_interrupts;
static MeMemProtectDevice g_memProtect;

MMIODevice *SysregDevice() {
	return &g_sysreg;
}

MMIODevice *InterruptDevice() {
	return &g_interrupts;
}

MMIODevice *ClockDevice() {
	return &g_clocks;
}

MMIODevice *VmeControlDevice() {
	return &g_vmeControl;
}

MMIODevice *DmacplusDevice() {
	return &g_dmacplus;
}

MMIODevice *MemProtectDevice() {
	return &g_memProtect;
}

MMIODevice *DdrDevice() {
	return &g_ddr;
}

MMIODevice *AudioDevice() {
	return &g_audio;
}

void AudioUpdate(s64 now) {
	MeCore::MEState::Audio &a = g_meState->audio;
	const bool playing = MeAudioDevice::Playing(a);
	while (playing && a.fifoCount > 0 && a.nextPlayed <= now) {
		if (g_audioPlayed.size() - g_audioPlayedRead < AUDIO_MIX_MAX * 2)
			g_audioPlayed.push_back(a.fifo[0]);
		memmove(a.fifo, a.fifo + 1, (a.fifoCount - 1) * sizeof(u32));
		a.fifoCount--;
		a.nextPlayed += MeAudioDevice::SamplePeriod();
		// The sample leaving the FIFO still has to play: it ran dry a sample later.
		a.lastPlayed = a.nextPlayed;
	}
	if (playing && a.fifoCount == 0 && a.lastPlayed <= now)
		a.underrun = true;
	if (playing && a.fifoCount < MeAudioDevice::FIFO_DEPTH)
		g_meState->intcRaised |= MeAudioDevice::INTR_AUDIO;
	else
		g_meState->intcRaised &= ~MeAudioDevice::INTR_AUDIO;
}

s64 AudioNextEvent() {
	const MeCore::MEState::Audio &a = g_meState->audio;
	return MeAudioDevice::Playing(a) && a.fifoCount == MeAudioDevice::FIFO_DEPTH ? a.nextPlayed : INT64_MAX;
}

void AudioMix(s32 *mix, int count) {
	size_t queued = g_audioPlayed.size() - g_audioPlayedRead;
	if (!g_audioMixPrimed) {
		if (queued < AUDIO_MIX_PRIME)
			return;
		g_audioMixPrimed = true;
	}
	if (queued > AUDIO_MIX_MAX) {
		g_audioPlayedRead = g_audioPlayed.size() - AUDIO_MIX_PRIME;
		queued = AUDIO_MIX_PRIME;
	}
	if (queued < (size_t)count)
		g_audioMixPrimed = false;
	for (int i = 0; i < count && g_audioPlayedRead < g_audioPlayed.size(); i++) {
		const u32 sample = g_audioPlayed[g_audioPlayedRead++];
		mix[i * 2] += (s16)(sample & 0xFFFF);
		mix[i * 2 + 1] += (s16)(sample >> 16);
	}
	if (g_audioPlayedRead > AUDIO_MIX_MAX * 4) {
		g_audioPlayed.erase(g_audioPlayed.begin(), g_audioPlayed.begin() + g_audioPlayedRead);
		g_audioPlayedRead = 0;
	}
}

}  // namespace MeMMIO
