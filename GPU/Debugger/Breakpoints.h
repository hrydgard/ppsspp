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

#include <string>
#include <set>
#include <mutex>
#include <unordered_map>
#include "Common/CommonTypes.h"
#include "Common/Math/expression_parser.h"
#include "GPU/Common/GPUDebugInterface.h"

struct GECmdInfo;

class GPUBreakpoints {
public:
	GPUBreakpoints() {
		Init();
	}
	void Init();

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

	bool HasBreakpoints() const {
		return hasBreakpoints_;
	}

	struct BreakpointInfo {
		bool isConditional = false;
		PostfixExpression expression;
		std::string expressionString;
	};

	bool ToggleCmdBreakpoint(const GECmdInfo &info);

private:
	void AddNonTextureTempBreakpoints();
	void CheckForTextureChange(u32 op, u32 addr);
	bool HasAnyBreakpoints() const;
	bool IsTextureCmdBreakpoint(u32 op);
	bool IsRenderTargetCmdBreakpoint(u32 op);
	bool HitAddressBreakpoint(u32 pc, u32 op);
	bool HitOpBreakpoint(u32 op);

	std::mutex breaksLock;

	bool breakCmds[256]{};
	BreakpointInfo breakCmdsInfo[256]{};
	std::unordered_map<u32, BreakpointInfo> breakPCs;
	std::set<u32> breakTextures;
	std::set<u32> breakRenderTargets;
	// Small optimization to avoid a lock/lookup for the common case.
	size_t breakPCsCount = 0;
	size_t breakTexturesCount = 0;
	size_t breakRenderTargetsCount = 0;

	// If these are set, the above are also, but they should be temporary.
	bool breakCmdsTemp[256];
	std::set<u32> breakPCsTemp;
	std::set<u32> breakTexturesTemp;
	std::set<u32> breakRenderTargetsTemp;
	bool textureChangeTemp = false;

	u32 lastTexture = 0xFFFFFFFF;
	std::vector<bool> nonTextureCmds;

	bool hasBreakpoints_ = false;  // cached value of HasAnyBreakpoints().
};
