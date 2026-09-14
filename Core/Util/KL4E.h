// Copyright (c) 2026- PPSSPP Project.

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

#include <cstddef>
#include "Common/CommonTypes.h"

// KL4E / KL3E - Sony's LZ77-with-arithmetic-coding scheme for compressed PSP executables, the
// alternative to gzip inside a ~PSP module. KL3E is the same bitstream with one constant changed.
//
// See docs/KL4E.md for the format.

// Decompresses one KL4E/KL3E stream. `in` points at the five-byte stream header, i.e. *past* the
// four-byte "KL4E"/"KL3E" magic. `inSize` bounds reads so a truncated file can't run off the end.
//
// Returns the number of bytes written, or a negative PSP error code:
//   SCE_KERNEL_ERROR_INVALID_SIZE   (0x80000104) output didn't fit
//   SCE_KERNEL_ERROR_INVALID_FORMAT (0x80000108) stream is malformed
//
// If `end` is non-null it receives a pointer just past the last input byte consumed. (The PSP's
// own routine reports the last byte consumed for stored streams and one past for compressed ones;
// this is deliberately consistent instead, since nothing in PPSSPP depends on the quirk.)
int DecompressKL4E(u8 *out, int outSize, const u8 *in, size_t inSize, const u8 **end, bool isKL3E);

// True if the buffer starts with a "KL4E" or "KL3E" magic. Sets *isKL3E when it does.
bool IsKL4EMagic(const u8 *data, size_t size, bool *isKL3E);
