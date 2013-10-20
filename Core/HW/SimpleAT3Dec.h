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

#include <cmath>

#include "base/basictypes.h"

// Wraps FFMPEG in a nice interface that's drop-in compatible with
// the old one. Decodes packet by packet - does NOT demux. That's done by
// MpegDemux. Only decodes Atrac3+, not regular Atrac3.

// Based on http://ffmpeg.org/doxygen/trunk/doc_2examples_2decoding_encoding_8c-example.html#_a13

// Ideally, Maxim's Atrac3+ decoder would be available as a standalone library
// that we could link, as that would be totally sufficient for the use case here.
// However, it will be maintained as a part of FFMPEG so that's the way we'll go
// for simplicity and sanity.

struct SimpleAT3;

SimpleAT3 *AT3Create();
bool AT3Decode(SimpleAT3 *ctx, void* inbuf, int inbytes, int *outbytes, uint8_t* outbuf);
void AT3Close(SimpleAT3 **ctx);
