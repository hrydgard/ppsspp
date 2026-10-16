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
//  - CHD: CHDm, CHDmodern (https://github.com/LunaMoo/CHDmodern) 0.2.0 or newer, ISO <-> DVD CHD.
//  - CSO: mCSO, maxcso (https://github.com/LunaMoo/maxcso) built with its C API version 2
//    (capi/maxcso_capi.h), ISO <-> CSO (decompressing ZSO and DAX too). The Max preset's
//    pre-check needs maxcso_process_ex; without it Max still works, just slower.
//  - PBP: iPBP (https://github.com/LunaMoo/iPBP), ISO <-> PSN-style EBOOT.PBP (NPUMDIMG). The Max
//    preset needs iPBP 1.1.0 or newer.
// Desktop only: Android, iOS and UWP won't load native code from user storage.

enum class ImageFormat {
	CHD = 0,
	CSO = 1,
	PBP = 2,
};

// Return false to cancel.
typedef std::function<bool(float progress)> ImageProgressFunc;

namespace ImageCompressors {

// Looks for both libraries. Only the first call does anything.
void Init();

bool IsAvailable(ImageFormat format);
// The file name looked for on this platform, like "CHDm.dll".
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

// Values are stored in the config (iCSOPreset) and index the menu.
enum class Preset {
	Max = 0,            // Zopfli, skipped on blocks libdeflate can't shrink below 98%; 256K blocks.
	Great = 1,          // libdeflate; 256K blocks. Much faster, slightly bigger than Max.
	PSPCompatible = 2,  // Plain zlib, 2K blocks: what every PSP custom firmware reads.
};

// Blocking. Compresses an ISO to CSO v1, or decompresses a CSO/ZSO/DAX back to ISO. Never
// overwrites, and deletes a partial output on failure or cancel (*cancelled is set then).
bool Compress(const Path &input, const Path &output, Preset preset, const ImageProgressFunc &progress, std::string *error, bool *cancelled);
bool Decompress(const Path &input, const Path &output, const ImageProgressFunc &progress, std::string *error, bool *cancelled);

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

namespace IPBP {

// Matches ipbp_iso2pbp_options.compress (IPBP_COMPRESS_*).
enum class Compression {
	None = 0,  // Blocks stored.
	LZRC = 1,  // Like sign_np: LZRC blocks, the output real PSPs are known to read.
	Max = 2,   // Smallest LZRC (optimal parsing), iPBP 1.1.0+. Still LZRC, but not sign_np's output.
};

// Whether the loaded iPBP has Compression::Max (older versions would quietly use LZRC).
bool SupportsMaxCompression();

// Blocking. Converts an ISO to a PSN-style EBOOT.PBP, or such a PBP back to an ISO. Never
// overwrites; a partial output never survives a failure or cancel (*cancelled is set then).
bool IsoToPbp(const Path &input, const Path &output, Compression compression, const ImageProgressFunc &progress, std::string *error, bool *cancelled);
bool PbpToIso(const Path &input, const Path &output, const ImageProgressFunc &progress, std::string *error, bool *cancelled);

// Checks that a PBP holds a UMD image iPBP can convert back (homebrew and PS1 EBOOTs don't),
// and gets its disc id ("ULUS10041"), possibly empty.
bool ReadDiscId(const Path &pbp, std::string *discId, std::string *error);

}  // namespace IPBP
