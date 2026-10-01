#include <algorithm>
#include <cstring>

#include "Common/UI/IconCache.h"
#include "Common/UI/Context.h"
#include "Common/TimeUtil.h"
#include "Common/Data/Format/PNGLoad.h"
#include "Common/Log.h"
#include "Common/GPU/thin3d.h"
#include "Common/File/FileUtil.h"

// 3: Entries store a wall-clock expiry time.
#define ICON_CACHE_VERSION 3
#define MK_FOURCC(str) (str[0] | ((uint8_t)str[1] << 8) | ((uint8_t)str[2] << 16) | ((uint8_t)str[3] << 24))

#define MAX_RUNTIME_CACHE_SIZE (1024 * 1024 * 4)
#define MAX_SAVED_CACHE_SIZE (1024 * 1024 * 1)

// Seconds before MarkPending accepts a key whose download failed.
constexpr double FAILED_RETRY_DELAY = 30.0;
// Seconds before BindIconTexture tries again to create a texture that failed.
constexpr double UPLOAD_RETRY_DELAY = 5.0;

constexpr uint32_t ICON_CACHE_MAGIC = MK_FOURCC("pICN");

IconCache g_iconCache;

struct DiskCacheHeader {
	uint32_t magic;
	uint32_t version;
	uint32_t entryCount;
};

struct DiskCacheEntry {
	uint32_t keyLen;
	uint32_t dataLen;
	IconFormat format;
	uint32_t padding;  // Explicit, so that 32-bit x86 Linux (which aligns double to 4) has the same layout.
	double expireTimestamp;
};
static_assert(sizeof(DiskCacheEntry) == 24, "DiskCacheEntry is written to disk as is");

// Reads the size from the header, so layout can use it before there's a texture. 0x0 if unknown.
static void PeekIconSize(const std::string &data, IconFormat format, int *width, int *height) {
	*width = 0;
	*height = 0;
	if (format == IconFormat::PNG && data.size() >= sizeof(PNGHeaderPeek)) {
		PNGHeaderPeek peek;
		memcpy(&peek, data.data(), sizeof(peek));
		if (peek.IsValidPNGHeader()) {
			*width = peek.Width();
			*height = peek.Height();
		}
	}
}

void IconCache::SaveToFile(FILE *file) {
	std::unique_lock<std::mutex> lock(lock_);

	// First, compute the total size. If above a threshold, remove until under.
	Decimate(MAX_SAVED_CACHE_SIZE);

	DiskCacheHeader header{};
	header.magic = ICON_CACHE_MAGIC;
	header.version = ICON_CACHE_VERSION;
	header.entryCount = (uint32_t)cache_.size();

	fwrite(&header, 1, sizeof(header), file);

	for (auto &iter : cache_) {
		DiskCacheEntry entryHeader{};
		entryHeader.keyLen = (uint32_t)iter.first.size();
		const auto &entry = iter.second;
		entryHeader.dataLen = (uint32_t)entry.data.size();
		entryHeader.format = entry.format;
		entryHeader.expireTimestamp = entry.expireTimeStamp;
		fwrite(&entryHeader, 1, sizeof(entryHeader), file);
		fwrite(iter.first.c_str(), 1, iter.first.size(), file);
		fwrite(entry.data.data(), 1, entry.data.size(), file);
	}
}

bool IconCache::LoadFromFile(FILE *file) {
	std::unique_lock<std::mutex> lock(lock_);

	DiskCacheHeader header{};
	if (fread(&header, 1, sizeof(header), file) != sizeof(DiskCacheHeader)) {
		return false;
	}
	if (header.magic != ICON_CACHE_MAGIC || header.version != ICON_CACHE_VERSION) {
		return false;
	}

	double now = time_now_d();
	double nowUnix = time_now_unix_utc();

	for (uint32_t i = 0; i < header.entryCount; i++) {
		DiskCacheEntry entryHeader{};
		if (fread(&entryHeader, 1, sizeof(entryHeader), file) != sizeof(entryHeader)) {
			break;
		}

		if (entryHeader.keyLen > 0x1000 || entryHeader.dataLen > MAX_SAVED_CACHE_SIZE) {
			// Let's say this is invalid, probably a corrupted file. Check before allocating.
			break;
		}

		std::string key;
		key.resize(entryHeader.keyLen, 0);

		if (fread(&key[0], 1, entryHeader.keyLen, file) != entryHeader.keyLen) {
			break;
		}

		// Skip it if we already have the entry somehow, or it has expired.
		if (cache_.find(key) != cache_.end() || nowUnix > entryHeader.expireTimestamp) {
			// Seek past the data and go to the next entry.
			File::Fseek(file, entryHeader.dataLen, SEEK_CUR);
			continue;
		}

		std::string data;
		data.resize(entryHeader.dataLen);
		size_t len = fread(&data[0], 1, entryHeader.dataLen, file);
		if (len != (size_t)entryHeader.dataLen) {
			// Stop reading and don't use this entry. Seems the file is truncated, but we'll recover.
			break;
		}

		Entry entry{};
		entry.data = data;
		entry.format = entryHeader.format;
		entry.expireTimeStamp = entryHeader.expireTimestamp;
		entry.usedTimeStamp = now;
		PeekIconSize(entry.data, entry.format, &entry.width, &entry.height);
		cache_.emplace(key, entry);
	}

	return true;
}

