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

#include "SaveState.h"
#include "MemMap.h"
#include "HLE/sceKernel.h"

// TODO: Need a way to schedule via CoreTiming and actually run then.

bool SaveState::Load(std::string &filename)
{
	SaveState state;
	return CChunkFileReader::Load(filename, REVISION, state);
}

bool SaveState::Save(std::string &filename)
{
	SaveState state;
	return CChunkFileReader::Save(filename, REVISION, state);
}

bool SaveState::Verify()
{
	SaveState state;
	return CChunkFileReader::Verify(state);
}

void SaveState::DoState(PointerWrap &p)
{
	Memory::DoState(p);
	__KernelDoState(p);
}