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

#pragma once

#include "Common/CommonTypes.h"

namespace GPUBreakpoints {
	void Init(void (*hasBreakpoints)(bool flag));

	bool IsBreakpoint(u32 pc, u32 op);

	bool IsAddressBreakpoint(u32 addr, bool &temp);
	bool IsAddressBreakpoint(u32 addr);
	bool IsCmdBreakpoint(u8 cmd, bool &temp);
	bool IsCmdBreakpoint(u8 cmd);
	bool IsTextureBreakpoint(u32 addr, bool &temp);
	bool IsTextureBreakpoint(u32 addr);
	bool IsRenderTargetBreakpoint(u32 addr, bool &temp);
	bool IsRenderTargetBreakpoint(u32 addr);

	void AddAddressBreakpoint(u32 addr, bool temp = false);
	void AddCmdBreakpoint(u8 cmd, bool temp = false);
	void AddTextureBreakpoint(u32 addr, bool temp = false);
	void AddTextureChangeTempBreakpoint();
	void AddRenderTargetBreakpoint(u32 addr, bool temp = false);
	// Quick way to trigger GE debugger statically.
	void AddAnyTempBreakpoint();

	void RemoveAddressBreakpoint(u32 addr);
	void RemoveCmdBreakpoint(u8 cmd);
	void RemoveTextureBreakpoint(u32 addr);
	void RemoveTextureChangeTempBreakpoint();
	void RemoveRenderTargetBreakpoint(u32 addr);

	bool SetAddressBreakpointCond(u32 addr, const std::string &expression, std::string *error);
	bool GetAddressBreakpointCond(u32 addr, std::string *expression);
	bool SetCmdBreakpointCond(u8 cmd, const std::string &expression, std::string *error);
	bool GetCmdBreakpointCond(u8 cmd, std::string *expression);

	void UpdateLastTexture(u32 addr);

	void ClearAllBreakpoints();
	void ClearTempBreakpoints();

	bool IsOpBreakpoint(u32 op, bool &temp);

	bool IsOpBreakpoint(u32 op);
};