void IconCache::ClearTextures() {
	std::unique_lock<std::mutex> lock(lock_);
	for (auto &iter : cache_) {
		if (iter.second.texture) {
			iter.second.texture->Release();
			iter.second.texture = nullptr;
		}
	}
}

void IconCache::ClearData() {
	ClearTextures();
	std::unique_lock<std::mutex> lock(lock_);
	cache_.clear();
	failed_.clear();
}

void IconCache::FrameUpdate() {
	std::unique_lock<std::mutex> lock(lock_);
	// Remove old textures after a while.
	double now = time_now_d();
	if (now > lastUpdate_ + 2.0) {
		for (auto &iter : cache_) {
			auto &entry = iter.second;
			double useAge = now - entry.usedTimeStamp;
			if (useAge > 5.0) {
				// Release the texture after a few seconds of no use.
				// Still, keep the png data loaded, it's small.
				if (entry.texture) {
					entry.texture->Release();
					entry.texture = nullptr;
				}
			}
		}
		for (auto iter = failed_.begin(); iter != failed_.end(); ) {
			if (now > iter->second + FAILED_RETRY_DELAY) {
				iter = failed_.erase(iter);  // MarkPending would accept it anyway.
			} else {
				++iter;
			}
		}
		lastUpdate_ = now;
	}

	if (now > lastDecimate_ + 60.0) {
		Decimate(MAX_RUNTIME_CACHE_SIZE);
		lastDecimate_ = now;
	}
}

void IconCache::Decimate(int64_t maxSize) {
	// Call this under the lock.

	int64_t totalSize = 0;
	for (auto &iter : cache_) {
		totalSize += (int64_t)iter.second.data.size();
	}

	if (totalSize <= maxSize) {
		return;
	}

	// Create a list of all the entries, sort by date. Then delete until we reach the desired size.
	struct SortEntry {
		std::string key;
		double usedTimestamp;
		size_t size;
	};

	std::vector<SortEntry> sortEntries;
	sortEntries.reserve(cache_.size());
	for (const auto &iter : cache_) {
		const auto &entry = iter.second;
		sortEntries.push_back({ iter.first, entry.usedTimeStamp, entry.data.size() });
	}

	std::sort(sortEntries.begin(), sortEntries.end(), [](const SortEntry &a, const SortEntry &b) {
		// Oldest should be last in the lsit.
		return a.usedTimestamp > b.usedTimestamp;
	});

	while (totalSize > maxSize && !sortEntries.empty()) {
		totalSize -= (int64_t)sortEntries.back().size;
		auto iter = cache_.find(sortEntries.back().key);
		if (iter != cache_.end()) {
			if (iter->second.texture) {
				iter->second.texture->Release();
			}
			cache_.erase(iter);  // iter is recomputed above, so no need to set it.
		}
		sortEntries.pop_back();
	}
}

bool IconCache::GetDimensions(std::string_view key, int *width, int *height) {
	std::unique_lock<std::mutex> lock(lock_);
	auto iter = cache_.find(key);
	if (iter == cache_.end()) {
		// Don't have this entry.
		return false;
	}

	const auto &entry = iter->second;
	if (entry.width <= 0 || entry.height <= 0) {
		return false;
	}
	*width = entry.width;
	*height = entry.height;
	return true;
}

bool IconCache::Contains(std::string_view key) {
	std::unique_lock<std::mutex> lock(lock_);
	return cache_.find(key) != cache_.end();
}

bool IconCache::MarkPending(std::string_view key) {
	std::unique_lock<std::mutex> lock(lock_);
	if (cache_.find(key) != cache_.end()) {
		return false;
	}
	if (pending_.find(key) != pending_.end()) {
		return false;
	}
	auto failedIter = failed_.find(key);
	if (failedIter != failed_.end()) {
		if (time_now_d() < failedIter->second + FAILED_RETRY_DELAY) {
			return false;
		}
		failed_.erase(failedIter);
	}
	pending_.emplace(key);
	return true;
}

void IconCache::MarkFailed(std::string_view key) {
	std::unique_lock<std::mutex> lock(lock_);
	auto iter = pending_.find(key);
	if (iter != pending_.end()) {
		pending_.erase(iter);
	}
	failed_[std::string(key)] = time_now_d();
}

