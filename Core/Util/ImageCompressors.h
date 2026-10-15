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
#include <functional>
#include <string>
#include <vector>

#include "Common/File/Path.h"

// Optional, runtime-loaded libraries that convert game images, used by the Un/Pack screen.
// PPSSPP doesn't ship or link them: if the user puts one in memstick/PSP/SYSTEM, it's picked
// up at startup.
//  - CHD: CHDmodern (https://github.com/LunaMoo/CHDmodern), ISO <-> DVD CHD.
//  - CSO: maxcso (https://github.com/unknownbrackets/maxcso) built with its C API
//    (capi/maxcso_capi.h), ISO <-> CSO (decompressing ZSO and DAX too).
// Desktop only: Android, iOS and UWP won't load native code from user storage.

enum class ImageFormat {
	CHD = 0,
	CSO = 1,
};

// Return false to cancel. Only CHD conversions stop mid-file, CSO ones run to the end.
typedef std::function<bool(float progress)> ImageProgressFunc;

namespace ImageCompressors {

// Looks for both libraries. Only the first call does anything.
void Init();

bool IsAvailable(ImageFormat format);
// The file name looked for on this platform, like "chdmodern.dll".
const char *LibraryFileName(ImageFormat format);
// Where it was loaded from, empty if it wasn't.
Path LibraryPath(ImageFormat format);
// Version the library reports, empty if it doesn't report one.
std::string Version(ImageFormat format);

// Extensions (with the dot) of the compressed images a format decompresses.
const std::vector<std::string> &CompressedExtensions(ImageFormat format);
// Extension of the images a format compresses to.
const char *OutputExtension(ImageFormat format);

}  // namespace ImageCompressors

namespace MaxCSO {

// Matches maxcso_mode in maxcso_capi.h.
enum class Mode {
	Decompress = 0,
	Fast = 1,
	Standard = 2,
	Maximum = 3,
};

// Blocking. Compresses an ISO to CSO, or decompresses a CSO/ZSO/DAX back to ISO. Never
// overwrites, and deletes a partial output on failure.
bool Process(const Path &input, const Path &output, Mode mode, const ImageProgressFunc &progress, std::string *error);

}  // namespace MaxCSO

namespace CHDModern {

enum class Codecs {
	Default = 0,  // chdman createdvd's: LZMA, zlib, Huffman, FLAC. Smallest.
	Zstd = 1,     // Zstandard only. Bigger, but faster to read while playing.
};

// Blocking. Compresses an ISO into a DVD CHD (unit size 2048) with the given hunk size, or
// writes a DVD CHD back out as an ISO. Never overwrites, and deletes a partial output on
// failure or cancel (*cancelled is set then).
bool CreateDVD(const Path &input, const Path &output, uint32_t hunkBytes, Codecs codecs, const ImageProgressFunc &progress, std::string *error, bool *cancelled);
bool Extract(const Path &input, const Path &output, const ImageProgressFunc &progress, std::string *error, bool *cancelled);

}  // namespace CHDModern
