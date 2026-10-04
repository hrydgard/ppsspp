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
#include <cmath>
#include <cstring>

#include "Common/Log.h"
#include "Core/Config.h"
#include "Core/MemMap.h"
#include "GPU/Common/TextureDecoder.h"
#include "GPU/GPUState.h"
#include "GPU/Software/BinManager.h"
#include "GPU/Software/Rasterizer.h"
#include "GPU/Software/Sampler.h"
#include "GPU/Software/TexCache.h"

// VRAM's mirrors are one memory.
static uint32_t MaskAddress(uint32_t addr) {
	addr &= 0x3FFFFFFF;
	if ((addr & 0x0F000000) == 0x04000000)
		addr &= 0x041FFFFF;
	return addr;
}

static int TexelBits(GETextureFormat fmt) {
	switch (fmt) {
	case GE_TFMT_CLUT4: return 4;
	case GE_TFMT_CLUT8: return 8;
	case GE_TFMT_5650: case GE_TFMT_5551: case GE_TFMT_4444: case GE_TFMT_CLUT16: return 16;
	case GE_TFMT_8888: case GE_TFMT_CLUT32: return 32;
	// DXT is cached decoded (exp203).
	case GE_TFMT_DXT1: case GE_TFMT_DXT3: case GE_TFMT_DXT5: return 32;
	default: return 0;
	}
}

static uint32_t DXTBlockBytes(uint8_t dxt) {
	return dxt == GE_TFMT_DXT1 ? 8 : 16;
}

TexCache::TexCache() {
	Clear();
}

void TexCache::Clear() {
	if (used_ != 0) {
		for (auto &set : lines_) {
			for (Line &line : set)
				line.valid = false;
		}
		memset(keys_, 0, sizeof(keys_));
		used_ = 0;
	}
	uncapturedLo_ = UINT32_MAX;
	uncapturedHi_ = 0;
	coherent_ = true;
	epochStarted_ = false;
	configs_.clear();
	log_.clear();
	loggedSerial_ = 0;
}

// The set split by bits per texel (exp198, exp199, exp202): four bits of column and band, the column's share
// shrinking as texels get smaller, so a set's lines repeat over a roughly square area of texels.
int TexCache::SetIndex(int bits, int col, int band) {
	switch (bits) {
	case 32: return (col & 7) | ((band & 1) << 3);
	case 16:
	case 8: return (col & 3) | ((band & 3) << 2);
	default: return (col & 1) | ((band & 7) << 1);
	}
}

void TexCache::CopyFrom(const Source &src, uint32_t addr, uint8_t *out, uint32_t n) {
	if (!src.mirror) {
		const uint32_t avail = Memory::ClampValidSizeAt(addr, n);
		if (avail)
			memcpy(out, Memory::GetPointerUnchecked(addr), avail);
		memset(out + avail, 0, n - avail);
		return;
	}
	// The depth layout keeps runs of 32 aligned bytes together.
	for (uint32_t pos = 0; pos < n; ) {
		const uint32_t a = addr + pos;
		const uint32_t run = std::min(32 - (a & 31), n - pos);
		const uint32_t from = Memory::DepthMirrored16(0x04000000 | src.mirror | (a & 0x001FFFFF));
		if (Memory::IsValidRange(from, run))
			memcpy(out + pos, Memory::GetPointerUnchecked(from), run);
		else
			memset(out + pos, 0, run);
		pos += run;
	}
}