void IconCache::CancelPending(std::string_view key) {
	std::unique_lock<std::mutex> lock(lock_);
	auto iter = pending_.find(key);
	if (iter == pending_.end()) {
		ERROR_LOG(Log::System, "IconCache: CancelPending called for non-pending key: %.*s", STR_VIEW(key));
		return;
	}
	pending_.erase(iter);
}

bool IconCache::InsertIcon(std::string_view key, IconFormat format, std::string &&data, double maxAge) {
	if (key.empty()) {
		return false;
	}

	if (data.empty()) {
		_dbg_assert_(false);
		ERROR_LOG(Log::G3D, "Can't insert empty data into icon cache");
		return false;
	}

	std::unique_lock<std::mutex> lock(lock_);

	if (cache_.find(key) != cache_.end()) {
		// Already have this entry.
		return false;
	}

	if (data.size() > 1024 * 512) {
		WARN_LOG(Log::G3D, "Unusually large icon inserted in icon cache: %.*s (%d bytes)", STR_VIEW(key), (int)data.size());
	}

	auto iter = pending_.find(key);
	if (iter != pending_.end()) {
		pending_.erase(iter);
	}

	double now = time_now_d();
	Entry entry{ std::move(data), format, nullptr, time_now_unix_utc() + maxAge, now };
	PeekIconSize(entry.data, entry.format, &entry.width, &entry.height);
	cache_.emplace(key, std::move(entry));
	return true;
}

Draw::Texture *IconCache::BindIconTexture(UIContext *context, std::string_view key) {
	if (key.empty()) {
		return nullptr;
	}

	// TODO: Cut down on how long we're holding this lock here.
	std::unique_lock<std::mutex> lock(lock_);
	auto iter = cache_.find(key);
	if (iter == cache_.end()) {
		// Don't have this entry.
		return nullptr;
	}

	auto &entry = iter->second;

	if (entry.texture) {
		context->GetDrawContext()->BindTexture(0, entry.texture);
		entry.usedTimeStamp = time_now_d();
		return entry.texture;
	}

	if (entry.uploadFailedTime != 0.0 && time_now_d() < entry.uploadFailedTime + UPLOAD_RETRY_DELAY) {
		return nullptr;
	}

	// OK, don't have a texture. Upload it!
	int width = 0;
	int height = 0;
	Draw::DataFormat dataFormat;
	unsigned char *buffer = nullptr;

	switch (entry.format) {
	case IconFormat::PNG:
	{
		const std::string &data = entry.data;
		int result = pngLoadPtr((const unsigned char *)data.data(), data.size(), &width, &height, &buffer, 256, 128);

		if (result != 1) {
			ERROR_LOG(Log::G3D, "IconCache: Failed to load png (%d bytes) for key %.*s", (int)data.size(), STR_VIEW(key));
			// Drop it, so it isn't saved to disk, and MarkPending allows downloading it again later.
			failed_[std::string(key)] = time_now_d();
			cache_.erase(iter);
			return nullptr;
		}
		dataFormat = Draw::DataFormat::R8G8B8A8_UNORM;
		break;
	}
	default:
		return nullptr;
	}

	Draw::TextureDesc iconDesc{};
	iconDesc.width = width;
	iconDesc.height = height;
	iconDesc.depth = 1;
	iconDesc.initData.push_back((const uint8_t *)buffer);
	iconDesc.mipLevels = 1;
	iconDesc.swizzle = Draw::TextureSwizzle::DEFAULT;
	iconDesc.generateMips = false;
	iconDesc.tag = "icon";
	iconDesc.format = dataFormat;
	iconDesc.type = Draw::TextureType::LINEAR2D;

	Draw::Texture *texture = context->GetDrawContext()->CreateTexture(iconDesc);
	free(buffer);
	if (!texture) {
		ERROR_LOG(Log::G3D, "IconCache: Failed to create a %dx%d texture for key %.*s", width, height, STR_VIEW(key));
		entry.uploadFailedTime = time_now_d();
		return nullptr;
	}
	entry.texture = texture;
	entry.usedTimeStamp = time_now_d();
	// The caller draws with it right away, same as with an existing texture.
	context->GetDrawContext()->BindTexture(0, texture);
	return texture;
}

IconCacheStats IconCache::GetStats() {
	IconCacheStats stats{};

	std::unique_lock<std::mutex> lock(lock_);

	for (auto &iter : cache_) {
		stats.cachedCount++;
		const auto &entry = iter.second;
		if (entry.texture)
			stats.textureCount++;
		stats.dataSize += entry.data.size();
	}

	stats.pending = pending_.size();

	return stats;
}
