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

#pragma once

#include "../GPUInterface.h"

class ShaderManager;

class GLES_GPU : public GPUInterface
{
public:
	virtual u32 EnqueueList(u32 listpc, u32 stall);
	virtual void UpdateStall(int listid, u32 newstall);
	virtual void ExecuteOp(u32 op, u32 diff);
	virtual bool InterpretList();
	virtual void DrawSync(int mode);
private:
	bool ProcessDLQueue();
};