void TexCache::ReadLine(const Source &src, int col, int band, uint8_t out[128]) {
	if (src.dxt) {
		// Two blocks, decoded: the line's 4 x 8 texels as 8888 (as the sampler decodes them).
		const uint32_t blockBytes = DXTBlockBytes(src.dxt);
		for (int r = 0; r < 8; ++r) {
			const int v = band * 8 + r;
			const uint32_t addr = src.addr + (uint32_t)(v >> 2) * src.strideBytes + col * blockBytes;
			uint32_t texels[4]{};
			if (Memory::IsValidRange(addr, blockBytes)) {
				alignas(16) u8 block[16];
				CopyFrom(src, addr, block, blockBytes);
				for (int x = 0; x < 4; ++x) {
					switch (src.dxt) {
					case GE_TFMT_DXT1: texels[x] = GetDXT1Texel((const DXT1Block *)block, x, v & 3); break;
					case GE_TFMT_DXT3: texels[x] = GetDXT3Texel((const DXT3Block *)block, x, v & 3); break;
					default: texels[x] = GetDXT5Texel((const DXT5Block *)block, x, v & 3); break;
					}
				}
			}
			memcpy(out + r * 16, texels, 16);
		}
		return;
	}
	if (src.swizzled) {
		// A swizzle block is the line: 16 bytes x 8 rows, contiguous.
		CopyFrom(src, src.addr + (uint32_t)(band * (src.strideBytes / 16) + col) * 128, out, 128);
		return;
	}
	for (int r = 0; r < 8; ++r)
		CopyFrom(src, src.addr + (uint32_t)(band * 8 + r) * src.strideBytes + col * 16, out + r * 16, 16);
}

void TexCache::MemoryRange(const Source &src, int col, int band, uint32_t &start, uint32_t &end) {
	LineRange(src, col, band, start, end);
	if (src.mirror) {
		start &= ~0xFFFFu;
		end = (end + 0xFFFF) & ~0xFFFFu;
	}
}

void TexCache::LineRange(const Source &src, int col, int band, uint32_t &start, uint32_t &end) {
	if (src.dxt) {
		const uint32_t blockBytes = DXTBlockBytes(src.dxt);
		start = src.addr + (uint32_t)band * 2 * src.strideBytes + col * blockBytes;
		end = start + src.strideBytes + blockBytes;
		return;
	}
	if (src.swizzled) {
		start = src.addr + (uint32_t)(band * (src.strideBytes / 16) + col) * 128;
		end = start + 128;
	} else {
		start = src.addr + (uint32_t)band * 8 * src.strideBytes + col * 16;
		end = start + 7 * src.strideBytes + 16;
	}
}

void TexCache::Bytes(const Line &line, uint8_t out[128]) const {
	if (line.captured)
		memcpy(out, line.data, 128);
	else
		ReadLine(line.src, line.col, line.band, out);
}

TexCache::Line *TexCache::Find(int bits, int level, int col, int band) {
	const int s = SetIndex(bits, col, band);
	const uint32_t key = Key(level, col, band);
	for (int w = 0; w < 4; ++w) {
		if (keys_[s][w] == key)
			return &lines_[s][w];
	}
	return nullptr;
}

void TexCache::Touch(int bits, int level, int col, int band, const Source &src) {
	Line *line = Find(bits, level, col, band);
	if (line) {
		line->lru = ++clock_;
		if (line->captured || !(line->src == src)) {
			Delivered d{ (uint8_t)level, (uint16_t)col, (uint16_t)band };
			uint8_t mem[128];
			Bytes(*line, d.data);
			ReadLine(src, col, band, mem);
			if (memcmp(d.data, mem, 128) != 0)
				delivered_.push_back(d);
		}
		return;
	}
	Load(bits, level, col, band, src);
}

// A miss loads from memory, over the least recently used way.
TexCache::Line *TexCache::Load(int bits, int level, int col, int band, const Source &src) {
	const int s = SetIndex(bits, col, band);
	Line *set = lines_[s];
	int way = 0;
	for (int w = 0; w < 4; ++w) {
		if (!set[w].valid) {
			way = w;
			break;
		}
		if (set[w].lru < set[way].lru)
			way = w;
	}
	Line *victim = &set[way];
	keys_[s][way] = Key(level, col, band);
	if (!victim->valid)
		used_++;
	victim->valid = true;
	victim->captured = false;
	victim->level = (uint8_t)level;
	victim->col = (uint16_t)col;
	victim->band = (uint16_t)band;
	victim->lru = ++clock_;
	victim->src = src;
	uint32_t start, end;
	MemoryRange(src, col, band, start, end);
	uncapturedLo_ = std::min(uncapturedLo_, start);
	uncapturedHi_ = std::max(uncapturedHi_, end);
	return victim;
}

