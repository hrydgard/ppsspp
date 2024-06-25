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

#include <map>
#include <cstdint>
#include <cstring>
#include "Common/Log.h"
#include "Common/Serialize/Serializer.h"

struct BufferQueue {
	BufferQueue(int size = 0x20000) {
		alloc(size);
	}

	~BufferQueue() {
		delete [] bufQueue;
	}

	bool alloc(int size) {
		_assert_(size > 0);
		delete [] bufQueue;
		bufQueue = new unsigned char[size];
		bufQueueSize = size;
		clear();
		return true;
	}

	void clear() {
		start = 0;
		end = 0;
		filled = 0;
	}

	inline int getQueueSize() {
		return filled;
	}

	inline int getRemainSize() {
		return bufQueueSize - getQueueSize();
	}

	bool push(const unsigned char *buf, int addsize, s64 pts = 0) {
		int space = getRemainSize();
		if (space < addsize || addsize < 0)
			return false;
		savePts(pts);
		if (end + addsize <= bufQueueSize) {
			// If end is before start, there's enough space.  Otherwise, we're nearing the queue size.
			memcpy(bufQueue + end, buf, addsize);
			end += addsize;
			if (end == bufQueueSize)
				end = 0;
		} else {
			// Time to wrap end.  Fill what remains, then fill before start.
			_assert_(end >= start);
			int firstSize = bufQueueSize - end;
			memcpy(bufQueue + end, buf, firstSize);
			memcpy(bufQueue, buf + firstSize, addsize - firstSize);
			end = addsize - firstSize;
		}
		filled += addsize;
		verifyQueueSize();
		return true;
	}

	int pop_front(unsigned char *buf, int wantedsize, s64 *pts = nullptr) {
		if (wantedsize <= 0)
			return 0;
		int bytesgot = getQueueSize();
		if (wantedsize < bytesgot)
			bytesgot = wantedsize;
		if (pts != nullptr) {
			*pts = findPts(bytesgot);
		}

		int firstSize = bufQueueSize - start;
		if (buf) {
			if (bytesgot <= firstSize) {
				memcpy(buf, bufQueue + start, bytesgot);
			} else {
				memcpy(buf, bufQueue + start, firstSize);
				memcpy(buf + firstSize, bufQueue, bytesgot - firstSize);
			}
		}
		if (bytesgot <= firstSize)
			start += bytesgot;
		else
			start = bytesgot - firstSize;
		if (start == bufQueueSize)
			start = 0;
		filled -= bytesgot;
		verifyQueueSize();
		return bytesgot;
	}

	int get_front(unsigned char *buf, int wantedsize) {
		if (wantedsize <= 0)
			return 0;
		int bytesgot = getQueueSize();
		if (wantedsize < bytesgot)
			bytesgot = wantedsize;
		int firstSize = bufQueueSize - start;
		if (bytesgot <= firstSize) {
			memcpy(buf, bufQueue + start, bytesgot);
		} else {
			memcpy(buf, bufQueue + start, firstSize);
			memcpy(buf + firstSize, bufQueue, bytesgot - firstSize);
		}
		return bytesgot;
	}

	void DoState(PointerWrap &p);

private:
	void savePts(u64 pts) {
		if (pts != 0) {
			ptsMarks[end] = pts;
		}
	}

	u64 findPts(std::map<u32, s64>::iterator earliest, std::map<u32, s64>::iterator latest) {
		u64 pts = 0;
		// Take the first one, that is the pts of this packet.
		if (earliest != latest) {
			pts = earliest->second;
		}
		ptsMarks.erase(earliest, latest);
		return pts;
	}

	u64 findPts(int packetSize) {
		auto earliest = ptsMarks.lower_bound(start);
		auto latest = ptsMarks.lower_bound(start + packetSize);

		u64 pts = findPts(earliest, latest);

		// If it wraps around, we have to look at the other half too.
		if (start + packetSize > bufQueueSize) {
			earliest = ptsMarks.begin();
			latest = ptsMarks.lower_bound(start + packetSize - bufQueueSize);
			// This also clears the range, so we always call on wrap.
			u64 latePts = findPts(earliest, latest);
			if (pts == 0)
				pts = latePts;
		}

		return pts;
	}

	inline int calcQueueSize() {
		if (end < start) {
			return bufQueueSize + end - start;
		}
		return end - start;
	}

	inline void verifyQueueSize() {
		_assert_(calcQueueSize() == filled || (end == start && filled == bufQueueSize));
	}

	uint8_t *bufQueue = nullptr;
	// Model: end may be less than start, indicating the space between end and start is free.
	// If end equals start, we're empty.
	int start = 0, end = 0;
	int filled = 0;
	int bufQueueSize = 0;

	std::map<u32, s64> ptsMarks;
};
