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

#include <algorithm>
#include <cstring>
#include <map>
#include <set>

#include "libchdr/cdrom.h"
#include "libchdr/chd.h"

#include "Common/File/DirListing.h"
#include "Common/Log.h"
#include "Common/StringUtils.h"
#include "Common/Swap.h"
#include "Core/ELF/ParamSFO.h"
#include "Core/FileLoaders/PS1EbootFileLoader.h"
#include "Core/FileSystems/BlockDevices.h"

static const u32 SECTOR_SIZE = 2352;
// The PSAR's disc image is in blocks of 16 sectors, each stored as one uncompressed DEFLATE block: a
// header byte (final, stored), the length, its complement, then the sectors. POPS doesn't take an
// uncompressed block in its own format (0x9300 bytes), but its decoder call goes to an inflate for a
// converted PBP (__PopsDeflateDecompress), and this way every block has the same size.
static const u32 BLOCK_SECTORS = 16;
static const u32 BLOCK_DATA_SIZE = BLOCK_SECTORS * SECTOR_SIZE;
static const u8 STORED_HEADER[5] = { 0x01, BLOCK_DATA_SIZE & 0xFF, BLOCK_DATA_SIZE >> 8, (u8)~BLOCK_DATA_SIZE, (u8)(~BLOCK_DATA_SIZE >> 8) };
static const u32 BLOCK_SIZE = BLOCK_DATA_SIZE + sizeof(STORED_HEADER);

// The PSISOIMG header, as popstation writes it: the disc ID at 0x400, the TOC at 0x800, the block index
// (32 bytes a block) from 0x4000, and the blocks from 0x100000.
static const u32 PSAR_HEADER_SIZE = 0x4000;
static const u32 PSAR_INDEX_ENTRY_SIZE = 32;
static const u32 PSAR_DATA_OFFSET = 0x100000;
static const u32 MAX_BLOCKS = (PSAR_DATA_OFFSET - PSAR_HEADER_SIZE) / PSAR_INDEX_ENTRY_SIZE;

static const u8 SYNC[12] = { 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00 };

static std::string AsciiToUpper(std::string_view s) {
	std::string out(s);
	for (char &c : out) {
		c = (char)toupper((unsigned char)c);
	}
	return out;
}

static std::string AsciiToLower(std::string_view s) {
	std::string out(s);
	for (char &c : out) {
		c = (char)tolower((unsigned char)c);
	}
	return out;
}

// The file name without its extension.
static std::string FileStem(const Path &path) {
	const std::string name = path.GetFilename();
	return name.substr(0, name.size() - path.GetFileExtension().size());
}

static u8 ToBCD(u32 v) {
	return (u8)(((v / 10) << 4) | (v % 10));
}

// The absolute time of an LBA (which starts at 00:02:00), in BCD.
static void LBAToMSF(s32 lba, u8 *out) {
	const u32 frames = (u32)(lba + 150);
	out[0] = ToBCD(frames / (75 * 60));
	out[1] = ToBCD((frames / 75) % 60);
	out[2] = ToBCD(frames % 75);
}

// "mm:ss:ff" to a frame count, -1 if it isn't one.
static int ParseMSF(std::string_view s) {
	int m, sec, f;
	char c1, c2;
	if (sscanf(std::string(s).c_str(), "%d%c%d%c%d", &m, &c1, &sec, &c2, &f) != 5 || c1 != ':' || c2 != ':') {
		return -1;
	}
	return (m * 60 + sec) * 75 + f;
}

static bool ReadSmallTextFile(const Path &path, std::string *text) {
	std::unique_ptr<FileLoader> loader(ConstructFileLoader(path));
	if (!loader || !loader->Exists()) {
		return false;
	}
	const s64 size = loader->FileSize();
	if (size <= 0 || size > 64 * 1024) {
		return false;
	}
	text->resize((size_t)size);
	text->resize(loader->ReadAt(0, (size_t)size, &(*text)[0]));
	if (startsWith(*text, "\xEF\xBB\xBF")) {
		text->erase(0, 3);
	}
	return true;
}

// The next token of a cue sheet line: a quoted string, or up to the next space.
static std::string_view NextToken(std::string_view *line) {
	*line = StripSpaces(*line);
	if (line->empty()) {
		return {};
	}
	size_t end;
	std::string_view token;
	if ((*line)[0] == '"') {
		end = line->find('"', 1);
		if (end == std::string_view::npos) {
			end = line->size();
		}
		token = line->substr(1, end - 1);
		end = std::min(end + 1, line->size());
	} else {
		end = line->find_first_of(" \t");
		if (end == std::string_view::npos) {
			end = line->size();
		}
		token = line->substr(0, end);
	}
	line->remove_prefix(end);
	return token;
}

bool IsPS1DiscSheet(FileLoader *fileLoader) {
	char buf[4096]{};
	const size_t size = fileLoader->ReadAt(0, sizeof(buf) - 1, buf);
	const std::string text(buf, size);
	const std::string ext = fileLoader->GetFileExtension();
	if (ext == ".ccd") {
		return text.find("[CloneCD]") != std::string::npos;
	}
	if (ext == ".mds") {
		return startsWith(text, "MEDIA DESCRIPTOR");
	}
	if (ext == ".cue") {
		const std::string upper = AsciiToUpper(text);
		return upper.find("FILE") != std::string::npos && upper.find("TRACK") != std::string::npos;
	}
	return false;
}

std::vector<std::string> CueSheetFileNames(const Path &cuePath) {
	std::string text;
	if (!ReadSmallTextFile(cuePath, &text)) {
		return {};
	}
	return CueSheetFileNamesFromText(text);
}

