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
#include <vector>

namespace Rasterizer {
struct RasterizerState;
}
struct BinItem;

// The GE's texture cache (measured on hardware with geprobe exp177-202, ppsspp-re docs/ge-texture-cache.md):
// 64 lines of 16 bytes x 8 rows in 16 sets of 4 ways, LRU. A line is tagged by its position in the texture
// (16-byte column, 8-row band) and mip level only, not the texture's address, buffer width, format or swizzle
// mode, so a texture read after another without TEXFLUSH gets the first one's bytes where their lines
// coincide. Lines load raw bytes from memory when first read, and nothing but TEXFLUSH drops them: rendering
// into a texture, block transfers and CPU writes leave the cached bytes as they were.
//
// This simulates it a primitive at a time, in submission order, from each primitive's texel footprint
// (which lines it can read). A line remembers where it was loaded from; its bytes are copied out only when
// that memory is about to change (BeforeWrite), so while nothing changes it costs only the tags. And while
// every line came from the one texture read since the last TEXFLUSH and none has been copied out, nothing
// read can differ from memory: then primitives only log what they read, which is stepped through the
// cache when that stops being true (or when it's dropped at TEXFLUSH, not at all).
class TexCache {
public:
	TexCache();

	// TEXFLUSH.
	void Clear();

	// Simulates the reads of a textured primitive. Returns true when it would read cached bytes that differ
	// from what its texture holds in memory now (then draw it from Image()).
	bool Access(const BinItem &item, const Rasterizer::RasterizerState &state);
	// The bytes from start to end (masked addresses) are about to change: lines loaded from them keep the old.
	void BeforeWrite(uint32_t start, uint32_t end);
	// The CPU is about to run, and may write anything: every line keeps its bytes.
	void CaptureAll();
	// Whether the primitive of the last Access reads any of the rectangle x1-x2, y1-y2 of a buffer at base
	// (bytes per pixel bpp, stride in pixels).
	bool FootprintReads(uint32_t base, int bpp, int stride, int x1, int y1, int x2, int y2);

	// Nothing cached or logged: writes can't matter.
	bool IsEmpty() const {
		return used_ == 0 && log_.empty();
	}
	// Whether writing start to end could change what the cache holds (else BeforeWrite does nothing).
	bool MayHold(uint32_t start, uint32_t end) const {
		return (!log_.empty() && end > epoch_.lo && start < epoch_.hi) || (used_ != 0 && end > uncapturedLo_ && start < uncapturedHi_);
	}

	// Copies of state's texture levels with what the last Access read from the cache laid over memory, for
	// drawing that primitive. Points state's texptr at them (for DXT, at 8888 decodes, and samples 8888).
	void Image(Rasterizer::RasterizerState &state);

private:
	void DecodedImage(Rasterizer::RasterizerState &state);

	struct Source {
		// Where the line's bytes came from: the level's address, its row stride in bytes, swizzled or not.
		uint32_t addr = 0;
		// For DXT, the bytes of a row of blocks.
		uint32_t strideBytes = 0;
		bool swizzled = false;
		// 0x00200000 or 0x00600000 for a level read through a swizzled VRAM mirror: memory through the depth
		// layout (Memory::DepthMirrored16), so its bytes lie anywhere in their 64 KB page.
		uint32_t mirror = 0;
		// The DXT format (the GE caches DXT decoded, as 8888 texels), or 0.
		uint8_t dxt = 0;
		bool operator==(const Source &o) const {
			return addr == o.addr && strideBytes == o.strideBytes && swizzled == o.swizzled && mirror == o.mirror && dxt == o.dxt;
		}
	};
	// What a primitive's footprint depends on, besides its texture coordinates.
	struct Config {
		int bits = 0;
		int level0 = 0, level1 = 0;
		uint16_t w[8]{}, h[8]{};
		bool clampS = false, clampT = false, linear = false;
		Source src[8];
		// The memory its levels span.
		uint32_t lo = 0, hi = 0;
		bool SameSources(const Config &o) const;
		bool operator==(const Config &o) const;
	};
	// The texture coordinate bounds of a primitive, as 0-1 of level 0 (unbounded: the whole texture).
	struct Bounds {
		float smin, smax, tmin, tmax;
		bool bounded;
	};
	struct Logged {
		uint16_t config;
		Bounds bounds;
	};
	struct Line {
		bool valid = false;
		// The bytes are in data (copied before their memory changed); else they're still in memory at src.
		bool captured = false;
		uint8_t level = 0;
		uint16_t col = 0;
		uint16_t band = 0;
		uint32_t lru = 0;
		Source src;
		uint8_t data[128];
	};
	// Bands band0-band1 of a level, each read across one or two column segments (two where it wraps).
	struct Footprint {
		int level;
		int segments;
		int col0[2], col1[2];
		int band0, band1;
	};
	// A line the last Access read from the cache that differs from memory: what it read then (later lines of
	// the same primitive can evict it).
	struct Delivered {
		uint8_t level;
		uint16_t col, band;
		uint8_t data[128];
	};

