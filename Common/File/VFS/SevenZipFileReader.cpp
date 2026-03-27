#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <set>

#define NOMINMAX  // 7z includes windows.h for some reason.
#include "ext/lzma-sdk/7z.h"
#include "ext/lzma-sdk/7zCrc.h"
#include "ext/lzma-sdk/7zFile.h"
#include "ext/lzma-sdk/Lzma2Dec.h"
#include "ext/lzma-sdk/LzmaDec.h"

#include "Common/Data/Encoding/Utf8.h"
#include "Common/Log.h"
#include "Common/StringUtils.h"
#include "Common/File/VFS/SevenZipFileReader.h"

static constexpr size_t SEVENZIP_LOOKBUF_SIZE = 1 << 14;
static constexpr size_t SEVENZIP_STREAM_LOOKAHEAD = 1 << 18;
static constexpr size_t SEVENZIP_SKIPBUF_SIZE = 1 << 15;

static constexpr UInt32 SEVENZIP_METHOD_COPY = 0;
static constexpr UInt32 SEVENZIP_METHOD_LZMA = 0x30101;
static constexpr UInt32 SEVENZIP_METHOD_LZMA2 = 0x21;

class SevenZipFileReference : public VFSFileReference {
public:
	UInt32 index = 0;
};

class SevenZipOpenFile : public VFSOpenFile {
public:
	enum class Mode {
		MEMORY,
		STREAM_COPY,
		STREAM_LZMA,
		STREAM_LZMA2,
	};

	SevenZipOpenFile() {
		LzmaDec_CONSTRUCT(&lzma_);
		Lzma2Dec_CONSTRUCT(&lzma2_);
		archiveStream_.wres = 0;
		File_Construct(&archiveStream_.file);
		LookToRead2_INIT(&lookStream_);
	}

	~SevenZipOpenFile() override {
		Release();
		delete[] data;
	}

	void Release() {
		if (lookStreamBuf_) {
			free(lookStreamBuf_);
			lookStreamBuf_ = nullptr;
		}
		if (lzmaAllocated_ && alloc_) {
			LzmaDec_Free(&lzma_, alloc_);
			lzmaAllocated_ = false;
		}
		if (lzma2Allocated_ && alloc_) {
			Lzma2Dec_Free(&lzma2_, alloc_);
			lzma2Allocated_ = false;
		}
		if (streamOpened_) {
			File_Close(&archiveStream_.file);
			streamOpened_ = false;
		}
	}

	Mode mode = Mode::MEMORY;

	uint8_t *data = nullptr;
	size_t size = 0;
	size_t offset = 0;

	UInt32 fileIndex = 0;
	UInt64 bytesToSkip = 0;
	UInt64 fileRemaining = 0;
	UInt64 packRemaining = 0;
	UInt64 folderRemaining = 0;
	bool streamError = false;
	bool hasFileCRC = false;
	UInt32 expectedFileCRC = 0;
	UInt32 runningCRC = CRC_INIT_VAL;
	bool crcChecked = false;

	ISzAlloc *alloc_ = nullptr;
	CFileInStream archiveStream_;
	CLookToRead2 lookStream_;
	Byte *lookStreamBuf_ = nullptr;
	bool streamOpened_ = false;
	CLzmaDec lzma_;
	CLzma2Dec lzma2_;
	bool lzmaAllocated_ = false;
	bool lzma2Allocated_ = false;
	std::array<uint8_t, SEVENZIP_SKIPBUF_SIZE> skipBuf_{};
};