std::vector<std::string> CueSheetFileNamesFromText(std::string_view text) {
	std::vector<std::string> names;
	std::vector<std::string_view> lines;
	SplitString(text, '\n', lines);
	for (std::string_view line : lines) {
		if (equalsNoCase(NextToken(&line), "FILE")) {
			names.emplace_back(NextToken(&line));
		}
	}
	return names;
}

Path FindCueSheetFor(const Path &imagePath) {
	if (!imagePath.CanNavigateUp()) {
		return Path();
	}
	const std::string name = imagePath.GetFilename();
	std::vector<File::FileInfo> cues;
	File::GetFilesInDir(imagePath.NavigateUp(), &cues, "cue:");
	for (const File::FileInfo &cue : cues) {
		if (cue.isDirectory) {
			continue;
		}
		for (const std::string &file : CueSheetFileNames(cue.fullName)) {
			if (equalsNoCase(Path(file).GetFilename(), name)) {
				return cue.fullName;
			}
		}
	}
	return Path();
}

void RemoveDescribedDiscImages(std::vector<File::FileInfo> *files) {
	std::set<std::string> described;
	for (const File::FileInfo &info : *files) {
		const std::string ext = info.fullName.GetFileExtension();
		if (info.isDirectory || (ext != ".cue" && ext != ".ccd")) {
			continue;
		}
		const std::string stem = AsciiToLower(FileStem(info.fullName));
		described.insert(stem + ".bin");
		described.insert(stem + ".img");
		if (ext == ".cue") {
			for (const std::string &file : CueSheetFileNames(info.fullName)) {
				described.insert(AsciiToLower(Path(file).GetFilename()));
			}
		}
	}
	files->erase(std::remove_if(files->begin(), files->end(), [&](const File::FileInfo &info) {
		const std::string ext = info.fullName.GetFileExtension();
		if (info.isDirectory || (ext != ".bin" && ext != ".img")) {
			return false;
		}
		return info.size % SECTOR_SIZE != 0 || described.count(AsciiToLower(info.name)) != 0;
	}), files->end());
}

bool FindPS1DiscFiles(const std::vector<ArchiveEntry> &entries, const std::function<bool(const std::string &name, std::string *text)> &readText, std::vector<std::string> *discFiles) {
	std::map<std::string, std::string> byLowerName;
	for (const ArchiveEntry &entry : entries) {
		byLowerName[AsciiToLower(ReplaceAll(entry.name, "\\", "/"))] = entry.name;
	}
	auto find = [&](const std::string &name, std::string *found) {
		auto it = byLowerName.find(AsciiToLower(ReplaceAll(name, "\\", "/")));
		if (it == byLowerName.end()) {
			return false;
		}
		*found = it->second;
		return true;
	};
	auto extension = [](const std::string &name) {
		const size_t dot = name.find_last_of("./");
		return dot != std::string::npos && name[dot] == '.' ? AsciiToLower(name.substr(dot)) : std::string();
	};

	for (const ArchiveEntry &entry : entries) {
		const std::string ext = extension(entry.name);
		const std::string stem = entry.name.substr(0, entry.name.size() - ext.size());
		const size_t slash = entry.name.find_last_of("/\\");
		const std::string dir = slash == std::string::npos ? "" : entry.name.substr(0, slash + 1);
		std::vector<std::string> files = { entry.name };
		std::string found;
		if (ext == ".cue") {
			std::string text;
			if (!readText(entry.name, &text)) {
				continue;
			}
			const std::vector<std::string> names = CueSheetFileNamesFromText(text);
			bool all = !names.empty();
			for (const std::string &name : names) {
				if (find(dir + name, &found) || (names.size() == 1 && (find(stem + ".bin", &found) || find(stem + ".img", &found)))) {
					files.push_back(found);
				} else {
					all = false;
				}
			}
			if (!all) {
				continue;
			}
		} else if (ext == ".ccd") {
			if (!find(stem + ".img", &found)) {
				continue;
			}
			files.push_back(found);
			if (find(stem + ".sub", &found)) {
				files.push_back(found);
			}
		} else if (ext == ".mds") {
			if (!find(stem + ".mdf", &found)) {
				continue;
			}
			files.push_back(found);
		} else {
			continue;
		}
		*discFiles = files;
		return true;
	}

	const ArchiveEntry *largest = nullptr;
	for (const ArchiveEntry &entry : entries) {
		const std::string ext = extension(entry.name);
		if ((ext == ".bin" || ext == ".img") && entry.size > 0 && entry.size % SECTOR_SIZE == 0 && (!largest || entry.size > largest->size)) {
			largest = &entry;
		}
	}
	if (largest) {
		*discFiles = { largest->name };
		return true;
	}
	return false;
}