	static int SetIndex(int bits, int col, int band);
	static uint32_t Key(int level, int col, int band) {
		return 0x80000000 | ((uint32_t)level << 26) | ((uint32_t)band << 13) | (uint32_t)col;
	}
	static bool MakeConfig(const Rasterizer::RasterizerState &state, Config &cfg);
	static bool MakeBounds(const BinItem &item, const Rasterizer::RasterizerState &state, Bounds &b);
	static void MakeFootprint(const Config &cfg, const Bounds &b, std::vector<Footprint> &out);
	Line *Find(int bits, int level, int col, int band);
	void Touch(int bits, int level, int col, int band, const Source &src);
	void Step(const Config &cfg, const std::vector<Footprint> &fp);
	void ReplayLog();
	// The line's bytes as the GE holds them.
	void Bytes(const Line &line, uint8_t out[128]) const;
	// The bytes of line (col, band) of the texture level at src, as memory holds them now.
	static void ReadLine(const Source &src, int col, int band, uint8_t out[128]);
	// Where a line is in its level (with the gaps between its rows), as addresses from src.addr.
	static void LineRange(const Source &src, int col, int band, uint32_t &start, uint32_t &end);
	// The memory a line's bytes can be in: LineRange, or its whole 64 KB pages through a depth mirror.
	static void MemoryRange(const Source &src, int col, int band, uint32_t &start, uint32_t &end);
	// n bytes from addr in src's level, as memory holds them now (0 where it isn't valid).
	static void CopyFrom(const Source &src, uint32_t addr, uint8_t *out, uint32_t n);

	Line lines_[16][4];
	// Each line's tag (Key), 0 when it's empty, apart for a quick look up.
	uint32_t keys_[16][4]{};
	uint32_t clock_ = 0;
	int used_ = 0;
	// Bounds of the memory the lines not yet captured were loaded from, to skip most BeforeWrites.
	uint32_t uncapturedLo_ = UINT32_MAX;
	uint32_t uncapturedHi_ = 0;

	// Since TEXFLUSH: every line is from one texture's sources and none has been captured, so the lines are
	// what memory holds and primitives just log their reads.
	bool coherent_ = true;
	bool epochStarted_ = false;
	Config epoch_;
	std::vector<Config> configs_;
	std::vector<Logged> log_;
	// The state serials the epoch comparison and the last logged config were made for.
	uint32_t epochSerial_ = 0;
	bool sameEpoch_ = false;
	uint32_t loggedSerial_ = 0;

	// The last Access: its config and footprint (footprintValid_ once computed), and what it read stale.
	Config current_;
	// The state serial current_ was made for (a state's config is the same for all its primitives).
	uint32_t configSerial_ = 0;
	bool configOk_ = false;
	Bounds bounds_{};
	bool currentValid_ = false;
	bool footprintValid_ = false;
	std::vector<Footprint> footprint_;
	std::vector<Delivered> delivered_;
	std::vector<uint8_t> images_[8];
};
