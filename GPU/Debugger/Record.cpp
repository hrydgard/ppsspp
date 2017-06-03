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

#include <vector>
#include "base/stringutil.h"
#include "Common/Common.h"
#include "Common/FileUtil.h"
#include "Core/ELF/ParamSFO.h"
#include "Core/MemMap.h"
#include "Core/System.h"
#include "GPU/Debugger/Record.h"
#include "GPU/ge_constants.h"

namespace GPURecord {

static const char *HEADER = "PPSSPPGE";
static const int VERSION = 1;

static bool active = false;
static bool nextFrame = false;

enum class CommandType : u8 {
	REGISTERS = 1,
};

struct Command {
	CommandType type;
	std::vector<u8> buf;
};

static std::vector<Command> commands;
static std::vector<u32> lastRegisters;

static void FlushRegisters() {
	if (!lastRegisters.empty()) {
		Command last{CommandType::REGISTERS};
		last.buf.resize(lastRegisters.size() * sizeof(u32));
		memcpy(last.buf.data(), lastRegisters.data(), last.buf.size());
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
	for (int i = 0; i < sz; ++i) {
		const Command &cmd = commands[i];
		fwrite(&cmd.type, sizeof(cmd.type), 1, fp);
		int bufsz = (int)cmd.buf.size();
		fwrite(&bufsz, sizeof(bufsz), 1, fp);
		fwrite(cmd.buf.data(), bufsz, 1, fp);
	}

	fclose(fp);
}

static void FlushPrimState() {
	// TODO: Eventually, how do we handle texturing from framebuf/zbuf?
	// TODO: Do we need to preload color/depth/stencil (in case from last frame)?

	// TODO: Flush textures, always (in case overwritten... hmm expensive.)
	// TODO: Flush vertex data?  Or part of prim?
	// TODO: Flush index data.
}

static void EmitTransfer(u32 op) {
	FlushRegisters();

	// TODO
}

static void EmitClut(u32 op) {
	FlushRegisters();

	// TODO
}

static void EmitPrim(u32 op) {
	FlushPrimState();
	FlushRegisters();

	// TODO
}

static void EmitBezier(u32 op) {
	FlushPrimState();
	FlushRegisters();

	// TODO
}

static void EmitSpline(u32 op) {
	FlushPrimState();
	FlushRegisters();

	// TODO
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
	case GE_CMD_CLUTADDR:
	case GE_CMD_CLUTADDRUPPER:
	case GE_CMD_TRANSFERSRC:
	case GE_CMD_TRANSFERSRCW:
	case GE_CMD_TRANSFERDST:
	case GE_CMD_TRANSFERDSTW:
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
		EmitBezier(op);
		break;

	case GE_CMD_SPLINE:
		EmitSpline(op);
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

void NotifyFrame() {
	if (active) {
		active = false;
		WriteRecording();
		commands.clear();
	}
	if (nextFrame) {
		active = true;
		nextFrame = false;
	}
}

};