PS1EbootFileLoader::PS1EbootFileLoader(const Path &path) : path_(path) {
	const std::string ext = path.GetFileExtension();
	std::string text;
	bool parsed = false;
	if (ext == ".cue" || ext == ".ccd" || ext == ".mds") {
		if (!ReadSmallTextFile(path, &text)) {
			error_ = "Can't read " + path.GetFilename();
			return;
		}
		if (ext == ".cue") {
			parsed = ParseCue(path, text);
		} else if (ext == ".ccd") {
			parsed = ParseCcd(path, text);
		} else {
			parsed = ParseMds(path, text);
		}
	} else if (ext == ".chd") {
		parsed = OpenCHD(path);
	} else {
		// A bare image with a cue sheet or a .ccd next to it is described by that, which has its audio tracks.
		Path cue = FindCueSheetFor(path);
		if (cue.empty()) {
			cue = path.WithReplacedExtension(".cue");
		}
		const Path ccd = path.WithReplacedExtension(".ccd");
		const Path mds = path.WithReplacedExtension(".mds");
		if (ReadSmallTextFile(cue, &text) && ParseCue(cue, text)) {
			parsed = true;
		} else if (ReadSmallTextFile(ccd, &text) && ParseCcd(ccd, text)) {
			parsed = true;
		} else if (ReadSmallTextFile(mds, &text) && ParseMds(mds, text)) {
			parsed = true;
		} else {
			files_.clear();
			extents_.clear();
			tracks_.clear();
			error_.clear();
			parsed = OpenBare(path);
		}
	}
	if (!parsed || tracks_.empty() || extents_.empty()) {
		if (error_.empty()) {
			error_ = "Not a PS1 disc image: " + path.GetFilename();
		}
		return;
	}

	// LBA 0 is the first track's INDEX 01. An image that has its pregap starts before it.
	const u32 origin = tracks_[0].start;
	if (origin != 0) {
		std::vector<Extent> shifted;
		for (Extent e : extents_) {
			if (e.lba + e.count <= origin) {
				continue;
			}
			if (e.lba < origin) {
				const u32 skip = origin - e.lba;
				e.offset += (s64)skip * e.sectorSize;
				e.count -= skip;
				e.lba = origin;
			}
			e.lba -= origin;
			shifted.push_back(e);
		}
		extents_ = shifted;
		for (Track &t : tracks_) {
			t.start -= origin;
		}
	}
	tracks_[0].pregap = 150;
	numSectors_ = extents_.back().lba + extents_.back().count;
	numBlocks_ = (numSectors_ + BLOCK_SECTORS - 1) / BLOCK_SECTORS;
	if (numBlocks_ > MAX_BLOCKS) {
		error_ = "Disc image too large";
		return;
	}

	ReadDiscID();
	title_ = FileStem(path);
	BuildHeaders();
	INFO_LOG(Log::Loader, "PS1 disc %s: %s, %d tracks, %d sectors", path.c_str(), discID_.c_str(), (int)tracks_.size(), numSectors_);
}

PS1EbootFileLoader::~PS1EbootFileLoader() {
	// It reads through files_.
	if (chd_) {
		chd_close(chd_);
	}
}

int PS1EbootFileLoader::AddFile(const Path &path) {
	FileLoader *loader = ConstructFileLoader(path);
	if (!loader || !loader->Exists()) {
		delete loader;
		return -1;
	}
	files_.emplace_back(loader);
	return (int)files_.size() - 1;
}

void PS1EbootFileLoader::AddExtent(u32 lba, u32 count, int file, s64 offset, int sectorSize, bool byteSwapped, int stride) {
	if (count != 0) {
		extents_.push_back(Extent{ lba, count, file, offset, sectorSize, byteSwapped, stride ? stride : sectorSize });
	}
}

// An Alcohol 120% .mds: a header, sessions (only the first is read) and their track blocks of 0x50 bytes,
// each with an extra block (pregap, length) and the image file's name. A track's length in sectors is
// stored from its offset in the .mdf, from its pregap's start (but never before LBA 0), and anything
// between tracks isn't stored.
bool PS1EbootFileLoader::ParseMds(const Path &mdsPath, const std::string &data) {
	const u8 *d = (const u8 *)data.data();
	const size_t size = data.size();
	auto read32 = [&](size_t offset) -> u32 {
		return offset + 4 <= size ? (d[offset] | (d[offset + 1] << 8) | (d[offset + 2] << 16) | ((u32)d[offset + 3] << 24)) : 0;
	};
	if (size < 0x58 || memcmp(d, "MEDIA DESCRIPTOR", 16) != 0) {
		return false;
	}
	const u32 session = read32(0x50);
	if (session + 0x18 > size) {
		return false;
	}
	const u32 numBlocks = d[session + 0x0A];
	const u32 trackBlocks = read32(session + 0x14);

	int file = -1;
	u32 lba = 0;
	for (u32 i = 0; i < numBlocks; i++) {
		const size_t b = trackBlocks + (size_t)i * 0x50;
		if (b + 0x50 > size) {
			return false;
		}
		const u8 point = d[b + 4];
		if (point == 0 || point > 99) {
			continue;
		}
		const u32 extra = read32(b + 0x0C);
		const int stride = d[b + 0x10] | (d[b + 0x11] << 8);
		const u32 start = read32(b + 0x24);
		const s64 offset = (s64)read32(b + 0x28) | ((s64)read32(b + 0x2C) << 32);
		const u32 pregap = read32(extra);
		const u32 length = read32(extra + 4);
		const int sectorSize = stride >= (int)SECTOR_SIZE ? (int)SECTOR_SIZE : stride;
		if (sectorSize != (int)SECTOR_SIZE && sectorSize != 2336 && sectorSize != 2048) {
			error_ = "Unsupported sector size in " + mdsPath.GetFilename();
			return false;
		}

		if (file < 0) {
			// "*.mdf" is the .mds's own name.
			const u32 nameBlock = read32(b + 0x34);
			const u32 nameOffset = read32(nameBlock);
			std::string name;
			for (u32 p = nameOffset; p < size && d[p] != 0; p += d[nameBlock + 4] ? 2 : 1) {
				name.push_back((char)d[p]);
			}
			const Path mdf = startsWith(name, "*") ? mdsPath.WithReplacedExtension(name.size() > 1 ? name.substr(1) : ".mdf") : mdsPath.NavigateUp() / name;
			file = AddFile(mdf);
			if (file < 0) {
				error_ = "Missing " + mdf.GetFilename();
				return false;
			}
		}

		const u32 first = start > pregap ? start - pregap : 0;
		if (first < lba) {
			return false;
		}
		AddExtent(lba, first - lba, GENERATED, 0, SECTOR_SIZE);
		AddExtent(first, length, file, offset, sectorSize, false, stride);
		lba = first + length;
		tracks_.push_back(Track{ point, (d[b] & 0x0F) == 0x09, start, start - first });
	}
	return !tracks_.empty();
}

