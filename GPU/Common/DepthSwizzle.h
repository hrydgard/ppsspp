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

#include <cstdint>

// Where the GE stores a depth buffer in VRAM (ppsspp-re geprobe exp8-exp10, docs/ge-precision-and-depth-layout.md).
// The depth pixel at linear VRAM offset L (zbuf + (y * stride + x) * 2) is stored at Stored(L): with a 32-bit
// color buffer, address bits 5 to log2(T) - 1 rotate left by one (bits 9 and 10 swap at T 0), then
// (T << 3) | 0x40 is XORed in (0x600 at T 0). T is sceGeEdramSetAddrTranslation's value: 0, 0x200, 0x400,
// 0x800 or 0x1000. The offsets are absolute, independent of the buffer's address and stride.
//
// Bits 0-4 never change, so each 32-byte aligned run of 16 pixels is stored together. The CPU (and texturing)
// sees the same mapping through VRAM's mirrors: an access at 0x04200000 + L goes to Stored(L) of the 16-bit
// layout, at 0x04600000 + L to the 32-bit one's (exp88).
struct DepthLayout {
	uint32_t rotMask = 0;
	int rotShift = 0;
	uint32_t xorBits = 0;

	uint32_t Stored(uint32_t offset) const {
		const uint32_t f = offset & rotMask;
		return ((offset & ~rotMask) | (((f << 1) | (f >> rotShift)) & rotMask)) ^ xorBits;
	}
};

inline DepthLayout GetDepthLayout(uint32_t translation, bool color32) {
	DepthLayout layout;
	if (translation == 0) {
		layout.xorBits = 0x600;
		if (color32) {
			layout.rotMask = 0x600;
			layout.rotShift = 1;
		}
		return layout;
	}
	layout.xorBits = (translation << 3) | 0x40;
	int top = 0;
	while ((2u << top) <= translation)
		top++;
	// Bits 5 to top - 1.
	if (color32 && top - 5 > 1) {
		layout.rotMask = ((1u << (top - 5)) - 1) << 5;
		layout.rotShift = top - 5 - 1;
	}
	return layout;
}