bool TexCache::Config::SameSources(const Config &o) const {
	if (bits != o.bits || level1 < level0 || o.level1 < o.level0)
		return false;
	// Lines of the levels both read.
	for (int i = std::max(level0, o.level0); i <= std::min(level1, o.level1); ++i) {
		if (!(src[i] == o.src[i]))
			return false;
	}
	return lo == o.lo && hi == o.hi;
}

bool TexCache::Config::operator==(const Config &o) const {
	if (bits != o.bits || level0 != o.level0 || level1 != o.level1 || clampS != o.clampS || clampT != o.clampT || linear != o.linear)
		return false;
	for (int i = level0; i <= level1; ++i) {
		if (w[i] != o.w[i] || h[i] != o.h[i] || !(src[i] == o.src[i]))
			return false;
	}
	return true;
}

void TexCache::BeforeWrite(uint32_t start, uint32_t end) {
	// The logged reads, if they're of this memory, have to be in the cache to keep what they loaded.
	if (!log_.empty() && end > epoch_.lo && start < epoch_.hi)
		ReplayLog();
	if (used_ == 0 || end <= uncapturedLo_ || start >= uncapturedHi_)
		return;
	for (auto &set : lines_) {
		for (Line &line : set) {
			if (!line.valid || line.captured)
				continue;
			uint32_t ls, le;
			MemoryRange(line.src, line.col, line.band, ls, le);
			if (le <= start || ls >= end)
				continue;
			ReadLine(line.src, line.col, line.band, line.data);
			line.captured = true;
			coherent_ = false;
		}
	}
}

void TexCache::CaptureAll() {
	ReplayLog();
	if (used_ == 0 || uncapturedLo_ >= uncapturedHi_)
		return;
	for (auto &set : lines_) {
		for (Line &line : set) {
			if (line.valid && !line.captured) {
				ReadLine(line.src, line.col, line.band, line.data);
				line.captured = true;
				coherent_ = false;
			}
		}
	}
	uncapturedLo_ = UINT32_MAX;
	uncapturedHi_ = 0;
}

bool TexCache::MakeConfig(const Rasterizer::RasterizerState &state, Config &cfg) {
	cfg.bits = TexelBits(state.samplerID.TexFmt());
	if (cfg.bits == 0)
		return false;
	const int maxLevel = state.maxTexLevel;
	cfg.level0 = 0;
	cfg.level1 = maxLevel;
	if (maxLevel != 0 && state.TexLevelMode() == GE_TEXLEVEL_MODE_CONST) {
		// A constant level, and the next one when blending between levels.
		const int offset = std::max(0, (int)state.texLevelOffset);
		cfg.level0 = std::min(offset >> 4, maxLevel);
		cfg.level1 = std::min(cfg.level0 + ((offset & 15) && state.mipFilt ? 1 : 0), maxLevel);
	}
	cfg.clampS = state.samplerID.clampS;
	cfg.clampT = state.samplerID.clampT;
	cfg.linear = state.minFilt || state.magFilt;
	cfg.lo = UINT32_MAX;
	cfg.hi = 0;
	for (int i = 0; i <= maxLevel; ++i) {
		cfg.w[i] = (uint16_t)state.samplerID.cached.sizes[i].w;
		cfg.h[i] = (uint16_t)state.samplerID.cached.sizes[i].h;
		Source &src = cfg.src[i];
		src.addr = MaskAddress(state.texaddr[i]);
		src.swizzled = state.samplerID.swizzle;
		src.mirror = Memory::DepthMirrorsActive() && Memory::IsDepthTexVRAMAddress(state.texaddr[i]) ? (state.texaddr[i] & 0x00600000) : 0;
		const GETextureFormat fmt = state.samplerID.TexFmt();
		src.dxt = fmt == GE_TFMT_DXT1 || fmt == GE_TFMT_DXT3 || fmt == GE_TFMT_DXT5 ? (uint8_t)fmt : 0;
		uint32_t lo = src.addr, hi;
		if (src.dxt) {
			src.strideBytes = (state.texbufw[i] / 4) * DXTBlockBytes(src.dxt);
			hi = src.addr + (uint32_t)((cfg.h[i] + 7) / 4) * src.strideBytes;
		} else {
			src.strideBytes = state.texbufw[i] * cfg.bits / 8;
			hi = src.addr + (uint32_t)((cfg.h[i] + 7) & ~7) * src.strideBytes + (uint32_t)cfg.w[i] * cfg.bits / 8;
		}
		if (src.mirror) {
			lo &= ~0xFFFFu;
			hi = (hi + 0xFFFF) & ~0xFFFFu;
		}
		cfg.lo = std::min(cfg.lo, lo);
		cfg.hi = std::max(cfg.hi, hi);
	}
	return true;
}