static int CHDTrackSectorSize(std::string_view type) {
	if (type == "AUDIO" || type == "MODE1_RAW" || type == "MODE2_RAW") {
		return 2352;
	} else if (type == "MODE2" || type == "MODE2_FORM_MIX") {
		return 2336;
	} else if (type == "MODE1" || type == "MODE2_FORM1") {
		return 2048;
	}
	return 0;
}

// A CD CHD's tracks are in its metadata, one entry each, and its frames (2352 bytes of sector and 96 of
// subcode) are in hunks of several. Each track's frames are padded to a multiple of four, and a pregap is
// in them only if its type starts with V.
//
// NOTE: We can't re-use CHDFileBlockDevice here, because it doesn't support multiple tracks.
bool PS1EbootFileLoader::OpenCHD(const Path &path) {
	const int file = AddFile(path);
	if (file < 0) {
		return false;
	}
	chd_ = OpenCHDThroughLoader(files_[file].get(), &error_);
	if (!chd_) {
		return false;
	}
	const chd_header *header = chd_get_header(chd_);
	if (header->unitbytes != CD_FRAME_SIZE || header->hunkbytes % CD_FRAME_SIZE != 0) {
		error_ = "Not a CD image: " + path.GetFilename();
		return false;
	}
	framesPerHunk_ = header->hunkbytes / CD_FRAME_SIZE;
	hunk_.resize(header->hunkbytes);

	u32 lba = 0;
	u32 frame = 0;
	for (u32 i = 0; i < 99; i++) {
		char meta[256]{};
		uint32_t len = 0;
		int number = 0, frames = 0, pregap = 0, postgap = 0;
		char type[32]{}, subtype[32]{}, pgtype[32]{}, pgsub[32]{};
		if (chd_get_metadata(chd_, CDROM_TRACK_METADATA2_TAG, i, meta, sizeof(meta) - 1, &len, nullptr, nullptr) == CHDERR_NONE) {
			if (sscanf(meta, "TRACK:%d TYPE:%31s SUBTYPE:%31s FRAMES:%d PREGAP:%d PGTYPE:%31s PGSUB:%31s POSTGAP:%d", &number, type, subtype, &frames, &pregap, pgtype, pgsub, &postgap) != 8) {
				return false;
			}
		} else if (chd_get_metadata(chd_, CDROM_TRACK_METADATA_TAG, i, meta, sizeof(meta) - 1, &len, nullptr, nullptr) == CHDERR_NONE) {
			if (sscanf(meta, "TRACK:%d TYPE:%31s SUBTYPE:%31s FRAMES:%d", &number, type, subtype, &frames) != 4) {
				return false;
			}
		} else {
			break;
		}
		const int sectorSize = CHDTrackSectorSize(type);
		if (sectorSize == 0 || frames < 0 || pregap < 0 || postgap < 0 || pregap > frames + pregap) {
			error_ = std::string("Unsupported track type in CHD: ") + type;
			return false;
		}
		const bool audio = strcmp(type, "AUDIO") == 0;
		const bool pregapStored = pgtype[0] == 'V';
		if (!pregapStored) {
			AddExtent(lba, pregap, GENERATED, 0, SECTOR_SIZE);
			lba += pregap;
		}
		AddExtent(lba, frames, CHD_SOURCE, frame, sectorSize, audio);
		tracks_.push_back(Track{ number, audio, pregapStored ? lba + pregap : lba, (u32)pregap });
		lba += frames;
		AddExtent(lba, postgap, GENERATED, 0, SECTOR_SIZE);
		lba += postgap;
		frame += (frames + CD_TRACK_PADDING - 1) / CD_TRACK_PADDING * CD_TRACK_PADDING;
	}
	if (tracks_.empty()) {
		error_ = "No tracks in CHD: " + path.GetFilename();
		return false;
	}
	return true;
}

bool IsPS1Chd(FileLoader *fileLoader) {
	std::string error;
	chd_file *chd = OpenCHDThroughLoader(fileLoader, &error);
	if (!chd) {
		return false;
	}
	const chd_header *header = chd_get_header(chd);
	bool ps1 = false;
	if (header->unitbytes == CD_FRAME_SIZE && header->hunkbytes % CD_FRAME_SIZE == 0) {
		// The volume descriptor, in sector 16 of the first track, after its pregap if that's stored.
		u32 frame = 16;
		char meta[256]{};
		int number = 0, frames = 0, pregap = 0;
		char type[32]{}, subtype[32]{}, pgtype[32]{};
		if (chd_get_metadata(chd, CDROM_TRACK_METADATA2_TAG, 0, meta, sizeof(meta) - 1, nullptr, nullptr, nullptr) == CHDERR_NONE &&
			sscanf(meta, "TRACK:%d TYPE:%31s SUBTYPE:%31s FRAMES:%d PREGAP:%d PGTYPE:%31s", &number, type, subtype, &frames, &pregap, pgtype) == 6 && pgtype[0] == 'V') {
			frame += pregap;
		}
		const u32 framesPerHunk = header->hunkbytes / CD_FRAME_SIZE;
		std::vector<u8> hunk(header->hunkbytes);
		if (chd_read(chd, frame / framesPerHunk, hunk.data()) == CHDERR_NONE) {
			const u8 *sector = hunk.data() + (frame % framesPerHunk) * CD_FRAME_SIZE;
			const u8 *pvd = sector + (sector[15] == 1 ? 16 : 24);
			// Every PS1 disc's system ID is PLAYSTATION. A PSP disc's is PSP GAME.
			ps1 = memcmp(pvd, "\x01" "CD001", 6) == 0 && memcmp(pvd + 8, "PLAYSTATION", 11) == 0;
		}
	}
	chd_close(chd);
	return ps1;
}

