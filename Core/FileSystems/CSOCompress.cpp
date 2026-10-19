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
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <thread>
#include <vector>

#include "Common/CommonTypes.h"
#include "Common/File/FileUtil.h"
#include "Common/Log.h"
#include "Common/Swap.h"
#include "Common/Thread/ThreadUtil.h"
#include "Core/FileSystems/BlockDevices.h"
#include "Core/Loaders.h"
#include "Core/FileSystems/CSOCompress.h"

#ifdef SHARED_ZLIB
#include <zlib.h>
#else
#include "ext/zlib/zlib.h"
#endif

#include "ext/libdeflate/libdeflate.h"

namespace CSOCompress {

namespace {

const uint32_t SECTOR_SIZE = 2048;
const uint32_t CSO_INDEX_UNCOMPRESSED = 0x80000000;
// Input read and compressed per round, so the threads always have work queued.
const size_t BATCH_BYTES = 32 * 1024 * 1024;

struct PresetSettings {
	uint32_t blockSize;
	bool zlib;        // zlib level 9, default strategy.
	bool libdeflate;  // libdeflate level 12.
};

// The trials maxcso's presets ran (with lz4, 7-zip and zlib's brute force strategies left out).
PresetSettings SettingsFor(Preset preset) {
	switch (preset) {
	case Preset::Best:
		// 64K: only ~0.25% bigger than 256K, and each read of a new block inflates a quarter as much.
		return { 65536, false, true };
	case Preset::PSPCompatible:
	default:
		// Some custom firmware can't read libdeflate's output or larger blocks.
		return { 2048, true, false };
	}
}

// One thread's encoders, reused for every block it compresses.
class BlockCompressor {
public:
	explicit BlockCompressor(const PresetSettings &settings) : settings_(settings), scratch_(settings.blockSize) {
		if (settings_.zlib) {
			zlibOk_ = deflateInit2(&z_, 9, Z_DEFLATED, -15, 9, Z_DEFAULT_STRATEGY) == Z_OK;
		}
		if (settings_.libdeflate) {
			libdeflate_ = libdeflate_alloc_compressor(12);
		}
	}
	~BlockCompressor() {
		if (zlibOk_) {
			deflateEnd(&z_);
		}
		if (libdeflate_) {
			libdeflate_free_compressor(libdeflate_);
		}
	}

	// Leaves the smallest raw deflate stream in *out, or nothing when none beats the block itself.
	void Compress(const u8 *in, std::vector<u8> *out) {
		const uint32_t blockSize = settings_.blockSize;
		out->clear();
		auto submit = [&](const u8 *data, size_t size) {
			// Not smaller than the block: stored as is, which also reads faster.
			if (size > 0 && size < blockSize && (out->empty() || size < out->size())) {
				out->assign(data, data + size);
			}
		};

		if (libdeflate_) {
			// libdeflate returns 0 when the result doesn't fit, i.e. isn't smaller.
			const size_t size = libdeflate_deflate_compress(libdeflate_, in, blockSize, scratch_.data(), scratch_.size());
			submit(scratch_.data(), size);
		}
		if (zlibOk_ && deflateReset(&z_) == Z_OK) {
			z_.next_in = (Bytef *)in;
			z_.avail_in = blockSize;
			z_.next_out = scratch_.data();
			z_.avail_out = (uInt)scratch_.size();
			int res;
			while ((res = deflate(&z_, Z_FINISH)) == Z_OK) {
				continue;
			}
			if (res == Z_STREAM_END) {
				submit(scratch_.data(), z_.total_out);
			}
		}
	}

private:
	PresetSettings settings_;
	std::vector<u8> scratch_;
	z_stream z_{};
	bool zlibOk_ = false;
	libdeflate_compressor *libdeflate_ = nullptr;
};

// Opens anything PPSSPP plays as a disc, as 2048-byte sectors.
struct Source {
	std::unique_ptr<FileLoader> loader;
	std::unique_ptr<BlockDevice> device;
	uint64_t size = 0;