// The vertices' texture coordinates bound what a primitive samples (perspective correct interpolation stays
// within them).
bool TexCache::MakeBounds(const BinItem &item, const Rasterizer::RasterizerState &state, Bounds &b) {
	int count;
	switch (item.type) {
	case BinItemType::TRIANGLE: count = 3; break;
	case BinItemType::RECT: case BinItemType::SPRITE: case BinItemType::LINE: count = 2; break;
	case BinItemType::POINT: count = 1; break;
	default: return false;
	}
	const VertexData *vs[3] = { &item.v0, &item.v1, &item.v2 };
	b.bounded = true;
	for (int i = 0; i < count; ++i) {
		float s = vs[i]->texturecoords.s(), t = vs[i]->texturecoords.t();
		if (state.textureProj) {
			const float q = vs[i]->texturecoords.q();
			if (!(q > 0.0f)) {
				b.bounded = false;
				return true;
			}
			s /= q;
			t /= q;
		}
		if (i == 0) {
			b.smin = b.smax = s;
			b.tmin = b.tmax = t;
		} else {
			b.smin = std::min(b.smin, s); b.smax = std::max(b.smax, s);
			b.tmin = std::min(b.tmin, t); b.tmax = std::max(b.tmax, t);
		}
	}
	if (state.throughMode) {
		// Through mode coordinates are level 0 texels.
		const float sw = 1.0f / (float)state.samplerID.cached.sizes[0].w, th = 1.0f / (float)state.samplerID.cached.sizes[0].h;
		b.smin *= sw; b.smax *= sw;
		b.tmin *= th; b.tmax *= th;
	}
	if (!(b.smin >= -4096.0f && b.smax <= 4096.0f && b.tmin >= -4096.0f && b.tmax <= 4096.0f))
		b.bounded = false;
	return true;
}

// Which lines a primitive can read, per level: its coordinate bounds widened by a texel for bilinear
// filtering, then clamped or wrapped as the sampler does.
void TexCache::MakeFootprint(const Config &cfg, const Bounds &b, std::vector<Footprint> &out) {
	out.clear();
	// The texels the samples can reach, as segments within the texture after clamping or wrapping. The far
	// coordinate is exclusive: samples are at pixel centers inside the primitive.
	struct Segment {
		int first, last;
	};
	auto segments = [&](float lo, float hi, int size, bool clamp, Segment seg[2]) -> int {
		const int texels = std::min(size, 512);
		int first, last;
		if (!b.bounded) {
			first = 0;
			last = texels - 1;
		} else {
			lo *= size;
			hi *= size;
			if (cfg.linear) {
				first = (int)floorf(lo - 0.5f);
				last = std::max(first + 1, (int)ceilf(hi - 0.5f));
			} else {
				first = (int)floorf(lo);
				last = std::max(first, (int)ceilf(hi) - 1);
			}
		}
		if (clamp) {
			seg[0] = Segment{ std::clamp(first, 0, texels - 1), std::clamp(last, 0, texels - 1) };
			return 1;
		}
		if (last - first + 1 >= texels) {
			seg[0] = Segment{ 0, texels - 1 };
			return 1;
		}
		first &= texels - 1;
		last &= texels - 1;
		if (first <= last) {
			seg[0] = Segment{ first, last };
			return 1;
		}
		seg[0] = Segment{ first, texels - 1 };
		seg[1] = Segment{ 0, last };
		return 2;
	};
	for (int level = cfg.level0; level <= cfg.level1; ++level) {
		Segment us[2], vs[2];
		const int nu = segments(b.smin, b.smax, cfg.w[level], cfg.clampS, us);
		const int nv = segments(b.tmin, b.tmax, cfg.h[level], cfg.clampT, vs);
		for (int j = 0; j < nv; ++j) {
			Footprint f{ level, nu, {}, {}, vs[j].first >> 3, vs[j].last >> 3 };
			for (int i = 0; i < nu; ++i) {
				f.col0[i] = us[i].first * cfg.bits / 128;
				f.col1[i] = us[i].last * cfg.bits / 128;
			}
			out.push_back(f);
		}
	}
}

