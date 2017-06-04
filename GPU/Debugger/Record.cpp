// Copyright (c) 2017- PPSSPP Project.

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
#include "base/stringutil.h"
#include "Common/Common.h"
#include "Common/FileUtil.h"
#include "Common/Log.h"
#include "Core/Core.h"
#include "Core/CoreTiming.h"
#include "Core/ELF/ParamSFO.h"
#include "Core/FileSystems/MetaFileSystem.h"
#include "Core/HLE/sceDisplay.h"
#include "Core/HLE/sceKernelMemory.h"
#include "Core/MemMap.h"
#include "Core/MIPS/MIPS.h"
#include "Core/System.h"
#include "GPU/GPUInterface.h"
#include "GPU/GPUState.h"
#include "GPU/ge_constants.h"
#include "GPU/Common/TextureDecoder.h"
#include "GPU/Common/VertexDecoderCommon.h"
#include "GPU/Debugger/Record.h"

namespace GPURecord {

static const char *HEADER = "PPSSPPGE";
static const int VERSION = 1;

static bool active = false;
static bool nextFrame = false;
static bool writePending = false;

enum class CommandType : u8 {
	INIT = 0,
	REGISTERS = 1,
	VERTICES = 2,
	INDICES = 3,
	CLUT = 4,
	TRANSFERSRC = 5,
	MEMSET = 6,
	MEMCPYDEST = 7,
	MEMCPYDATA = 8,
	DISPLAY = 9,

