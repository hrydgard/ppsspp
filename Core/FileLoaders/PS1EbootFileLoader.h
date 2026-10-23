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

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "Common/CommonTypes.h"
#include "Core/Loaders.h"

// A PS1 disc image (a cue sheet, a CloneCD .ccd, an Alcohol .mds, a CHD, or a bare .bin/.img/.iso) presented as a converted
// PS1 EBOOT.PBP, the kind popstation makes, for the firmware's POPS to run. Only the headers are kept
// in memory. The disc image is stored uncompressed, as stored DEFLATE blocks, so a read of it goes
// straight to the image's files. See docs/POPS.md.
class PS1EbootFileLoader : public FileLoader {
public:
	explicit PS1EbootFileLoader(const Path &path);
	~PS1EbootFileLoader();

	bool IsValid() const { return error_.empty(); }

	bool Exists() override { return IsValid(); }
	bool IsDirectory() override { return false; }
	s64 FileSize() override { return size_; }
	Path GetPath() const override { return path_; }
	std::string GetFileExtension() const override { return ".pbp"; }
	size_t ReadAt(s64 absolutePos, size_t bytes, size_t count, void *data, Flags flags = Flags::NONE) override;
	using FileLoader::ReadAt;
	void Cancel() override;
	std::string LatestError() const override { return error_; }

	const std::string &DiscID() const { return discID_; }
	const std::string &Title() const { return title_; }

private:
	struct Track {
		int number;
		bool audio;
		u32 start;  // Disc LBA of INDEX 01.
		u32 pregap;  // Sectors before it that belong to the track (INDEX 00, or a generated PREGAP).
	};
	// A run of disc sectors, from a file, the CHD (CHD_SOURCE, offset in frames), or (GENERATED) all zeroes.
	struct Extent {
		u32 lba;
		u32 count;
		int file;
		s64 offset;
		int sectorSize;  // 2352 raw, 2336 without sync and header, 2048 user data only.
		bool byteSwapped;  // CD audio in a CHD is big-endian.
		int stride;  // Bytes from one sector to the next in a file, more than sectorSize with subchannel data.
	};
	static const int GENERATED = -1;
	static const int CHD_SOURCE = -2;

	bool ParseCue(const Path &cuePath, const std::string &text);
	bool ParseCcd(const Path &ccdPath, const std::string &text);
	bool OpenBare(const Path &path);
	bool OpenCHD(const Path &path);
	bool ParseMds(const Path &mdsPath, const std::string &data);
	int AddFile(const Path &path);
	void AddExtent(u32 lba, u32 count, int file, s64 offset, int sectorSize, bool byteSwapped = false, int stride = 0);

	bool ReadSourceSector(const Extent &e, u32 index, u8 *out);
	bool ReadRawSectors(u32 lba, u32 count, u8 *out);
	bool ReadUserData(u32 lba, u8 *out);
	void ReadDiscID();
	void BuildHeaders();

	Path path_;
	std::string error_;
	std::vector<std::unique_ptr<FileLoader>> files_;
	std::vector<Extent> extents_;
	std::vector<Track> tracks_;
	u32 numSectors_ = 0;

	// A CHD, read a hunk (several frames) at a time.
	struct _chd_file *chd_ = nullptr;
	std::mutex chdLock_;
	std::vector<u8> hunk_;
	u32 cachedHunk_ = 0xFFFFFFFF;
	u32 framesPerHunk_ = 0;

	std::string discID_;
	std::string title_;

	// The PBP up to the PSAR's block index, then the index and the blocks, which are made on the fly.
	std::vector<u8> head_;
	u32 psarOffset_ = 0;
	u32 numBlocks_ = 0;
	s64 size_ = 0;
};

namespace File {
struct FileInfo;
}

// Whether a .cue, .ccd or .mds describes a disc, by its contents.
bool IsPS1DiscSheet(FileLoader *fileLoader);

// Whether a CHD is of a PS1 disc, by its volume descriptor. A PSP disc's can be a CD's too, as chdman
// createcd makes them.
bool IsPS1Chd(FileLoader *fileLoader);

// The file names a cue sheet's FILE lines give.
std::vector<std::string> CueSheetFileNames(const Path &cuePath);
std::vector<std::string> CueSheetFileNamesFromText(std::string_view text);

// A file in an archive: its path there, and its size.
struct ArchiveEntry {
	std::string name;
	s64 size;
};

// The files of a PS1 disc among an archive's, by their paths in it: a cue sheet and the files it names,
// a .ccd with its .img (and .sub), an .mds with its .mdf, or else the largest .bin or .img that's a whole
// number of sectors.
// readText reads a small file from the archive.
bool FindPS1DiscFiles(const std::vector<ArchiveEntry> &entries, const std::function<bool(const std::string &name, std::string *text)> &readText, std::vector<std::string> *discFiles);

// The cue sheet next to a disc image whose FILE lines name it, or an empty path.
Path FindCueSheetFor(const Path &imagePath);

// Removes the .bin and .img files from a directory listing that a cue sheet or .ccd in it describes, so
// that each game shows up once, and those that can't be a raw disc image (a size that isn't a whole
// number of sectors).
void RemoveDescribedDiscImages(std::vector<File::FileInfo> *files);