// In raster order: band by band, and along each band left to right (across a wrap too).
void TexCache::Step(const Config &cfg, const std::vector<Footprint> &fp) {
	for (const Footprint &f : fp) {
		for (int band = f.band0; band <= f.band1; ++band) {
			for (int s = 0; s < f.segments; ++s) {
				for (int col = f.col0[s]; col <= f.col1[s]; ++col)
					Touch(cfg.bits, f.level, col, band, cfg.src[f.level]);
			}
		}
	}
}

void TexCache::ReplayLog() {
	if (log_.empty())
		return;
	std::vector<Footprint> fp;
	for (const Logged &l : log_) {
		const Config &cfg = configs_[l.config];
		MakeFootprint(cfg, l.bounds, fp);
		Step(cfg, fp);
	}
	// Coherent: nothing logged could read anything stale.
	delivered_.clear();
	log_.clear();
	configs_.clear();
	loggedSerial_ = 0;
}

bool TexCache::Prepare(const BinItem &item, const Rasterizer::RasterizerState &state) {
	delivered_.clear();
	footprintValid_ = false;
	currentValid_ = false;
	if (!state.enableTextures)
		return false;
	if (state.serial != configSerial_ || state.serial == 0) {
		configSerial_ = state.serial;
		configOk_ = MakeConfig(state, current_);
	}
	if (!configOk_ || !MakeBounds(item, state, bounds_))
		return false;
	currentValid_ = true;
	return true;
}

bool TexCache::Access() {
	if (!currentValid_)
		return false;
	if (coherent_) {
		if (!epochStarted_) {
			epoch_ = current_;
			epochStarted_ = true;
			epochSerial_ = 0;
		}
		if (epochSerial_ != configSerial_ || configSerial_ == 0) {
			sameEpoch_ = current_.SameSources(epoch_);
			epochSerial_ = configSerial_;
		}
		if (sameEpoch_) {
			if (configs_.empty() || loggedSerial_ != configSerial_ || configSerial_ == 0) {
				if (configs_.size() >= 0xFFFF)
					ReplayLog();
				configs_.push_back(current_);
				loggedSerial_ = configSerial_;
			}
			log_.push_back(Logged{ (uint16_t)(configs_.size() - 1), bounds_ });
			if (log_.size() >= 4096)
				ReplayLog();
			return false;
		}
		// Another texture: from now on hits can give the first one's bytes.
		ReplayLog();
		coherent_ = false;
	}
	MakeFootprint(current_, bounds_, footprint_);
	footprintValid_ = true;
	Step(current_, footprint_);
	return !delivered_.empty();
}