	bool Open(const Path &path, std::string *error) {
		loader.reset(ConstructFileLoader(path));
		if (!loader || !loader->Exists()) {
			*error = "Could not open the game";
			return false;
		}
		device.reset(ConstructBlockDevice(loader.get(), error));
		if (!device) {
			return false;
		}
		size = (uint64_t)device->GetNumBlocks() * SECTOR_SIZE;
		if (device->GetUncompressedSize() != size) {
			*error = "The image isn't a whole number of sectors";
			return false;
		}
		if (size == 0) {
			*error = "The image is empty";
			return false;
		}
		return true;
	}
};

bool WriteAll(FILE *f, const void *data, size_t size) {
	return size == 0 || fwrite(data, 1, size, f) == size;
}

// Fails a conversion, removing what it wrote.
bool Fail(FILE *&f, const Path &output, std::string *error, const char *message) {
	if (f) {
		fclose(f);
		f = nullptr;
	}
	File::Delete(output, true);
	if (message) {
		*error = message;
	}
	return false;
}

}  // namespace

bool Compress(const Path &input, const Path &output, Preset preset, const ProgressFunc &progress, std::string *error, bool *cancelled) {
	*cancelled = false;
	if (File::Exists(output)) {
		*error = "Output file already exists";
		return false;
	}
	Source src;
	if (!src.Open(input, error)) {
		return false;
	}

	const PresetSettings settings = SettingsFor(preset);
	const uint32_t blockSize = settings.blockSize;
	const uint32_t sectorsPerBlock = blockSize / SECTOR_SIZE;
	const uint32_t numBlocks = (uint32_t)((src.size + blockSize - 1) / blockSize);
	const uint32_t numSectors = src.device->GetNumBlocks();

	// The index holds offsets >> shift in 31 bits. Like maxcso, size it for the worst case, every
	// block stored uncompressed.
	const uint64_t headerSize = 24;
	const uint64_t dataStart = headerSize + ((uint64_t)numBlocks + 1) * 4;
	const uint64_t worst = dataStart + src.size;
	uint8_t indexShift = 0;
	for (int i = 62; i >= 31; --i) {
		if (worst >= (1ULL << i)) {
			indexShift = (uint8_t)(i + 1 - 31);
			break;
		}
	}
	const uint64_t align = 1ULL << indexShift;
	auto alignUp = [align](uint64_t pos) {
		return (pos + align - 1) & ~(align - 1);
	};

	FILE *f = File::OpenCFile(output, "wb");
	if (!f) {
		*error = "Could not create the output file";
		return false;
	}
	std::vector<u32_le> index(numBlocks + 1);
	// Header and index go in at the end, once the offsets are known.
	std::vector<u8> zeros((size_t)(alignUp(dataStart)), 0);
	if (!WriteAll(f, zeros.data(), zeros.size())) {
		return Fail(f, output, error, "Could not write the output file");
	}
	uint64_t pos = zeros.size();

	const unsigned threads = std::max(1u, std::thread::hardware_concurrency());
	const uint32_t batchBlocks = std::max<uint32_t>(threads * 4, (uint32_t)(BATCH_BYTES / blockSize));
	std::vector<u8> in((size_t)batchBlocks * blockSize);
	std::vector<std::vector<u8>> out(batchBlocks);
	static const u8 padding[2048]{};

	INFO_LOG(Log::Loader, "CSOCompress: %s -> %s, %u blocks of %u, %u threads", input.c_str(), output.c_str(), numBlocks, blockSize, threads);
	for (uint32_t first = 0; first < numBlocks; first += batchBlocks) {
		if (progress && !progress((float)first / (float)numBlocks)) {
			*cancelled = true;
			return Fail(f, output, error, "Cancelled");
		}
		const uint32_t count = std::min(batchBlocks, numBlocks - first);

		// The last block past the end of the image is zero-padded, like maxcso does.
		const uint32_t firstSector = first * sectorsPerBlock;
		const uint32_t sectors = std::min(count * sectorsPerBlock, numSectors - firstSector);
		memset(in.data() + (size_t)sectors * SECTOR_SIZE, 0, ((size_t)count * sectorsPerBlock - sectors) * SECTOR_SIZE);
		if (!src.device->ReadBlocks(firstSector, (int)sectors, in.data())) {
			return Fail(f, output, error, "Could not read the game");
		}

		std::atomic<uint32_t> next{0};
		auto work = [&]() {
			BlockCompressor compressor(settings);
			uint32_t i;
			while ((i = next++) < count) {
				compressor.Compress(in.data() + (size_t)i * blockSize, &out[i]);
			}
		};
		std::vector<std::thread> pool;
		const unsigned workers = std::min<unsigned>(threads, count);
		for (unsigned t = 1; t < workers; ++t) {
			pool.emplace_back([&]() {
				SetCurrentThreadName("CSOCompress");
				work();
			});
		}
		work();
		for (std::thread &t : pool) {
			t.join();
		}

		for (uint32_t i = 0; i < count; ++i) {
			// Not smaller once padded to the index alignment (images over 2 GB): store it.
			const bool stored = out[i].empty() || alignUp(out[i].size()) >= blockSize;
			const u8 *data = stored ? in.data() + (size_t)i * blockSize : out[i].data();
			const size_t size = stored ? blockSize : out[i].size();
			index[first + i] = (u32)(pos >> indexShift) | (stored ? CSO_INDEX_UNCOMPRESSED : 0);
			if (!WriteAll(f, data, size)) {
				return Fail(f, output, error, "Could not write the output file");
			}
			pos += size;
			const uint64_t aligned = alignUp(pos);
			if (!WriteAll(f, padding, (size_t)(aligned - pos))) {
				return Fail(f, output, error, "Could not write the output file");
			}
			pos = aligned;
		}
	}
	index[numBlocks] = (u32)(pos >> indexShift);

	// The header: "CISO", its size, the image size, the block size, version 1 and the shift.
	u8 header[24]{};
	memcpy(header, "CISO", 4);
	const u32_le headerSizeLE = (u32)headerSize;
	const u64_le sizeLE = src.size;
	const u32_le blockSizeLE = blockSize;
	memcpy(header + 4, &headerSizeLE, 4);
	memcpy(header + 8, &sizeLE, 8);
	memcpy(header + 16, &blockSizeLE, 4);
	header[20] = 1;
	header[21] = indexShift;
	if (File::Fseek(f, 0, SEEK_SET) != 0 || !WriteAll(f, header, sizeof(header)) || !WriteAll(f, index.data(), index.size() * sizeof(u32_le))) {
		return Fail(f, output, error, "Could not write the output file");
	}
	if (fclose(f) != 0) {
		f = nullptr;
		return Fail(f, output, error, "Could not write the output file");
	}
	if (progress) {
		progress(1.0f);
	}
	return true;
}

bool Extract(const Path &input, const Path &output, const ProgressFunc &progress, std::string *error, bool *cancelled) {
	*cancelled = false;
	if (File::Exists(output)) {
		*error = "Output file already exists";
		return false;
	}
	Source src;
	if (!src.Open(input, error)) {
		return false;
	}
	FILE *f = File::OpenCFile(output, "wb");
	if (!f) {
		*error = "Could not create the output file";
		return false;
	}

	const uint32_t numSectors = src.device->GetNumBlocks();
	const uint32_t batch = (uint32_t)(BATCH_BYTES / SECTOR_SIZE);
	std::vector<u8> buffer((size_t)batch * SECTOR_SIZE);
	for (uint32_t first = 0; first < numSectors; first += batch) {
		if (progress && !progress((float)first / (float)numSectors)) {
			*cancelled = true;
			return Fail(f, output, error, "Cancelled");
		}
		const uint32_t count = std::min(batch, numSectors - first);
		if (!src.device->ReadBlocks(first, (int)count, buffer.data())) {
			return Fail(f, output, error, "Could not read the game");
		}
		if (!WriteAll(f, buffer.data(), (size_t)count * SECTOR_SIZE)) {
			return Fail(f, output, error, "Could not write the output file");
		}
	}
	if (fclose(f) != 0) {
		f = nullptr;
		return Fail(f, output, error, "Could not write the output file");
	}
	if (progress) {
		progress(1.0f);
	}
	return true;
}

}  // namespace CSOCompress