static bool InitStreamingOpenFile(
	const Path &archivePath,
	const CSzArEx &db,
	ISzAlloc *alloc,
	UInt32 fileIndex,
	SevenZipOpenFile *openFile) {
	openFile->Release();
	openFile->mode = SevenZipOpenFile::Mode::MEMORY;
	openFile->alloc_ = alloc;
	openFile->fileIndex = fileIndex;

	const UInt32 folderIndex = db.FileToFolder[fileIndex];
	if (folderIndex == (UInt32)-1) {
		openFile->mode = SevenZipOpenFile::Mode::STREAM_COPY;
		openFile->bytesToSkip = 0;
		openFile->fileRemaining = 0;
		openFile->folderRemaining = 0;
		openFile->packRemaining = 0;
		openFile->size = 0;
		openFile->offset = 0;
		return true;
	}

	const Byte *folderData = db.db.CodersData + db.db.FoCodersOffsets[folderIndex];
	CSzData sd;
	sd.Data = folderData;
	sd.Size = db.db.FoCodersOffsets[(size_t)folderIndex + 1] - db.db.FoCodersOffsets[folderIndex];
	CSzFolder folder;
	if (SzGetNextFolderItem(&folder, &sd) != SZ_OK || sd.Size != 0) {
		return false;
	}

	if (folder.NumCoders != 1 || folder.NumPackStreams != 1 || folder.NumBonds != 0 || folder.PackStreams[0] != 0) {
		return false;
	}

	const CSzCoderInfo &coder = folder.Coders[0];
	if (coder.NumStreams != 1) {
		return false;
	}

	SevenZipOpenFile::Mode mode = SevenZipOpenFile::Mode::MEMORY;
	if (coder.MethodID == SEVENZIP_METHOD_COPY) {
		mode = SevenZipOpenFile::Mode::STREAM_COPY;
	} else if (coder.MethodID == SEVENZIP_METHOD_LZMA) {
		mode = SevenZipOpenFile::Mode::STREAM_LZMA;
	} else if (coder.MethodID == SEVENZIP_METHOD_LZMA2) {
		mode = SevenZipOpenFile::Mode::STREAM_LZMA2;
	} else {
		return false;
	}

	UInt64 packSize = 0;
	const UInt64 *packPositions = db.db.PackPositions + db.db.FoStartPackStreamIndex[folderIndex];
	packSize = packPositions[1] - packPositions[0];

	const UInt64 unpackPos = db.UnpackPositions[fileIndex];
	const UInt64 folderStartUnpackPos = db.UnpackPositions[db.FolderToFile[folderIndex]];
	const UInt64 fileSize64 = db.UnpackPositions[(size_t)fileIndex + 1] - unpackPos;
	const UInt64 folderSize64 = SzAr_GetFolderUnpackSize(&db.db, folderIndex);
	const UInt64 fileOffsetInFolder = unpackPos - folderStartUnpackPos;

	openFile->size = (size_t)fileSize64;
	openFile->offset = 0;
	openFile->mode = mode;
	openFile->bytesToSkip = fileOffsetInFolder;
	openFile->fileRemaining = fileSize64;
	openFile->folderRemaining = folderSize64;
	openFile->packRemaining = packSize;
	openFile->streamError = false;
	openFile->runningCRC = CRC_INIT_VAL;
	openFile->crcChecked = false;
	openFile->hasFileCRC = SzBitWithVals_Check(&db.CRCs, fileIndex);
	openFile->expectedFileCRC = openFile->hasFileCRC ? db.CRCs.Vals[fileIndex] : 0;

#ifdef _WIN32
	if (InFile_OpenW(&openFile->archiveStream_.file, archivePath.ToWString().c_str()) != 0) {
#else
	if (InFile_Open(&openFile->archiveStream_.file, archivePath.ToString().c_str()) != 0) {
#endif
		return false;
	}
	openFile->streamOpened_ = true;

	openFile->lookStreamBuf_ = (Byte *)malloc(SEVENZIP_LOOKBUF_SIZE * sizeof(Byte));
	if (!openFile->lookStreamBuf_) {
		openFile->Release();
		return false;
	}

	openFile->lookStream_.bufSize = SEVENZIP_LOOKBUF_SIZE;
	openFile->lookStream_.buf = openFile->lookStreamBuf_;
	openFile->lookStream_.realStream = &openFile->archiveStream_.vt;
	FileInStream_CreateVTable(&openFile->archiveStream_);
	LookToRead2_CreateVTable(&openFile->lookStream_, False);
	LookToRead2_INIT(&openFile->lookStream_);

	const UInt64 streamPos = db.dataPos + packPositions[0];
	if (LookInStream_SeekTo(&openFile->lookStream_.vt, streamPos) != SZ_OK) {
		openFile->Release();
		return false;
	}

	const Byte *props = folderData + coder.PropsOffset;
	if (mode == SevenZipOpenFile::Mode::STREAM_LZMA) {
		if (LzmaDec_Allocate(&openFile->lzma_, props, coder.PropsSize, alloc) != SZ_OK) {
			openFile->Release();
			return false;
		}
		openFile->lzmaAllocated_ = true;
		LzmaDec_Init(&openFile->lzma_);
	} else if (mode == SevenZipOpenFile::Mode::STREAM_LZMA2) {
		if (coder.PropsSize != 1 || Lzma2Dec_Allocate(&openFile->lzma2_, props[0], alloc) != SZ_OK) {
			openFile->Release();
			return false;
		}
		openFile->lzma2Allocated_ = true;
		Lzma2Dec_Init(&openFile->lzma2_);
	}

	return true;
}

static size_t StreamCopyRead(SevenZipOpenFile *openFile, uint8_t *dest, size_t length) {
	size_t produced = 0;
	while (produced < length && openFile->packRemaining > 0) {
		const size_t wanted = (size_t)std::min<UInt64>((UInt64)(length - produced), openFile->packRemaining);
		size_t lookahead = wanted;
		const void *inBuf = nullptr;
		const SRes lookRes = ILookInStream_Look(&openFile->lookStream_.vt, &inBuf, &lookahead);
		if (lookRes != SZ_OK || lookahead == 0) {
			openFile->streamError = true;
			break;
		}

		memcpy(dest + produced, inBuf, lookahead);
		if (ILookInStream_Skip(&openFile->lookStream_.vt, lookahead) != SZ_OK) {
			openFile->streamError = true;
			break;
		}

		openFile->packRemaining -= lookahead;
		produced += lookahead;
	}
	return produced;
}

static size_t StreamDecodeRead(SevenZipOpenFile *openFile, uint8_t *dest, size_t length) {
	size_t produced = 0;
	while (produced < length && openFile->packRemaining > 0) {
		size_t lookahead = (size_t)std::min<UInt64>(SEVENZIP_STREAM_LOOKAHEAD, openFile->packRemaining);
		const void *inBuf = nullptr;
		const SRes lookRes = ILookInStream_Look(&openFile->lookStream_.vt, &inBuf, &lookahead);
		if (lookRes != SZ_OK || lookahead == 0) {
			openFile->streamError = true;
			break;
		}

		SizeT inProcessed = (SizeT)lookahead;
		SizeT outProcessed = (SizeT)(length - produced);
		ELzmaStatus status = LZMA_STATUS_NOT_SPECIFIED;
		SRes decodeRes = SZ_ERROR_UNSUPPORTED;
		if (openFile->mode == SevenZipOpenFile::Mode::STREAM_LZMA) {
			decodeRes = LzmaDec_DecodeToBuf(&openFile->lzma_, dest + produced, &outProcessed, (const Byte *)inBuf, &inProcessed, LZMA_FINISH_ANY, &status);
		} else {
			decodeRes = Lzma2Dec_DecodeToBuf(&openFile->lzma2_, dest + produced, &outProcessed, (const Byte *)inBuf, &inProcessed, LZMA_FINISH_ANY, &status);
		}

		if (decodeRes != SZ_OK) {
			openFile->streamError = true;
			break;
		}

		if (inProcessed > 0) {
			if (ILookInStream_Skip(&openFile->lookStream_.vt, inProcessed) != SZ_OK) {
				openFile->streamError = true;
				break;
			}
			openFile->packRemaining -= inProcessed;
		}

		produced += outProcessed;
		if (outProcessed > 0) {
			if (openFile->folderRemaining < outProcessed) {
				openFile->streamError = true;
				break;
			}
			openFile->folderRemaining -= outProcessed;
		}

		if (inProcessed == 0 && outProcessed == 0) {
			openFile->streamError = true;
			break;
		}

		if (status == LZMA_STATUS_FINISHED_WITH_MARK || status == LZMA_STATUS_MAYBE_FINISHED_WITHOUT_MARK) {
			break;
		}
	}
	return produced;
}

static size_t StreamReadChunk(SevenZipOpenFile *openFile, uint8_t *dest, size_t length) {
	if (length == 0 || openFile->streamError) {
		return 0;
	}
	if (openFile->mode == SevenZipOpenFile::Mode::STREAM_COPY) {
		return StreamCopyRead(openFile, dest, length);
	}
	return StreamDecodeRead(openFile, dest, length);
}

void *SevenZipFileReader::Alloc(ISzAllocPtr, size_t size) {
	return size == 0 ? nullptr : malloc(size);
}

void SevenZipFileReader::Free(ISzAllocPtr, void *address) {
	free(address);
}

SevenZipFileReader::SevenZipFileReader(const Path &archivePath, const std::string &inArchivePath)
	: archivePath_(archivePath), inArchivePath_(inArchivePath) {
	if (!inArchivePath_.empty() && inArchivePath_.back() != '/') {
		inArchivePath_.push_back('/');
	}

	archiveStream_.wres = 0;
	File_Construct(&archiveStream_.file);

	allocImp_.Alloc = Alloc;
	allocImp_.Free = Free;
	allocTempImp_.Alloc = Alloc;
	allocTempImp_.Free = Free;

	SzArEx_Init(&db_);
	LookToRead2_INIT(&lookStream_);
}

SevenZipFileReader::~SevenZipFileReader() {
	std::lock_guard<std::mutex> guard(lock_);
	CloseArchive();
}

SevenZipFileReader *SevenZipFileReader::Create(const Path &archivePath, std::string_view inArchivePath, bool logErrors) {
	SevenZipFileReader *reader = new SevenZipFileReader(archivePath, std::string(inArchivePath));
	if (!reader->OpenArchive(logErrors)) {
		delete reader;
		return nullptr;
	}
	return reader;
}

bool SevenZipFileReader::OpenArchive(bool logErrors) {
	std::lock_guard<std::mutex> guard(lock_);

	if (valid_) {
		return true;
	}

	static bool crcTableGenerated = false;
	if (!crcTableGenerated) {
		CrcGenerateTable();
		crcTableGenerated = true;
	}

#ifdef _WIN32
	if (InFile_OpenW(&archiveStream_.file, archivePath_.ToWString().c_str()) != 0) {
#else
	if (InFile_Open(&archiveStream_.file, archivePath_.ToString().c_str()) != 0) {
#endif
		if (logErrors) {
			ERROR_LOG(Log::IO, "Failed to open %s as a 7z file", archivePath_.c_str());
		}
		return false;
	}

	lookStreamBuf_ = (Byte *)malloc(SEVENZIP_LOOKBUF_SIZE * sizeof(Byte));
	if (!lookStreamBuf_) {
		if (logErrors) {
			ERROR_LOG(Log::IO, "Failed to allocate 7z look buffer");
		}
		File_Close(&archiveStream_.file);
		return false;
	}

	lookStream_.bufSize = SEVENZIP_LOOKBUF_SIZE;
	lookStream_.buf = lookStreamBuf_;
	lookStream_.realStream = &archiveStream_.vt;
	FileInStream_CreateVTable(&archiveStream_);
	LookToRead2_CreateVTable(&lookStream_, False);
	LookToRead2_INIT(&lookStream_);

	const SRes res = SzArEx_Open(&db_, &lookStream_.vt, &allocImp_, &allocTempImp_);
	if (res != SZ_OK) {
		if (logErrors) {
			ERROR_LOG(Log::IO, "Failed to parse %s as a 7z archive (error %d)", archivePath_.c_str(), res);
		}
		CloseArchive();
		return false;
	}

	valid_ = BuildEntryCache();
	if (!valid_ && logErrors) {
		ERROR_LOG(Log::IO, "Failed to build file table for 7z archive %s", archivePath_.c_str());
	}
	return valid_;
}

void SevenZipFileReader::CloseArchive() {
	if (cachedBlock_) {
		IAlloc_Free(&allocImp_, cachedBlock_);
		cachedBlock_ = nullptr;
		cachedBlockSize_ = 0;
		blockIndex_ = 0xFFFFFFFF;
	}

	entries_.clear();
	SzArEx_Free(&db_, &allocImp_);
	File_Close(&archiveStream_.file);

	if (lookStreamBuf_) {
		free(lookStreamBuf_);
		lookStreamBuf_ = nullptr;
	}

	valid_ = false;
}

std::string SevenZipFileReader::ReadEntryPath(UInt32 index) const {
	const size_t utf16Length = SzArEx_GetFileNameUtf16(&db_, index, nullptr);
	if (utf16Length == 0) {
		return std::string();
	}

	std::vector<UInt16> utf16(utf16Length);
	SzArEx_GetFileNameUtf16(&db_, index, utf16.data());

	size_t actualLen = utf16Length;
	if (actualLen > 0 && utf16[actualLen - 1] == 0) {
		--actualLen;
	}

	std::u16string utf16String;
	utf16String.reserve(actualLen);
	for (size_t i = 0; i < actualLen; ++i) {
		utf16String.push_back((char16_t)utf16[i]);
	}

	std::string utf8 = ConvertUCS2ToUTF8(utf16String);
	std::replace(utf8.begin(), utf8.end(), '\\', '/');
	return utf8;
}

bool SevenZipFileReader::BuildEntryCache() {
	entries_.clear();
	entries_.reserve(db_.NumFiles);

	for (UInt32 i = 0; i < db_.NumFiles; ++i) {
		SevenZipEntry entry{};
		entry.path = ReadEntryPath(i);
		entry.isDirectory = SzArEx_IsDir(&db_, i) != 0;
		entry.size = (uint64_t)SzArEx_GetFileSize(&db_, i);
		entries_.push_back(std::move(entry));
	}

	return true;
}

std::string SevenZipFileReader::ResolvePath(std::string_view path) const {
	std::string resolved = join(inArchivePath_, path);
	std::replace(resolved.begin(), resolved.end(), '\\', '/');
	return resolved;
}

bool SevenZipFileReader::FindEntry(std::string_view path, UInt32 *index, bool *isDirectory) const {
	const std::string target = ResolvePath(path);

	for (UInt32 i = 0; i < (UInt32)entries_.size(); ++i) {
		const SevenZipEntry &entry = entries_[i];
		if (equalsNoCase(entry.path, target)) {
			*index = i;
			if (isDirectory) {
				*isDirectory = entry.isDirectory;
			}
			return true;
		}
	}
	return false;
}

uint8_t *SevenZipFileReader::ExtractFile(UInt32 fileIndex, size_t *size) {
	std::lock_guard<std::mutex> guard(lock_);

	if (!valid_) {
		return nullptr;
	}

	size_t offset = 0;
	size_t outSizeProcessed = 0;
	SRes res = SzArEx_Extract(
		&db_,
		&lookStream_.vt,
		fileIndex,
		&blockIndex_,
		&cachedBlock_,
		&cachedBlockSize_,
		&offset,
		&outSizeProcessed,
		&allocImp_,
		&allocTempImp_);
	if (res != SZ_OK) {
		ERROR_LOG(Log::IO, "Failed extracting '%s' from 7z archive (error %d)", entries_[fileIndex].path.c_str(), res);
		return nullptr;
	}

	uint8_t *data = new uint8_t[outSizeProcessed + 1];
	memcpy(data, cachedBlock_ + offset, outSizeProcessed);
	data[outSizeProcessed] = 0;
	*size = outSizeProcessed;
	return data;
}

uint8_t *SevenZipFileReader::ReadFile(std::string_view path, size_t *size) {
	UInt32 index = 0;
	bool isDirectory = false;
	if (!FindEntry(path, &index, &isDirectory) || isDirectory) {
		return nullptr;
	}
	return ExtractFile(index, size);
}

VFSFileReference *SevenZipFileReader::GetFile(std::string_view path) {
	UInt32 index = 0;
	bool isDirectory = false;
	if (!FindEntry(path, &index, &isDirectory) || isDirectory) {
		return nullptr;
	}

	SevenZipFileReference *ref = new SevenZipFileReference();
	ref->index = index;
	return ref;
}

bool SevenZipFileReader::GetFileInfo(VFSFileReference *vfsReference, File::FileInfo *fileInfo) {
	SevenZipFileReference *reference = (SevenZipFileReference *)vfsReference;
	if (reference->index >= entries_.size()) {
		return false;
	}

	const SevenZipEntry &entry = entries_[reference->index];
	*fileInfo = File::FileInfo{};
	fileInfo->isDirectory = entry.isDirectory;
	fileInfo->isWritable = false;
	fileInfo->exists = true;
	fileInfo->size = entry.size;
	return true;
}

void SevenZipFileReader::ReleaseFile(VFSFileReference *vfsReference) {
	delete (SevenZipFileReference *)vfsReference;
}

VFSOpenFile *SevenZipFileReader::OpenFileForRead(VFSFileReference *vfsReference, size_t *size) {
	SevenZipFileReference *reference = (SevenZipFileReference *)vfsReference;
	if (reference->index >= entries_.size()) {
		return nullptr;
	}
	if (entries_[reference->index].isDirectory) {
		return nullptr;
	}

	SevenZipOpenFile *openFile = new SevenZipOpenFile();
	if (!InitStreamingOpenFile(archivePath_, db_, &allocImp_, reference->index, openFile)) {
		// Fallback for unsupported folder graphs (for example BCJ2).
		openFile->mode = SevenZipOpenFile::Mode::MEMORY;
		openFile->data = ExtractFile(reference->index, &openFile->size);
		if (!openFile->data) {
			delete openFile;
			return nullptr;
		}
	}

	openFile->offset = 0;
	*size = openFile->size;
	return openFile;
}

void SevenZipFileReader::Rewind(VFSOpenFile *vfsOpenFile) {
	SevenZipOpenFile *openFile = (SevenZipOpenFile *)vfsOpenFile;
	if (openFile->mode != SevenZipOpenFile::Mode::MEMORY) {
		if (!InitStreamingOpenFile(archivePath_, db_, &allocImp_, openFile->fileIndex, openFile)) {
			openFile->streamError = true;
		}
		return;
	}
	openFile->offset = 0;
}

size_t SevenZipFileReader::Read(VFSOpenFile *vfsOpenFile, void *buffer, size_t length) {
	SevenZipOpenFile *openFile = (SevenZipOpenFile *)vfsOpenFile;
	if (openFile->mode != SevenZipOpenFile::Mode::MEMORY) {
		size_t produced = 0;
		uint8_t *dest = (uint8_t *)buffer;

		while (produced < length && openFile->fileRemaining > 0 && !openFile->streamError) {
			if (openFile->bytesToSkip > 0) {
				const size_t skipReq = (size_t)std::min<UInt64>(openFile->bytesToSkip, openFile->skipBuf_.size());
				const size_t skipped = StreamReadChunk(openFile, openFile->skipBuf_.data(), skipReq);
				if (skipped == 0) {
					openFile->streamError = true;
					break;
				}
				openFile->bytesToSkip -= skipped;
				continue;
			}

			const size_t wanted = (size_t)std::min<UInt64>((UInt64)(length - produced), openFile->fileRemaining);
			const size_t got = StreamReadChunk(openFile, dest + produced, wanted);
			if (got == 0) {
				openFile->streamError = true;
				break;
			}

			if (openFile->hasFileCRC) {
				openFile->runningCRC = CrcUpdate(openFile->runningCRC, dest + produced, got);
			}

			produced += got;
			openFile->fileRemaining -= got;
			openFile->offset += got;
		}

		if (openFile->fileRemaining == 0 && openFile->hasFileCRC && !openFile->crcChecked) {
			openFile->crcChecked = true;
			if (CRC_GET_DIGEST(openFile->runningCRC) != openFile->expectedFileCRC) {
				openFile->streamError = true;
				ERROR_LOG(Log::IO, "CRC mismatch while streaming '%s' from 7z archive", entries_[openFile->fileIndex].path.c_str());
			}
		}

		return produced;
	}

	if (openFile->offset >= openFile->size) {
		return 0;
	}

	const size_t remaining = openFile->size - openFile->offset;
	const size_t toRead = std::min(length, remaining);
	memcpy(buffer, openFile->data + openFile->offset, toRead);
	openFile->offset += toRead;
	return toRead;
}

void SevenZipFileReader::CloseFile(VFSOpenFile *vfsOpenFile) {
	delete (SevenZipOpenFile *)vfsOpenFile;
}

bool SevenZipFileReader::GetFileListing(std::string_view origPath, std::vector<File::FileInfo> *listing, const char *filter) {
	std::string path = ResolvePath(origPath);
	if (!path.empty() && path.back() != '/') {
		path.push_back('/');
	}

	std::set<std::string> filters;
	std::string tmp;
	if (filter) {
		while (*filter) {
			if (*filter == ':') {
				filters.emplace("." + tmp);
				tmp.clear();
			} else {
				tmp.push_back(*filter);
			}
			filter++;
		}
	}
	if (!tmp.empty()) {
		filters.emplace("." + tmp);
	}

	std::set<std::string> files;
	std::set<std::string> directories;
	for (const auto &entry : entries_) {
		if (!startsWith(entry.path, path)) {
			continue;
		}

		if (entry.path.size() == path.size()) {
			continue;
		}

		std::string_view relative = std::string_view(entry.path).substr(path.size());
		size_t slashPos = relative.find('/');
		if (slashPos != std::string::npos) {
			directories.emplace(std::string(relative.substr(0, slashPos)));
		} else if (!entry.isDirectory) {
			files.emplace(std::string(relative));
		}
	}

	listing->clear();

	const std::string relativePath = path.substr(inArchivePath_.size());
	listing->reserve(directories.size() + files.size());

	for (const auto &dir : directories) {
		File::FileInfo info;
		info.name = dir;
		info.fullName = Path(relativePath + dir);
		info.exists = true;
		info.isWritable = false;
		info.isDirectory = true;
		listing->push_back(info);
	}

	for (const auto &file : files) {
		File::FileInfo info;
		info.name = file;
		info.fullName = Path(relativePath + file);
		info.exists = true;
		info.isWritable = false;
		info.isDirectory = false;
		if (filter) {
			std::string ext = info.fullName.GetFileExtension();
			if (filters.find(ext) == filters.end()) {
				continue;
			}
		}
		listing->push_back(info);
	}

	std::sort(listing->begin(), listing->end());
	return !listing->empty();
}

bool SevenZipFileReader::GetFileInfo(std::string_view path, File::FileInfo *info) {
	*info = File::FileInfo{};
	info->fullName = Path(path);
	info->isWritable = false;

	UInt32 index = 0;
	bool isDirectory = false;
	if (FindEntry(path, &index, &isDirectory)) {
		const SevenZipEntry &entry = entries_[index];
		info->exists = true;
		info->isDirectory = entry.isDirectory;
		info->size = entry.size;
		return true;
	}

	const std::string base = ResolvePath(path);
	const std::string prefix = base.empty() || base.back() == '/' ? base : base + "/";
	for (const auto &entry : entries_) {
		if (startsWith(entry.path, prefix)) {
			info->exists = true;
			info->isDirectory = true;
			info->size = 0;
			return true;
		}
	}

	info->exists = false;
	return false;
}