bool TexCache::FootprintReads(uint32_t base, int bpp, int stride, int x1, int y1, int x2, int y2) {
	const uint32_t strideBytes = (uint32_t)stride * bpp;
	if (strideBytes == 0 || !currentValid_)
		return false;
	const uint32_t rectStart = base + (uint32_t)(y1 * stride + x1) * bpp;
	const uint32_t rectEnd = base + (uint32_t)(y2 * stride + x2 + 1) * bpp;
	if (rectEnd <= current_.lo || rectStart >= current_.hi)
		return false;
	if (!footprintValid_) {
		MakeFootprint(current_, bounds_, footprint_);
		footprintValid_ = true;
	}
	// Whether bytes [a, a + n) are inside the rectangle's rows and columns.
	auto inRect = [&](uint32_t a, uint32_t n) {
		if (a + n <= rectStart || a >= rectEnd)
			return false;
		const uint32_t off = a - base;
		const int row = (int)(off / strideBytes);
		const uint32_t col0 = off % strideBytes, col1 = col0 + n - 1;
		if (row < y1 || row > y2)
			return col1 >= strideBytes;  // Runs into the next row.
		return col1 >= (uint32_t)x1 * bpp && col0 < (uint32_t)(x2 + 1) * bpp;
	};
	for (const Footprint &f : footprint_) {
		const Source &src = current_.src[f.level];
		for (int s = 0; s < f.segments; ++s) {
			// The whole footprint's span first; then line by line, row by row.
			uint32_t s0, e0, s1, e1;
			MemoryRange(src, f.col0[s], f.band0, s0, e0);
			MemoryRange(src, f.col1[s], f.band1, s1, e1);
			if (std::max(e0, e1) <= rectStart || std::min(s0, s1) >= rectEnd)
				continue;
			// Through a depth mirror, any byte of the pages may be one it reads.
			if (src.mirror)
				return true;
			for (int band = f.band0; band <= f.band1; ++band) {
				for (int col = f.col0[s]; col <= f.col1[s]; ++col) {
					uint32_t ls, le;
					LineRange(src, col, band, ls, le);
					if (le <= rectStart || ls >= rectEnd)
						continue;
					if (src.swizzled) {
						if (inRect(ls, 128))
							return true;
						continue;
					}
					for (int r = 0; r < 8; ++r) {
						if (inRect(ls + r * src.strideBytes, 16))
							return true;
					}
				}
			}
		}
	}
	return false;
}

void TexCache::Image(Rasterizer::RasterizerState &state) {
	if (current_.src[0].dxt) {
		DecodedImage(state);
		return;
	}
	CopyLevels(state);
	for (const Delivered &line : delivered_)
		PutLine(line.level, line.col, line.band, line.data);
}

void TexCache::CopyLevels(Rasterizer::RasterizerState &state) {
	for (int level = 0; level <= state.maxTexLevel; ++level) {
		const Source &cur = current_.src[level];
		const int w = state.samplerID.cached.sizes[level].w;
		const int h = state.samplerID.cached.sizes[level].h;
		// What the sampler can address: whole bands, and a row's texels past the stride.
		const uint32_t rows = (uint32_t)((h + 7) & ~7);
		const uint32_t bytes = rows * cur.strideBytes + (uint32_t)std::max(0, w * current_.bits / 8 - (int)cur.strideBytes) + 16;
		std::vector<uint8_t> &img = images_[level];
		img.resize(bytes);
		CopyFrom(cur, cur.addr, img.data(), bytes);
		state.texptr[level] = img.data();
	}
}

void TexCache::PutLine(int level, int col, int band, const uint8_t data[128]) {
	const Source &cur = current_.src[level];
	std::vector<uint8_t> &img = images_[level];
	uint32_t start, end;
	LineRange(cur, col, band, start, end);
	if (cur.swizzled) {
		const uint32_t off = start - cur.addr;
		if (off + 128 <= img.size())
			memcpy(img.data() + off, data, 128);
		return;
	}
	for (int r = 0; r < 8; ++r) {
		const uint32_t off = start - cur.addr + r * cur.strideBytes;
		if (off + 16 <= img.size())
			memcpy(img.data() + off, data + r * 16, 16);
	}
}

void TexCache::BeginSerial(Rasterizer::RasterizerState &state) {
	CopyLevels(state);
	unflushedCount_ = 0;
	// A new generation: no line is in the copies yet.
	if (++serialGen_ == 0) {
		for (auto &set : lines_) {
			for (Line &line : set)
				line.viewGen = 0;
		}
		serialGen_ = 1;
	}
}

void TexCache::Defer() {
	ReplayLog();
	delivered_.clear();
}

void TexCache::PushUnflushed(uint32_t addr, const uint8_t old[16]) {
	_dbg_assert_(unflushedCount_ < MAX_UNFLUSHED);
	Unflushed &u = unflushed_[unflushedCount_++];
	u.addr = MaskAddress(addr);
	memcpy(u.old, old, 16);
}