bool PS1EbootFileLoader::ParseCue(const Path &cuePath, const std::string &text) {
	struct CueTrack {
		int number;
		bool audio;
		int index0 = -1;
		int index1 = -1;
		int pregap = 0;
	};
	struct CueFile {
		Path path;
		int sectorSize = 0;
		std::vector<CueTrack> tracks;
	};
	std::vector<CueFile> cueFiles;

	std::vector<std::string_view> lines;
	SplitString(text, '\n', lines);
	for (std::string_view line : lines) {
		const std::string command = AsciiToUpper(NextToken(&line));
		if (command == "FILE") {
			const std::string_view name = NextToken(&line);
			const std::string type = AsciiToUpper(NextToken(&line));
			if (type != "BINARY" && type != "MOTOROLA") {
				error_ = "Unsupported file type in cue sheet: " + type;
				return false;
			}
			CueFile file;
			file.path = cuePath.NavigateUp() / name;
			cueFiles.push_back(file);
		} else if (command == "TRACK") {
			if (cueFiles.empty()) {
				return false;
			}
			const int number = atoi(std::string(NextToken(&line)).c_str());
			const std::string mode = AsciiToUpper(NextToken(&line));
			int sectorSize;
			if (mode == "AUDIO" || mode == "MODE1/2352" || mode == "MODE2/2352") {
				sectorSize = 2352;
			} else if (mode == "MODE2/2336") {
				sectorSize = 2336;
			} else if (mode == "MODE1/2048" || mode == "MODE2/2048") {
				sectorSize = 2048;
			} else {
				error_ = "Unsupported track mode in cue sheet: " + mode;
				return false;
			}
			CueFile &file = cueFiles.back();
			if (file.sectorSize == 0) {
				file.sectorSize = sectorSize;
			} else if (file.sectorSize != sectorSize) {
				error_ = "Tracks with different sector sizes in one file";
				return false;
			}
			CueTrack track;
			track.number = number;
			track.audio = mode == "AUDIO";
			file.tracks.push_back(track);
		} else if (command == "INDEX" || command == "PREGAP") {
			if (cueFiles.empty() || cueFiles.back().tracks.empty()) {
				return false;
			}
			CueTrack &track = cueFiles.back().tracks.back();
			const int index = command == "INDEX" ? atoi(std::string(NextToken(&line)).c_str()) : -1;
			const int frames = ParseMSF(NextToken(&line));
			if (frames < 0) {
				return false;
			}
			if (index == -1) {
				track.pregap = frames;
			} else if (index == 0) {
				track.index0 = frames;
			} else if (index == 1) {
				track.index1 = frames;
			}
		}
	}

	// Lay the files out one after another, with any generated pregap before its track.
	u32 lba = 0;
	for (CueFile &cueFile : cueFiles) {
		int file = AddFile(cueFile.path);
		if (file < 0) {
			// Renamed images are common: try the cue sheet's own name.
			const std::string ext = cueFile.path.GetFileExtension();
			for (const char *alt : { ext.c_str(), ".bin", ".img" }) {
				file = AddFile(cuePath.WithReplacedExtension(alt));
				if (file >= 0) {
					break;
				}
			}
			if (file < 0 || cueFiles.size() != 1) {
				error_ = "Missing file in cue sheet: " + cueFile.path.GetFilename();
				return false;
			}
		}
		if (cueFile.tracks.empty()) {
			continue;
		}
		const u32 fileSectors = (u32)(files_[file]->FileSize() / cueFile.sectorSize);
		u32 fileSector = 0;
		for (const CueTrack &cueTrack : cueFile.tracks) {
			if (cueTrack.index1 < 0) {
				error_ = "Track without INDEX 01 in cue sheet";
				return false;
			}
			const u32 split = (u32)(cueTrack.index0 >= 0 ? cueTrack.index0 : cueTrack.index1);
			if (split < fileSector || (u32)cueTrack.index1 < split) {
				return false;
			}
			AddExtent(lba, split - fileSector, file, (s64)fileSector * cueFile.sectorSize, cueFile.sectorSize);
			lba += split - fileSector;
			AddExtent(lba, cueTrack.pregap, GENERATED, 0, SECTOR_SIZE);
			lba += cueTrack.pregap;
			fileSector = split;

			Track track;
			track.number = cueTrack.number;
			track.audio = cueTrack.audio;
			track.start = lba + (cueTrack.index1 - split);
			track.pregap = cueTrack.pregap + (cueTrack.index1 - split);
			tracks_.push_back(track);
		}
		if (fileSectors > fileSector) {
			AddExtent(lba, fileSectors - fileSector, file, (s64)fileSector * cueFile.sectorSize, cueFile.sectorSize);
			lba += fileSectors - fileSector;
		}
	}
	return !tracks_.empty();
}

