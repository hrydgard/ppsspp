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

#include <cstring>
#include "Common/ChunkFile.h"

struct BufferQueue {
	BufferQueue(int size = 0x20000) {
		bufQueue = 0;
		alloc(size);
	}

	~BufferQueue() {
		if (bufQueue)
			delete [] bufQueue;
	}

	bool alloc(int size) {
		if (size < 0)
			return false;
		if (bufQueue)
			delete [] bufQueue;
		bufQueue = new unsigned char[size];
		start = 0;
		end = 0;
		bufQueueSize = size;
		return true;
	}

	void clear() {
		start = 0;
		end = 0;
	}

	inline int getQueueSize() {
		if (end >= start) {
			return end - start;
		} else {
			return bufQueueSize + end - start;
		}
	}

	inline int getRemainSize() {
		return bufQueueSize - getQueueSize();
	}

	bool push(unsigned char *buf, int addsize) {
		int queuesz = getQueueSize();
		int space = bufQueueSize - queuesz;
		if (space < addsize || addsize < 0)
			return false;
		if (end + addsize <= bufQueueSize) {
			memcpy(bufQueue + end, buf, addsize);
			end += addsize;
		} else {
			int firstSize = bufQueueSize - end;
			memcpy(bufQueue + end, buf, firstSize);
			memcpy(bufQueue, buf + firstSize, addsize - firstSize);
			end = addsize - firstSize;
		}
		return true;
	}

	int pop_front(unsigned char *buf, int wantedsize) {
		if (wantedsize <= 0)
			return 0;
		int bytesgot = getQueueSize();
		if (wantedsize < bytesgot)
			bytesgot = wantedsize;
		if (buf) {
			if (start + bytesgot <= bufQueueSize) {
				memcpy(buf, bufQueue + start, bytesgot);
				start += bytesgot;
			} else {
				int firstSize = bufQueueSize - start;
				memcpy(buf, bufQueue + start, firstSize);
				memcpy(buf + firstSize, bufQueue, bytesgot - firstSize);
				start = bytesgot - firstSize;
			}
		} else {
			int firstSize = bufQueueSize - start;
			if (start + bytesgot <= bufQueueSize)
				start += bytesgot;
			else 
				start = bytesgot - firstSize;
		}
		return bytesgot;
	}

	int get_front(unsigned char *buf, int wantedsize) {
		if (wantedsize <= 0)
			return 0;
		int bytesgot = getQueueSize();
		if (wantedsize < bytesgot)
			bytesgot = wantedsize;
		if (start + bytesgot <= bufQueueSize) {
			memcpy(buf, bufQueue + start, bytesgot);
		} else {
			int size = bufQueueSize - start;
			memcpy(buf, bufQueue + start, size);
			memcpy(buf + size, bufQueue, bytesgot - size);
		}
		return bytesgot;
	}

	void DoState(PointerWrap &p) {
		p.Do(bufQueueSize);
		p.Do(start);
		p.Do(end);
		if (bufQueue)
			p.DoArray(bufQueue, bufQueueSize);
	}

	unsigned char* bufQueue;
	int start, end;
	int bufQueueSize;
};
