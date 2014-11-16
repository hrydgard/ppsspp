// Copyright (c) 2013- PPSSPP Project.

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
#include <set>
#include "base/mutex.h"
#include "GPU/Debugger/Breakpoints.h"
#include "GPU/GPUState.h"

namespace GPUBreakpoints {

static recursive_mutex breaksLock;
static std::vector<bool> breakCmds;
static std::set<u32> breakPCs;
static std::set<u32> breakTextures;
// Small optimization to avoid a lock/lookup for the common case.
static size_t breakPCsCount = 0;
static size_t breakTexturesCount = 0;

// If these are set, the above are also, but they should be temporary.
static std::vector<bool> breakCmdsTemp;
static std::set<u32> breakPCsTemp;
static std::set<u32> breakTexturesTemp;
static bool textureChangeTemp = false;

static u32 lastTexture = 0xFFFFFFFF;

// These are commands we run before breaking on a texture.
// They are commands that affect the decoding of the texture.
const static u8 textureRelatedCmds[] = {
	GE_CMD_TEXADDR0, GE_CMD_TEXADDR1, GE_CMD_TEXADDR2, GE_CMD_TEXADDR3, GE_CMD_TEXADDR4, GE_CMD_TEXADDR5, GE_CMD_TEXADDR6, GE_CMD_TEXADDR7,
	GE_CMD_TEXBUFWIDTH0, GE_CMD_TEXBUFWIDTH1, GE_CMD_TEXBUFWIDTH2, GE_CMD_TEXBUFWIDTH3, GE_CMD_TEXBUFWIDTH4, GE_CMD_TEXBUFWIDTH5, GE_CMD_TEXBUFWIDTH6, GE_CMD_TEXBUFWIDTH7,
	GE_CMD_TEXSIZE0, GE_CMD_TEXSIZE1, GE_CMD_TEXSIZE2, GE_CMD_TEXSIZE3, GE_CMD_TEXSIZE4, GE_CMD_TEXSIZE5, GE_CMD_TEXSIZE6, GE_CMD_TEXSIZE7,

	GE_CMD_CLUTADDR, GE_CMD_CLUTADDRUPPER, GE_CMD_LOADCLUT, GE_CMD_CLUTFORMAT,
	GE_CMD_TEXFORMAT, GE_CMD_TEXMODE, GE_CMD_TEXTUREMAPENABLE,
	GE_CMD_TEXFILTER, GE_CMD_TEXWRAP,

	// Sometimes found between clut/texture params.
	GE_CMD_TEXFLUSH, GE_CMD_TEXSYNC,
};
static std::vector<bool> nonTextureCmds;

void Init() {
	ClearAllBreakpoints();

	nonTextureCmds.clear();
	nonTextureCmds.resize(256, true);
	for (size_t i = 0; i < ARRAY_SIZE(textureRelatedCmds); ++i) {
		nonTextureCmds[textureRelatedCmds[i]] = false;
	}
}

void AddNonTextureTempBreakpoints() {
	for (int i = 0; i < 256; ++i) {
		if (nonTextureCmds[i]) {
			AddCmdBreakpoint(i, true);
		}
	}
}

u32 GetAdjustedTextureAddress(u32 op) {
	const u8 cmd = op >> 24;
	bool interesting = (cmd >= GE_CMD_TEXADDR0 && cmd <= GE_CMD_TEXADDR7);
	interesting = interesting || (cmd >= GE_CMD_TEXBUFWIDTH0 && cmd <= GE_CMD_TEXBUFWIDTH7);

	if (!interesting) {
		return (u32)-1;
	}

	int level = cmd <= GE_CMD_TEXADDR7 ? cmd - GE_CMD_TEXADDR0 : cmd - GE_CMD_TEXBUFWIDTH0;
	u32 addr;

	// Okay, so would this op modify the low or high part?
	if (cmd <= GE_CMD_TEXADDR7) {
		addr = (op & 0xFFFFF0) | ((gstate.texbufwidth[level] << 8) & 0x0F000000);
	} else {
		addr = (gstate.texaddr[level] & 0xFFFFF0) | ((op << 8) & 0x0F000000);
	}

	return addr;
}

bool IsTextureChangeBreakpoint(u32 op, u32 addr) {
	if (!textureChangeTemp) {
		return false;
	}

	const u8 cmd = op >> 24;
	bool enabled = gstate.isTextureMapEnabled();

	// Only for level 0.
	if (cmd != GE_CMD_TEXADDR0 && cmd != GE_CMD_TEXBUFWIDTH0) {
		// But we don't break when it's not enabled.
		if (cmd == GE_CMD_TEXTUREMAPENABLE) {
			enabled = (op & 1) != 0;
		} else {
			return false;
		}
	}
	if (enabled && addr != lastTexture) {
		textureChangeTemp = false;
		lastTexture = addr;
		return true;
	} else {
		return false;
	}
}

bool IsTextureCmdBreakpoint(u32 op) {
	const u32 addr = GetAdjustedTextureAddress(op);
	if (addr != (u32)-1) {
		return IsTextureChangeBreakpoint(op, addr) || IsTextureBreakpoint(addr);
	} else {
		return IsTextureChangeBreakpoint(op, gstate.getTextureAddress(0));
	}
}

bool IsBreakpoint(u32 pc, u32 op) {
	if (IsAddressBreakpoint(pc) || IsOpBreakpoint(op)) {
		return true;
	}

	if ((breakTexturesCount != 0 || textureChangeTemp) && IsTextureCmdBreakpoint(op)) {
		// Break on the next non-texture.
		AddNonTextureTempBreakpoints();
	}

	return false;
}

bool IsAddressBreakpoint(u32 addr, bool &temp) {
	if (breakPCsCount == 0) {
		temp = false;
		return false;
	}

	lock_guard guard(breaksLock);
	temp = breakPCsTemp.find(addr) != breakPCsTemp.end();
	return breakPCs.find(addr) != breakPCs.end();
}

bool IsAddressBreakpoint(u32 addr) {
	if (breakPCsCount == 0) {
		return false;
	}

	lock_guard guard(breaksLock);
	return breakPCs.find(addr) != breakPCs.end();
}

bool IsTextureBreakpoint(u32 addr, bool &temp) {
	if (breakTexturesCount == 0) {
		temp = false;
		return false;
	}

	lock_guard guard(breaksLock);
	temp = breakTexturesTemp.find(addr) != breakTexturesTemp.end();
	return breakTextures.find(addr) != breakTextures.end();
}

bool IsTextureBreakpoint(u32 addr) {
	if (breakTexturesCount == 0) {
		return false;
	}

	lock_guard guard(breaksLock);
	return breakTextures.find(addr) != breakTextures.end();
}

bool IsCmdBreakpoint(u8 cmd, bool &temp) {
	temp = breakCmdsTemp[cmd];
	return breakCmds[cmd];
}

bool IsCmdBreakpoint(u8 cmd) {
	return breakCmds[cmd];
}

void AddAddressBreakpoint(u32 addr, bool temp) {
	lock_guard guard(breaksLock);

	if (temp) {
		if (breakPCs.find(addr) == breakPCs.end()) {
			breakPCsTemp.insert(addr);
			breakPCs.insert(addr);
		}
		// Already normal breakpoint, let's not make it temporary.
	} else {
		// Remove the temporary marking.
		breakPCsTemp.erase(addr);
		breakPCs.insert(addr);
	}

	breakPCsCount = breakPCs.size();
}

void AddCmdBreakpoint(u8 cmd, bool temp) {
	if (temp) {
		if (!breakCmds[cmd]) {
			breakCmdsTemp[cmd] = true;
			breakCmds[cmd] = true;
		}
		// Ignore adding a temp breakpoint when a normal one exists.
	} else {
		// This is no longer temporary.
		breakCmdsTemp[cmd] = false;
		breakCmds[cmd] = true;
	}
}

void AddTextureBreakpoint(u32 addr, bool temp) {
	lock_guard guard(breaksLock);

	if (temp) {
		if (breakTextures.find(addr) == breakTextures.end()) {
			breakTexturesTemp.insert(addr);
			breakTextures.insert(addr);
		}
	} else {
		breakTexturesTemp.erase(addr);
		breakTextures.insert(addr);
	}

	breakTexturesCount = breakTextures.size();
}

void AddTextureChangeTempBreakpoint() {
	textureChangeTemp = true;
}

void RemoveAddressBreakpoint(u32 addr) {
	lock_guard guard(breaksLock);

	breakPCsTemp.erase(addr);
	breakPCs.erase(addr);

	breakPCsCount = breakPCs.size();
}

void RemoveCmdBreakpoint(u8 cmd) {
	breakCmdsTemp[cmd] = false;
	breakCmds[cmd] = false;
}

void RemoveTextureBreakpoint(u32 addr) {
	lock_guard guard(breaksLock);

	breakTexturesTemp.erase(addr);
	breakTextures.erase(addr);

	breakTexturesCount = breakTextures.size();
}

void RemoveTextureChangeTempBreakpoint() {
	textureChangeTemp = false;
}

void UpdateLastTexture(u32 addr) {
	lastTexture = addr;
}

void ClearAllBreakpoints() {
	lock_guard guard(breaksLock);

	breakCmds.clear();
	breakCmds.resize(256, false);
	breakPCs.clear();
	breakTextures.clear();

	breakCmdsTemp.clear();
	breakCmdsTemp.resize(256, false);
	breakPCsTemp.clear();
	breakTexturesTemp.clear();

	breakPCsCount = breakPCs.size();
	breakTexturesCount = breakTextures.size();

	textureChangeTemp = false;
}

void ClearTempBreakpoints() {
	lock_guard guard(breaksLock);

	// Reset ones that were temporary back to non-breakpoints in the primary arrays.
	for (int i = 0; i < 256; ++i) {
		if (breakCmdsTemp[i]) {
			breakCmds[i] = false;
			breakCmdsTemp[i] = false;
		}
	}

	for (auto it = breakPCsTemp.begin(), end = breakPCsTemp.end(); it != end; ++it) {
		breakPCs.erase(*it);
	}
	breakPCsTemp.clear();
	breakPCsCount = breakPCs.size();

	for (auto it = breakTexturesTemp.begin(), end = breakTexturesTemp.end(); it != end; ++it) {
		breakTextures.erase(*it);
	}
	breakTexturesTemp.clear();
	breakPCsCount = breakTextures.size();

	textureChangeTemp = false;
}

};