bool PS1EbootFileLoader::ParseCcd(const Path &ccdPath, const std::string &text) {
	const int file = AddFile(ccdPath.WithReplacedExtension(".img"));
	if (file < 0) {
		error_ = "Missing " + ccdPath.WithReplacedExtension(".img").GetFilename();
		return false;
	}
	AddExtent(0, (u32)(files_[file]->FileSize() / SECTOR_SIZE), file, 0, SECTOR_SIZE);

	// [TRACK n] sections: MODE=0 is audio, INDEX 1= is where the track starts, INDEX 0= its pregap.
	std::vector<std::string_view> lines;
	SplitString(text, '\n', lines);
	Track *track = nullptr;
	int index0 = -1;
	auto finishTrack = [&]() {
		if (track && index0 >= 0 && (u32)index0 <= track->start) {
			track->pregap = track->start - index0;
		}
		index0 = -1;
	};
	for (std::string_view line : lines) {
		line = StripSpaces(line);
		if (startsWith(line, "[")) {
			finishTrack();
			track = nullptr;
			if (startsWithNoCase(line, "[TRACK ")) {
				tracks_.push_back(Track{ atoi(std::string(line.substr(7)).c_str()), false, 0, 0 });
				track = &tracks_.back();
			}
			continue;
		}
		std::string_view key, value;
		if (!track || !SplitStringOnce(line, &key, &value, '=')) {
			continue;
		}
		key = StripSpaces(key);
		const int v = atoi(std::string(StripSpaces(value)).c_str());
		if (equalsNoCase(key, "MODE")) {
			track->audio = v == 0;
		} else if (equalsNoCase(key, "INDEX 1")) {
			track->start = v;
		} else if (equalsNoCase(key, "INDEX 0")) {
			index0 = v;
		}
	}
	finishTrack();
	std::sort(tracks_.begin(), tracks_.end(), [](const Track &a, const Track &b) { return a.number < b.number; });
	if (tracks_.empty()) {
		// No tracks listed: one data track.
		tracks_.push_back(Track{ 1, false, 0, 150 });
	}
	return true;
}

bool PS1EbootFileLoader::OpenBare(const Path &path) {
	const int file = AddFile(path);
	if (file < 0) {
		return false;
	}
	FileLoader *loader = files_[file].get();
	const s64 size = loader->FileSize();
	u8 sync[12]{};
	u8 cd001[6]{};
	loader->ReadAt(0, sizeof(sync), sync);
	loader->ReadAt(16 * 2048, sizeof(cd001), cd001);
	int sectorSize;
	if (size % SECTOR_SIZE == 0 && memcmp(sync, SYNC, sizeof(SYNC)) == 0) {
		sectorSize = SECTOR_SIZE;
	} else if (size % 2048 == 0 && memcmp(cd001, "\x01" "CD001", 6) == 0) {
		sectorSize = 2048;
	} else {
		return false;
	}
	AddExtent(0, (u32)(size / sectorSize), file, 0, sectorSize);
	tracks_.push_back(Track{ 1, false, 0, 150 });
	return true;
}

bool PS1EbootFileLoader::ReadRawSectors(u32 lba, u32 count, u8 *out) {
	bool ok = true;
	while (count > 0) {
		auto it = std::upper_bound(extents_.begin(), extents_.end(), lba, [](u32 lba, const Extent &e) { return lba < e.lba; });
		if (lba >= numSectors_ || it == extents_.begin()) {
			memset(out, 0, (size_t)count * SECTOR_SIZE);
			return ok;
		}
		const Extent &e = *(it - 1);
		const u32 run = std::min(count, e.lba + e.count - lba);
		const u32 index = lba - e.lba;
		if (e.file == GENERATED) {
			memset(out, 0, (size_t)run * SECTOR_SIZE);
		} else if (e.file >= 0 && e.sectorSize == (int)SECTOR_SIZE && e.stride == (int)SECTOR_SIZE) {
			const size_t want = (size_t)run * SECTOR_SIZE;
			const size_t got = files_[e.file]->ReadAt(e.offset + (s64)index * SECTOR_SIZE, want, out);
			if (got < want) {
				memset(out + got, 0, want - got);
				ok = false;
			}
		} else if (e.file >= 0 && e.sectorSize == (int)SECTOR_SIZE) {
			// Raw sectors with subchannel data after each: one read, then without it.
			std::vector<u8> buf((size_t)run * e.stride);
			const size_t got = files_[e.file]->ReadAt(e.offset + (s64)index * e.stride, buf.size(), buf.data());
			if (got < buf.size()) {
				memset(buf.data() + got, 0, buf.size() - got);
				ok = false;
			}
			for (u32 i = 0; i < run; i++) {
				memcpy(out + (size_t)i * SECTOR_SIZE, buf.data() + (size_t)i * e.stride, SECTOR_SIZE);
			}
		} else {
			// One at a time. Sectors without their sync and header (2336), or only their user data (2048,
			// which can only be Mode 2 Form 1), get them made. The EDC and ECC of those stay zero.
			for (u32 i = 0; i < run; i++) {
				u8 *sector = out + (size_t)i * SECTOR_SIZE;
				memset(sector, 0, SECTOR_SIZE);
				u8 *data = sector;
				if (e.sectorSize != (int)SECTOR_SIZE) {
					memcpy(sector, SYNC, sizeof(SYNC));
					LBAToMSF(lba + i, sector + 12);
					sector[15] = 2;
					data = sector + 16;
					if (e.sectorSize == 2048) {
						static const u8 subheader[8] = { 0, 0, 8, 0, 0, 0, 8, 0 };
						memcpy(sector + 16, subheader, sizeof(subheader));
						data = sector + 24;
					}
				}
				ok = ReadSourceSector(e, index + i, data) && ok;
			}
		}
		lba += run;
		count -= run;
		out += (size_t)run * SECTOR_SIZE;
	}
	return ok;
}

