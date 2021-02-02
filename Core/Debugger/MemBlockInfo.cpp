// Copyright (c) 2021- PPSSPP Project.

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

#include "Core/Debugger/Breakpoints.h"
#include "Core/Debugger/MemBlockInfo.h"
#include "Core/MIPS/MIPS.h"

void NotifyMemInfo(MemBlockFlags flags, uint32_t start, uint32_t size, const std::string &tag) {
	NotifyMemInfoPC(flags, start, size, currentMIPS->pc, tag);
}

void NotifyMemInfoPC(MemBlockFlags flags, uint32_t start, uint32_t size, uint32_t pc, const std::string &tag) {
	// TODO

	if (flags & MemBlockFlags::WRITE) {
		CBreakPoints::ExecMemCheck(start, true, size, pc, tag);
	} else if (flags & MemBlockFlags::READ) {
		CBreakPoints::ExecMemCheck(start, false, size, pc, tag);
	}
}