void TexCache::RemoveUnflushed(int index) {
	if (index < 0 || index >= unflushedCount_)
		return;
	memmove(unflushed_ + index, unflushed_ + index + 1, sizeof(Unflushed) * (unflushedCount_ - index - 1));
	unflushedCount_--;
}

void TexCache::SerialRead(int level, int u, int v) {
	const int col = u * current_.bits / 128, band = v >> 3;
	Line *line = Find(current_.bits, level, col, band);
	if (line) {
		line->lru = ++clock_;
		if (line->viewGen != serialGen_) {
			uint8_t data[128];
			Bytes(*line, data);
			PutLine(level, col, band, data);
			line->viewGen = serialGen_;
		}
		return;
	}
	// Loaded now: memory as it is, but for the pixels still in the output block.
	const Source &src = current_.src[level];
	line = Load(current_.bits, level, col, band, src);
	ReadLine(src, col, band, line->data);
	uint32_t start, end;
	LineRange(src, col, band, start, end);
	for (int k = 0; k < unflushedCount_; ++k) {
		const Unflushed &u = unflushed_[k];
		if (end <= u.addr || start >= u.addr + 16)
			continue;
		auto patch = [&](uint32_t addr, uint8_t *dst, uint32_t n) {
			const uint32_t lo = std::max(addr, u.addr), hi = std::min(addr + n, u.addr + 16);
			if (lo < hi)
				memcpy(dst + (lo - addr), u.old + (lo - u.addr), hi - lo);
		};
		if (src.swizzled) {
			patch(start, line->data, 128);
		} else {
			for (int r = 0; r < 8; ++r)
				patch(start + r * src.strideBytes, line->data + r * 16, 16);
		}
	}
	line->captured = true;
	coherent_ = false;
	PutLine(level, col, band, line->data);
	line->viewGen = serialGen_;
}

// The cache holds DXT decoded, so a stale line is 8888 texels: the levels are decoded to 8888, the lines laid
// over that, and the state switched to sample 8888.
void TexCache::DecodedImage(Rasterizer::RasterizerState &state) {
	for (int level = 0; level <= state.maxTexLevel; ++level) {
		const Source &cur = current_.src[level];
		const int w = state.samplerID.cached.sizes[level].w;
		const int h = state.samplerID.cached.sizes[level].h;
		const int cols = (std::max(w, (int)state.texbufw[level]) + 3) / 4;
		const int bands = (h + 7) / 8;
		const uint32_t strideBytes = cols * 16;
		std::vector<uint8_t> &img = images_[level];
		img.assign(strideBytes * bands * 8 + 16, 0);
		uint8_t line[128];
		for (int band = 0; band < bands; ++band) {
			for (int col = 0; col < cols; ++col) {
				ReadLine(cur, col, band, line);
				for (int r = 0; r < 8; ++r)
					memcpy(img.data() + (band * 8 + r) * strideBytes + col * 16, line + r * 16, 16);
			}
		}
		for (const Delivered &d : delivered_) {
			if (d.level != level || d.col >= cols || d.band >= bands)
				continue;
			for (int r = 0; r < 8; ++r)
				memcpy(img.data() + (d.band * 8 + r) * strideBytes + d.col * 16, d.data + r * 16, 16);
		}
		state.texptr[level] = img.data();
		state.texbufw[level] = (uint16_t)(cols * 4);
	}
	state.samplerID.texfmt = GE_TFMT_8888;
	state.samplerID.swizzle = false;
	state.samplerID.useStandardBufw = false;
	state.samplerID.hasInvalidPtr = false;
	state.samplerID.overReadSafe = true;
	// No binner: a sampler not compiled yet falls back to C++ (compiling here would flush mid draw).
	state.linear = Sampler::GetLinearFunc(state.samplerID, nullptr);
	state.nearest = Sampler::GetNearestFunc(state.samplerID, nullptr);
	if (g_Config.iTexFiltering == TEX_FILTER_FORCE_LINEAR) {
		state.nearest = state.linear;
	} else if (g_Config.iTexFiltering == TEX_FILTER_FORCE_NEAREST) {
		state.linear = state.nearest;
	}
	state.linearQuad = Sampler::GetLinearQuadFunc(state.samplerID, state.linear);
}