// One sector's stored bytes (e.sectorSize of them) from an extent's file or the CHD.
bool PS1EbootFileLoader::ReadSourceSector(const Extent &e, u32 index, u8 *out) {
	if (e.file >= 0) {
		return files_[e.file]->ReadAt(e.offset + (s64)index * e.stride, e.sectorSize, out) == (size_t)e.sectorSize;
	}
	const u32 frame = (u32)e.offset + index;
	const u32 hunk = frame / framesPerHunk_;
	std::lock_guard<std::mutex> guard(chdLock_);
	if (hunk != cachedHunk_) {
		if (chd_read(chd_, hunk, hunk_.data()) != CHDERR_NONE) {
			cachedHunk_ = 0xFFFFFFFF;
			return false;
		}
		cachedHunk_ = hunk;
	}
	const u8 *src = hunk_.data() + (size_t)(frame % framesPerHunk_) * CD_FRAME_SIZE;
	if (e.byteSwapped) {
		for (int i = 0; i + 1 < e.sectorSize; i += 2) {
			out[i] = src[i + 1];
			out[i + 1] = src[i];
		}
	} else {
		memcpy(out, src, e.sectorSize);
	}
	return true;
}

bool PS1EbootFileLoader::ReadUserData(u32 lba, u8 *out) {
	u8 sector[SECTOR_SIZE];
	if (!ReadRawSectors(lba, 1, sector)) {
		return false;
	}
	memcpy(out, sector + (sector[15] == 1 ? 16 : 24), 2048);
	return true;
}

// The ID the disc boots by, from SYSTEM.CNF's BOOT line ("cdrom:\SLUS_007.13;1" is SLUS00713).
void PS1EbootFileLoader::ReadDiscID() {
	discID_ = "SLUS00000";
	u8 pvd[2048];
	if (!ReadUserData(16, pvd) || memcmp(pvd, "\x01" "CD001", 6) != 0) {
		return;
	}
	const u32 rootLBA = pvd[156 + 2] | (pvd[156 + 3] << 8) | (pvd[156 + 4] << 16) | ((u32)pvd[156 + 5] << 24);
	const u32 rootSize = pvd[156 + 10] | (pvd[156 + 11] << 8) | (pvd[156 + 12] << 16) | ((u32)pvd[156 + 13] << 24);
	std::string cnf;
	for (u32 s = 0; s < std::min(rootSize / 2048, 16U) && cnf.empty(); s++) {
		u8 dir[2048];
		if (!ReadUserData(rootLBA + s, dir)) {
			return;
		}
		for (u32 pos = 0; pos + 33 < sizeof(dir) && dir[pos] != 0; pos += dir[pos]) {
			const u8 nameLen = dir[pos + 32];
			if (pos + 33 + nameLen > sizeof(dir)) {
				break;
			}
			std::string_view name((const char *)&dir[pos + 33], nameLen);
			name = name.substr(0, name.find(';'));
			if (!equalsNoCase(name, "SYSTEM.CNF")) {
				continue;
			}
			const u32 lba = dir[pos + 2] | (dir[pos + 3] << 8) | (dir[pos + 4] << 16) | ((u32)dir[pos + 5] << 24);
			const u32 size = std::min(dir[pos + 10] | (dir[pos + 11] << 8) | (dir[pos + 12] << 16) | ((u32)dir[pos + 13] << 24), 2048U);
			u8 data[2048];
			if (ReadUserData(lba, data)) {
				cnf.assign((const char *)data, size);
			}
			break;
		}
	}

	std::vector<std::string_view> lines;
	SplitString(cnf, '\n', lines);
	for (std::string_view line : lines) {
		std::string_view key, value;
		if (!SplitStringOnce(line, &key, &value, '=') || !equalsNoCase(StripSpaces(key), "BOOT")) {
			continue;
		}
		value = StripSpaces(value);
		const size_t slash = value.find_last_of("\\:/");
		if (slash != std::string_view::npos) {
			value = value.substr(slash + 1);
		}
		value = value.substr(0, value.find(';'));
		std::string id;
		for (char c : value) {
			if (isalnum((unsigned char)c)) {
				id.push_back((char)toupper((unsigned char)c));
			}
		}
		if (id.size() == 9) {
			discID_ = id;
		}
		break;
	}
}