	TEXTURE0 = 0x10,
	TEXTURE1 = 0x11,
	TEXTURE2 = 0x12,
	TEXTURE3 = 0x13,
	TEXTURE4 = 0x14,
	TEXTURE5 = 0x15,
	TEXTURE6 = 0x16,
	TEXTURE7 = 0x17,
};

#pragma pack(push,1)

struct Command {
	CommandType type;
	u32 sz;
	u32 ptr;
};

#pragma pack(pop)

static std::vector<u8> pushbuf;
static std::vector<Command> commands;
static std::vector<u32> lastRegisters;
static std::vector<u32> lastTextures;

// TODO: Maybe move execute to another file?
static u32 execIndPtr;
static u32 execVertPtr;
static u32 execClutPtr;
static u32 execTransferPtr;
static u32 execMemcpyDest;
static u32 execTexturePtrs[8];
static std::vector<u32> execListQueue;

static void FlushRegisters() {
	if (!lastRegisters.empty()) {
		Command last{CommandType::REGISTERS};
		last.ptr = (u32)pushbuf.size();
		last.sz = (u32)(lastRegisters.size() * sizeof(u32));
		pushbuf.resize(pushbuf.size() + last.sz);
		memcpy(pushbuf.data() + last.ptr, lastRegisters.data(), last.sz);
		lastRegisters.clear();

		commands.push_back(last);
	}
}

static std::string GenRecordingFilename() {
	const std::string dumpDir = GetSysDirectory(DIRECTORY_DUMP);
	const std::string prefix = dumpDir + "/" + g_paramSFO.GetDiscID();

	File::CreateFullPath(dumpDir);

	for (int n = 1; n < 10000; ++n) {
		std::string filename = StringFromFormat("%s_%04d.ppdmp", prefix.c_str(), n);
		if (!File::Exists(filename)) {
			return filename;
		}
	}

	return StringFromFormat("%s_%04d.ppdmp", prefix.c_str(), 9999);
}

static void EmitDisplayBuf() {
	struct DisplayBufData {
		PSPPointer<u8> topaddr;
		u32 linesize, pixelFormat;
	};

	DisplayBufData disp{};
	__DisplayGetFramebuf(&disp.topaddr, &disp.linesize, &disp.pixelFormat, 0);

	u32 ptr = (u32)pushbuf.size();
	u32 sz = (u32)sizeof(disp);
	pushbuf.resize(pushbuf.size() + sz);
	memcpy(pushbuf.data() + ptr, &disp, sz);

	commands.push_back({CommandType::DISPLAY, sz, ptr});
}

static void BeginRecording() {
	u32 ptr = (u32)pushbuf.size();
	u32 sz = 512 * 4;
	pushbuf.resize(pushbuf.size() + sz);
	gstate.Save((u32_le *)(pushbuf.data() + ptr));

	commands.push_back({CommandType::INIT, sz, ptr});
}

static void WriteRecording() {
	FlushRegisters();
	EmitDisplayBuf();

	const std::string filename = GenRecordingFilename();
	FILE *fp = File::OpenCFile(filename, "wb");
	fwrite(HEADER, sizeof(HEADER), 1, fp);
	fwrite(&VERSION, sizeof(VERSION), 1, fp);

	int sz = (int)commands.size();
	fwrite(&sz, sizeof(sz), 1, fp);
	int bufsz = (int)pushbuf.size();
	fwrite(&bufsz, sizeof(bufsz), 1, fp);

	for (int i = 0; i < sz; ++i) {
		const Command &cmd = commands[i];
		fwrite(&cmd.type, sizeof(cmd.type), 1, fp);
		fwrite(&cmd.sz, sizeof(cmd.sz), 1, fp);
		fwrite(&cmd.ptr, sizeof(cmd.ptr), 1, fp);
	}

	fwrite(pushbuf.data(), bufsz, 1, fp);

	fclose(fp);
}

static void GetVertDataSizes(int vcount, const void *indices, u32 &vbytes, u32 &ibytes) {
	VertexDecoder vdec;
	VertexDecoderOptions opts{};
	vdec.SetVertexType(gstate.vertType, opts);

	if (indices) {
		u16 lower = 0;
		u16 upper = 0;
		GetIndexBounds(indices, vcount, gstate.vertType, &lower, &upper);

		vbytes = (upper + 1) * vdec.VertexSize();
		u32 idx = gstate.vertType & GE_VTYPE_IDX_MASK;
		if (idx == GE_VTYPE_IDX_8BIT) {
			ibytes = vcount * sizeof(u8);
		} else if (idx == GE_VTYPE_IDX_16BIT) {
			ibytes = vcount * sizeof(u16);
		} else if (idx == GE_VTYPE_IDX_32BIT) {
			ibytes = vcount * sizeof(u32);
		}
	} else {
		vbytes = vcount * vdec.VertexSize();
	}
}

static const u8 *mymemmem(const u8 *haystack, size_t hlen, const u8 *needle, size_t nlen) {
    if (!nlen) {
        return nullptr;
	}

	const u8 *last_possible = haystack + hlen - nlen;
    int first = *needle;
    const u8 *p = haystack;
    while (p <= last_possible) {
		p = (const u8 *)memchr(p, first, last_possible - p + 1);
		if (!p) {
			return nullptr;
		}
        if (!memcmp(p, needle, nlen)) {
            return p;
		}

        p++;
    }

    return nullptr;
}

static Command EmitCommandWithRAM(CommandType t, const void *p, u32 sz) {
	FlushRegisters();

	Command cmd{t, sz, 0};

	if (sz) {
		// If at all possible, try to find it already in the buffer.
		const u8 *prev = nullptr;
		const size_t NEAR_WINDOW = std::max((int)sz * 2, 1024 * 10);
		// Let's try nearby first... it will often be nearby.
		if (pushbuf.size() > NEAR_WINDOW) {
			prev = mymemmem(pushbuf.data() + pushbuf.size() - NEAR_WINDOW, NEAR_WINDOW, (const u8 *)p, sz);
		}
		if (!prev) {
			prev = mymemmem(pushbuf.data(), pushbuf.size(), (const u8 *)p, sz);
		}

		if (prev) {
			cmd.ptr = (u32)(prev - pushbuf.data());
		} else {
			cmd.ptr = (u32)pushbuf.size();
			pushbuf.resize(pushbuf.size() + sz);
			memcpy(pushbuf.data() + cmd.ptr, p, sz);
		}
	}

	commands.push_back(cmd);

	return cmd;
}

static void EmitTextureData(int level, u32 texaddr) {
	GETextureFormat format = gstate.getTextureFormat();
	int w = gstate.getTextureWidth(level);
	int h = gstate.getTextureHeight(level);
	int bufw = GetTextureBufw(level, texaddr, format);
	int extraw = w > bufw ? w - bufw : 0;
	u32 sizeInRAM = (textureBitsPerPixel[format] * (bufw * h + extraw)) / 8;

	u32 bytes = Memory::ValidSize(texaddr, sizeInRAM);
	if (Memory::IsValidAddress(texaddr)) {
		FlushRegisters();

		CommandType type = CommandType((int)CommandType::TEXTURE0 + level);
		const u8 *p = Memory::GetPointerUnchecked(texaddr);

		// Dumps are huge - let's try to find this already emitted.
		for (u32 prevptr : lastTextures) {
			if (pushbuf.size() < prevptr + bytes) {
				continue;
			}

			if (memcmp(pushbuf.data() + prevptr, p, bytes) == 0) {
				commands.push_back({type, bytes, prevptr});
				// Okay, that was easy.  Bail out.
				return;
			}
		}

		// Not there, gotta emit anew.
		Command cmd = EmitCommandWithRAM(type, p, bytes);
		lastTextures.push_back(cmd.ptr);
	}
}

static void FlushPrimState(int vcount) {
	// TODO: Eventually, how do we handle texturing from framebuf/zbuf?
	// TODO: Do we need to preload color/depth/stencil (in case from last frame)?

	// We re-flush textures always in case the game changed them... kinda expensive.
	// TODO: Dirty textures on transfer/stall/etc. somehow?
	// TODO: Or maybe de-dup by validating if it has changed?
	for (int level = 0; level < 8; ++level) {
		u32 texaddr = gstate.getTextureAddress(level);
		if (texaddr) {
			EmitTextureData(level, texaddr);
		}
	}

	const void *verts = Memory::GetPointer(gstate_c.vertexAddr);
	const void *indices = nullptr;
	if ((gstate.vertType & GE_VTYPE_IDX_MASK) != GE_VTYPE_IDX_NONE) {
		indices = Memory::GetPointer(gstate_c.indexAddr);
	}

	u32 ibytes = 0;
	u32 vbytes = 0;
	GetVertDataSizes(vcount, indices, vbytes, ibytes);

	if (indices) {
		EmitCommandWithRAM(CommandType::INDICES, indices, ibytes);
	}
	if (verts) {
		EmitCommandWithRAM(CommandType::VERTICES, verts, vbytes);
	}
}

static void EmitTransfer(u32 op) {
	FlushRegisters();

	// This may not make a lot of sense right now, unless it's to a framebuf...
	if (!Memory::IsVRAMAddress(gstate.getTransferDstAddress())) {
		// Skip, not VRAM, so can't affect drawing (we flush textures each prim.)
		return;
	}

	u32 srcBasePtr = gstate.getTransferSrcAddress();
	u32 srcStride = gstate.getTransferSrcStride();
	int srcX = gstate.getTransferSrcX();
	int srcY = gstate.getTransferSrcY();
	int width = gstate.getTransferWidth();
	int height = gstate.getTransferHeight();
	int bpp = gstate.getTransferBpp();

	u32 srcBytes = ((srcY + height - 1) * srcStride + (srcX + width)) * bpp;
	srcBytes = Memory::ValidSize(srcBasePtr, srcBytes);

	EmitCommandWithRAM(CommandType::TRANSFERSRC, Memory::GetPointerUnchecked(srcBasePtr), srcBytes);

	lastRegisters.push_back(op);
}

static void EmitClut(u32 op) {
	u32 addr = gstate.getClutAddress();
	u32 bytes = gstate.getClutLoadBytes();
	bytes = Memory::ValidSize(addr, bytes);

	EmitCommandWithRAM(CommandType::CLUT, Memory::GetPointerUnchecked(addr), bytes);

	lastRegisters.push_back(op);
}

static void EmitPrim(u32 op) {
	FlushPrimState(op & 0x0000FFFF);

	lastRegisters.push_back(op);
}

static void EmitBezierSpline(u32 op) {
	int ucount = op & 0xFF;
	int vcount = (op >> 8) & 0xFF;
	FlushPrimState(ucount * vcount);

	lastRegisters.push_back(op);
}

bool IsActive() {
	return active;
}

void Activate() {
	nextFrame = true;
}

void NotifyCommand(u32 pc) {
	if (!active) {
		return;
	}
	if (writePending) {
		WriteRecording();
		commands.clear();
		pushbuf.clear();

		writePending = false;
		// We're done - this was just to write the result out.
		NOTICE_LOG(SYSTEM, "Recording finished");
		active = false;
		return;
	}

	const u32 op = Memory::Read_U32(pc);
	const GECommand cmd = GECommand(op >> 24);

	switch (cmd) {
	case GE_CMD_VADDR:
	case GE_CMD_IADDR:
	case GE_CMD_JUMP:
	case GE_CMD_CALL:
	case GE_CMD_RET:
	case GE_CMD_END:
	case GE_CMD_SIGNAL:
	case GE_CMD_FINISH:
	case GE_CMD_BASE:
	case GE_CMD_OFFSETADDR:
	case GE_CMD_ORIGIN:
		// These just prepare future commands, and are flushed with those commands.
		// TODO: Maybe add a command just to log that these were hit?
		break;

	case GE_CMD_BOUNDINGBOX:
	case GE_CMD_BJUMP:
		// Since we record each command, this is theoretically not relevant.
		// TODO: Output a CommandType to validate this.
		break;

	case GE_CMD_PRIM:
		EmitPrim(op);
		break;

	case GE_CMD_BEZIER:
	case GE_CMD_SPLINE:
		EmitBezierSpline(op);
		break;

	case GE_CMD_LOADCLUT:
		EmitClut(op);
		break;

	case GE_CMD_TRANSFERSTART:
		EmitTransfer(op);
		break;

	default:
		lastRegisters.push_back(op);
		break;
	}
}

void NotifyMemcpy(u32 dest, u32 src, u32 sz) {
	if (!active) {
		return;
	}
	if (Memory::IsVRAMAddress(dest)) {
		FlushRegisters();
		Command cmd{CommandType::MEMCPYDEST, sizeof(dest), (u32)pushbuf.size()};
		pushbuf.resize(pushbuf.size() + sizeof(dest));
		memcpy(pushbuf.data() + cmd.ptr, &dest, sizeof(dest));

		sz = Memory::ValidSize(dest, sz);
		EmitCommandWithRAM(CommandType::MEMCPYDATA, Memory::GetPointer(dest), sz);
	}
}

void NotifyMemset(u32 dest, int v, u32 sz) {
	if (!active) {
		return;
	}
	struct MemsetCommand {
		u32 dest;
		int value;
		u32 sz;
	};

	if (Memory::IsVRAMAddress(dest)) {
		sz = Memory::ValidSize(dest, sz);
		MemsetCommand data{dest, v, sz};

		FlushRegisters();
		Command cmd{CommandType::MEMSET, sizeof(data), (u32)pushbuf.size()};
		pushbuf.resize(pushbuf.size() + sizeof(data));
		memcpy(pushbuf.data() + cmd.ptr, &data, sizeof(data));
	}
}

void NotifyUpload(u32 dest, u32 sz) {
	if (!active) {
		return;
	}
	NotifyMemcpy(dest, dest, sz);
}

void NotifyFrame() {
	if (active && !writePending) {
		// Delay write until the first command of the next frame, so we get the right display buf.
		NOTICE_LOG(SYSTEM, "Recording complete - waiting to get display buffer");
		writePending = true;
	}
	if (nextFrame) {
		NOTICE_LOG(SYSTEM, "Recording starting...");
		active = true;
		nextFrame = false;
		lastTextures.clear();
		BeginRecording();
	}
}

static bool ExecuteSubmitCmds(void *p, u32 sz) {
	// TODO: Smarter memory allocation.
	// Over allocate for the finish/end, and include execListQueue.
	u32 pendingSize = (int)execListQueue.size() * sizeof(u32);
	u32 allocSize = pendingSize + sz + 8;
	u32 ptr = userMemory.Alloc(allocSize);
	if (ptr == -1 || ptr == 0) {
		ERROR_LOG(SYSTEM, "Unable to allocate for registers");
		return false;
	}

	Memory::MemcpyUnchecked(ptr, execListQueue.data(), pendingSize);
	Memory::MemcpyUnchecked(ptr + pendingSize, p, sz);
	Memory::Write_U32(GE_CMD_FINISH << 24, ptr + pendingSize + sz + 0);
	Memory::Write_U32(GE_CMD_END << 24, ptr + pendingSize + sz + 4);
	auto optParam = PSPPointer<PspGeListArgs>::Create(0);
	gpu->EnableInterrupts(false);
	u32 listID = gpu->EnqueueList(ptr, 0, -1, optParam, false);
	currentMIPS->downcount -= gpu->GetListTicks(listID) - CoreTiming::GetTicks();
	gpu->ListSync(listID, 0);
	gpu->EnableInterrupts(true);
	userMemory.Free(ptr);

	execListQueue.clear();

	return true;
}

static void FreePSPPointer(u32 &p) {
	if (p) {
		userMemory.Free(p);
		p = 0;
	}
}

static bool AllocatePSPBuf(u32 &pspPointer, u32 bufptr, u32 sz) {
	// TODO: Smarter... but slabs don't work, because of alignment issues.  Arg.
	FreePSPPointer(pspPointer);

	u32 allocSize = sz;
	pspPointer = userMemory.Alloc(allocSize);
	if (pspPointer == -1 || pspPointer == 0) {
		return false;
	}

	Memory::MemcpyUnchecked(pspPointer, pushbuf.data() + bufptr, sz);
	return true;
}

static void ExecuteInit(u32 ptr, u32 sz) {
	gstate.Restore((u32_le *)(pushbuf.data() + ptr));
	gpu->ReapplyGfxState();
}

static void ExecuteRegisters(u32 ptr, u32 sz) {
	ExecuteSubmitCmds(pushbuf.data() + ptr, sz);
}

static void ExecuteVertices(u32 ptr, u32 sz) {
	if (!AllocatePSPBuf(execVertPtr, ptr, sz)) {
		ERROR_LOG(SYSTEM, "Unable to allocate for vertices");
		return;
	}

	execListQueue.push_back((GE_CMD_BASE << 24) | ((execVertPtr >> 8) & 0x00FF0000));
	execListQueue.push_back((GE_CMD_VADDR << 24) | (execVertPtr & 0x00FFFFFF));
}

static void ExecuteIndices(u32 ptr, u32 sz) {
	if (!AllocatePSPBuf(execIndPtr, ptr, sz)) {
		ERROR_LOG(SYSTEM, "Unable to allocate for indices");
		return;
	}

	execListQueue.push_back((GE_CMD_BASE << 24) | ((execIndPtr >> 8) & 0x00FF0000));
	execListQueue.push_back((GE_CMD_IADDR << 24) | (execIndPtr & 0x00FFFFFF));
}

static void ExecuteClut(u32 ptr, u32 sz) {
	if (!AllocatePSPBuf(execClutPtr, ptr, sz)) {
		ERROR_LOG(SYSTEM, "Unable to allocate for clut");
		return;
	}

	execListQueue.push_back((GE_CMD_CLUTADDRUPPER << 24) | ((execClutPtr >> 8) & 0x00FF0000));
	execListQueue.push_back((GE_CMD_CLUTADDR << 24) | (execClutPtr & 0x00FFFFFF));
}

static void ExecuteTransferSrc(u32 ptr, u32 sz) {
	if (!AllocatePSPBuf(execTransferPtr, ptr, sz)) {
		ERROR_LOG(SYSTEM, "Unable to allocate for transfer");
		return;
	}

	execListQueue.push_back((gstate.transfersrcw & 0xFF00FFFF) | ((execTransferPtr >> 8) & 0x00FF0000));
	execListQueue.push_back(((GE_CMD_TRANSFERSRC) << 24) | (execTransferPtr & 0x00FFFFFF));
}

static void ExecuteMemset(u32 ptr, u32 sz) {
	struct MemsetCommand {
		u32 dest;
		int value;
		u32 sz;
	};

	const MemsetCommand *data = (const MemsetCommand *)(pushbuf.data() + ptr);

	if (Memory::IsVRAMAddress(data->dest)) {
		gpu->PerformMemorySet(data->dest, (u8)data->value, data->sz);
	}
}

static void ExecuteMemcpyDest(u32 ptr, u32 sz) {
	execMemcpyDest = *(const u32 *)(pushbuf.data() + ptr);
}

static void ExecuteMemcpy(u32 ptr, u32 sz) {
	if (Memory::IsVRAMAddress(execMemcpyDest)) {
		Memory::MemcpyUnchecked(execMemcpyDest, pushbuf.data() + ptr, sz);
		gpu->PerformMemoryUpload(execMemcpyDest, sz);
	}
}

static void ExecuteTexture(int level, u32 ptr, u32 sz) {
	if (!AllocatePSPBuf(execTexturePtrs[level], ptr, sz)) {
		ERROR_LOG(SYSTEM, "Unable to allocate for texture");
		return;
	}

	u32 pspPointer = execTexturePtrs[level];
	execListQueue.push_back((gstate.texbufwidth[level] & 0xFF00FFFF) | ((pspPointer >> 8) & 0x00FF0000));
	execListQueue.push_back(((GE_CMD_TEXADDR0 + level) << 24) | (pspPointer & 0x00FFFFFF));
}

static void ExecuteDisplay(u32 ptr, u32 sz) {
	struct DisplayBufData {
		PSPPointer<u8> topaddr;
		u32 linesize, pixelFormat;
	};

	DisplayBufData *disp = (DisplayBufData *)(pushbuf.data() + ptr);

	__DisplaySetFramebuf(disp->topaddr.ptr, disp->linesize, disp->pixelFormat, 1);
	__DisplaySetFramebuf(disp->topaddr.ptr, disp->linesize, disp->pixelFormat, 0);
}

static void ExecuteFree() {
	FreePSPPointer(execIndPtr);
	FreePSPPointer(execVertPtr);
	FreePSPPointer(execClutPtr);
	FreePSPPointer(execTransferPtr);
	execMemcpyDest = 0;
	for (int level = 0; level < 8; ++level) {
		FreePSPPointer(execTexturePtrs[level]);
	}

	commands.clear();
	pushbuf.clear();
}

static bool ExecuteCommands() {
	for (const Command &cmd : commands) {
		switch (cmd.type) {
		case CommandType::INIT:
			ExecuteInit(cmd.ptr, cmd.sz);
			break;

		case CommandType::REGISTERS:
			ExecuteRegisters(cmd.ptr, cmd.sz);
			break;

		case CommandType::VERTICES:
			ExecuteVertices(cmd.ptr, cmd.sz);
			break;

		case CommandType::INDICES:
			ExecuteIndices(cmd.ptr, cmd.sz);
			break;

		case CommandType::CLUT:
			ExecuteClut(cmd.ptr, cmd.sz);
			break;

		case CommandType::TRANSFERSRC:
			ExecuteTransferSrc(cmd.ptr, cmd.sz);
			break;

		case CommandType::MEMSET:
			ExecuteMemset(cmd.ptr, cmd.sz);
			break;

		case CommandType::MEMCPYDEST:
			ExecuteMemcpyDest(cmd.ptr, cmd.sz);
			break;

		case CommandType::MEMCPYDATA:
			ExecuteMemcpy(cmd.ptr, cmd.sz);
			break;

		case CommandType::TEXTURE0:
		case CommandType::TEXTURE1:
		case CommandType::TEXTURE2:
		case CommandType::TEXTURE3:
		case CommandType::TEXTURE4:
		case CommandType::TEXTURE5:
		case CommandType::TEXTURE6:
		case CommandType::TEXTURE7:
			ExecuteTexture((int)cmd.type - (int)CommandType::TEXTURE0, cmd.ptr, cmd.sz);
			break;

		case CommandType::DISPLAY:
			ExecuteDisplay(cmd.ptr, cmd.sz);
			break;

		default:
			ERROR_LOG(SYSTEM, "Unsupported GE dump command: %d", cmd.type);
			return false;
		}
	}

	return true;
}

bool RunMountedReplay() {
	_assert_msg_(SYSTEM, !active && !nextFrame, "Cannot run replay while recording.");

	u32 fp = pspFileSystem.OpenFile("disc0:/data.ppdmp", FILEACCESS_READ);
	u8 header[8]{};
	int version = 0;
	pspFileSystem.ReadFile(fp, header, sizeof(header));
	pspFileSystem.ReadFile(fp, (u8 *)&version, sizeof(version));

	if (memcmp(header, HEADER, sizeof(HEADER)) != 0 || version != VERSION) {
		ERROR_LOG(SYSTEM, "Invalid GE dump or unsupported version");
		return false;
	}

	int sz = 0;
	pspFileSystem.ReadFile(fp, (u8 *)&sz, sizeof(sz));
	int bufsz = 0;
	pspFileSystem.ReadFile(fp, (u8 *)&bufsz, sizeof(bufsz));

	commands.resize(sz);
	pushbuf.resize(bufsz);

	bool truncated = false;
	if (pspFileSystem.ReadFile(fp, (u8 *)commands.data(), sizeof(Command) * sz) != sizeof(Command) * sz) {
		truncated = true;
	}
	if (pspFileSystem.ReadFile(fp, pushbuf.data(), bufsz) != bufsz) {
		truncated = true;
	}

	pspFileSystem.CloseFile(fp);

	if (truncated) {
		ERROR_LOG(SYSTEM, "Truncated GE dump");
		ExecuteFree();
		return false;
	}

	bool success = ExecuteCommands();
	ExecuteFree();
	return success;
}

};
