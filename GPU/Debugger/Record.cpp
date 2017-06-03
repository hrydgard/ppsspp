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
#include "Core/ELF/ParamSFO.h"
#include "Core/FileSystems/MetaFileSystem.h"
#include "Core/MemMap.h"
#include "Core/System.h"
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

enum class CommandType : u8 {
	REGISTERS = 1,
	VERTICES = 2,
	INDICES = 3,
	CLUT = 4,
	TRANSFERSRC = 5,
	MEMSET = 6,
	MEMCPYDEST = 7,
	MEMCPYDATA = 8,

	TEXTURE0 = 0x10,
	TEXTURE1 = 0x11,
	TEXTURE2 = 0x12,
	TEXTURE3 = 0x13,
	TEXTURE4 = 0x14,
	TEXTURE5 = 0x15,
	TEXTURE6 = 0x16,
	TEXTURE7 = 0x17,
};

struct Command {
	CommandType type;
	u32 sz;
	u32 ptr;
};

static std::vector<u8> pushbuf;
static std::vector<Command> commands;
static std::vector<u32> lastRegisters;
static std::vector<u32> lastTextures;

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

static void WriteRecording() {
	FlushRegisters();

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
        if (nlen == 1 || !memcmp(p + 1, needle + 1, nlen - 1)) {
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
}

static void EmitClut(u32 op) {
	u32 addr = gstate.getClutAddress();
	u32 bytes = gstate.getClutLoadBytes();
	bytes = Memory::ValidSize(addr, bytes);

	EmitCommandWithRAM(CommandType::CLUT, Memory::GetPointerUnchecked(addr), bytes);
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

void NotifyCommand(u32 pc) {
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
	NotifyMemcpy(dest, dest, sz);
}

void NotifyFrame() {
	if (active) {
		active = false;
		WriteRecording();
		commands.clear();
	}
	if (nextFrame) {
		active = true;
		nextFrame = false;
		lastTextures.clear();
	}
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
	for (int i = 0; i < sz; ++i) {
		size_t read = 0;
		read += pspFileSystem.ReadFile(fp, (u8 *)&commands[i].type, sizeof(commands[i].type));
		read += pspFileSystem.ReadFile(fp, (u8 *)&commands[i].sz, sizeof(commands[i].sz));
		read += pspFileSystem.ReadFile(fp, (u8 *)&commands[i].ptr, sizeof(commands[i].ptr));
		if (read != sizeof(commands[i].type) + sizeof(commands[i].sz) + sizeof(commands[i].ptr)) {
			ERROR_LOG(SYSTEM, "Truncated GE dump");
			return false;
		}
	}

	pushbuf.resize(bufsz);
	if (pspFileSystem.ReadFile(fp, pushbuf.data(), bufsz) != bufsz) {
		ERROR_LOG(SYSTEM, "Truncated GE dump");
		return false;
	}

	pspFileSystem.CloseFile(fp);

	// TODO: Execute commands.
	return true;
}

};
