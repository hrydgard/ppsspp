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

#include "Common/Serialize/SerializeFuncs.h"
#include "Common/Serialize/SerializeMap.h"
#include "Core/HW/BufferQueue.h"

void BufferQueue::DoState(PointerWrap &p) {
	auto s = p.Section("BufferQueue", 0, 2);

	Do(p, bufQueueSize);
	Do(p, start);
	Do(p, end);
	if (bufQueue) {
		DoArray(p, bufQueue, bufQueueSize);
	}

	if (s >= 1) {
		Do(p, ptsMarks);
	} else {
		ptsMarks.clear();
	}
	if (s >= 2) {
		Do(p, filled);
	} else {
		filled = calcQueueSize();
	}
}