void PS1EbootFileLoader::BuildHeaders() {
	ParamSFOData sfo;
	sfo.SetValue("BOOTABLE", 1, 4);
	sfo.SetValue("CATEGORY", "ME", 4);
	sfo.SetValue("DISC_ID", discID_, 16);
	sfo.SetValue("DISC_VERSION", "1.00", 8);
	sfo.SetValue("PARENTAL_LEVEL", 1, 4);
	sfo.SetValue("PSP_SYSTEM_VER", "3.01", 8);
	sfo.SetValue("REGION", 0x8000, 4);
	sfo.SetValue("TITLE", title_, 128);
	u8 *sfoData = nullptr;
	size_t sfoSize = 0;
	sfo.WriteSFO(&sfoData, &sfoSize);

	// PARAM.SFO, then no icons, pictures, sound or DATA.PSP.
	const u32 sfoEnd = 0x28 + (u32)sfoSize;
	psarOffset_ = (sfoEnd + 0xFFFF) & ~0xFFFF;
	head_.assign(psarOffset_ + PSAR_HEADER_SIZE, 0);
	u8 *pbp = head_.data();
	memcpy(pbp, "\0PBP", 4);
	u32_le *words = (u32_le *)pbp;
	words[1] = 0x00010000;
	words[2] = 0x28;
	for (int i = 3; i < 9; i++) {
		words[i] = sfoEnd;
	}
	words[9] = psarOffset_;
	memcpy(pbp + 0x28, sfoData, sfoSize);
	delete[] sfoData;

	u8 *psar = pbp + psarOffset_;
	auto write32 = [&](u32 offset, u32 value) {
		*(u32_le *)(psar + offset) = value;
	};
	const u32 end = PSAR_DATA_OFFSET + numBlocks_ * BLOCK_SIZE;
	memcpy(psar, "PSISOIMG0000", 12);
	write32(0x0C, end);
	const std::string gameID = "_" + discID_.substr(0, 4) + "_" + discID_.substr(4);
	memcpy(psar + 0x400, gameID.data(), std::min(gameID.size(), (size_t)0x20));

	// The TOC, as Q subchannel entries of 10 bytes (control/ADR, TNO, POINT, the absolute time, zero, then
	// POINT's): the first track (and the disc type, 0x20 being XA), the last one, the lead-out, then each
	// track with its pregap's start and its own.
	const Track &first = tracks_.front();
	const Track &last = tracks_.back();
	auto control = [](const Track &t) -> u8 { return t.audio ? 0x01 : 0x41; };
	u8 *toc = psar + 0x800;
	toc[0] = control(first); toc[2] = 0xA0; toc[7] = ToBCD(first.number); toc[8] = 0x20;
	toc += 10;
	toc[0] = control(last); toc[2] = 0xA1; toc[7] = ToBCD(last.number);
	toc += 10;
	toc[0] = control(last); toc[2] = 0xA2; LBAToMSF(numSectors_, toc + 7);
	toc += 10;
	for (const Track &t : tracks_) {
		if (toc + 10 > psar + 0xBFC) {
			break;
		}
		toc[0] = control(t);
		toc[2] = ToBCD(t.number);
		LBAToMSF((s32)t.start - (s32)t.pregap, toc + 3);
		LBAToMSF(t.start, toc + 7);
		toc += 10;
	}
	write32(0xBFC, PSAR_DATA_OFFSET);
	write32(0x1220, end + 0x2D31);
	write32(0x1228, 0x7FF);
	memcpy(psar + 0x122C, title_.data(), std::min(title_.size(), (size_t)0x7F));
	write32(0x12AC, 3);

	size_ = (s64)psarOffset_ + end;
}

size_t PS1EbootFileLoader::ReadAt(s64 absolutePos, size_t bytes, size_t count, void *data, Flags flags) {
	if (!IsValid() || absolutePos < 0 || bytes == 0) {
		return 0;
	}
	u8 *out = (u8 *)data;
	size_t remaining = (size_t)std::min((s64)(bytes * count), std::max(size_ - absolutePos, (s64)0));
	const size_t total = remaining;
	s64 pos = absolutePos;
	const s64 indexStart = (s64)head_.size();
	const s64 dataStart = (s64)psarOffset_ + PSAR_DATA_OFFSET;
	while (remaining > 0) {
		size_t n;
		if (pos < indexStart) {
			n = std::min(remaining, (size_t)(indexStart - pos));
			memcpy(out, head_.data() + pos, n);
		} else if (pos < dataStart) {
			const u32 entry = (u32)((pos - indexStart) / PSAR_INDEX_ENTRY_SIZE);
			const u32 within = (u32)((pos - indexStart) % PSAR_INDEX_ENTRY_SIZE);
			u8 buf[PSAR_INDEX_ENTRY_SIZE]{};
			if (entry < numBlocks_) {
				*(u32_le *)buf = entry * BLOCK_SIZE;
				*(u16_le *)(buf + 4) = (u16)BLOCK_SIZE;
			}
			n = std::min(remaining, (size_t)(PSAR_INDEX_ENTRY_SIZE - within));
			memcpy(out, buf + within, n);
		} else {
			const u32 block = (u32)((pos - dataStart) / BLOCK_SIZE);
			const u32 within = (u32)((pos - dataStart) % BLOCK_SIZE);
			if (within < sizeof(STORED_HEADER)) {
				n = std::min(remaining, sizeof(STORED_HEADER) - within);
				memcpy(out, STORED_HEADER + within, n);
			} else {
				const u32 offset = within - sizeof(STORED_HEADER);
				n = std::min(remaining, (size_t)(BLOCK_DATA_SIZE - offset));
				const u32 lba = block * BLOCK_SECTORS + offset / SECTOR_SIZE;
				if (offset % SECTOR_SIZE == 0 && n % SECTOR_SIZE == 0) {
					ReadRawSectors(lba, (u32)(n / SECTOR_SIZE), out);
				} else {
					const u32 first = offset % SECTOR_SIZE;
					const u32 sectors = (u32)((first + n + SECTOR_SIZE - 1) / SECTOR_SIZE);
					std::vector<u8> buf((size_t)sectors * SECTOR_SIZE);
					ReadRawSectors(lba, sectors, buf.data());
					memcpy(out, buf.data() + first, n);
				}
			}
		}
		out += n;
		pos += n;
		remaining -= n;
	}
	return total / bytes;
}

void PS1EbootFileLoader::Cancel() {
	for (auto &file : files_) {
		file->Cancel();
	}
}
