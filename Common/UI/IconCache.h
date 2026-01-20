#pragma once

#include <map>
#include <set>
#include <string_view>
#include <mutex>
#include <cstdint>

#include "Common/File/Path.h"


class UIContext;

enum class IconFormat : uint32_t {
	PNG,
};

namespace Draw {
class Texture;
}

// TODO: Possibly make this smarter and use instead of ManagedTexture?

struct IconCacheStats {
	size_t cachedCount;
	size_t textureCount;  // number of cached images that are "live" textures
	size_t pending;
	size_t dataSize;
};

class IconCache {
public:
	// NOTE: Don't store the returned texture. Only use it to look up dimensions or other properties,
	// instead call BindIconTexture every time you want to use it.
	Draw::Texture *BindIconTexture(UIContext *context, std::string_view key);

	// It's okay to call these from any thread.
	bool MarkPending(std::string_view key);  // returns false if already pending or loaded
	void CancelPending(std::string_view key);
	bool InsertIcon(std::string_view key, IconFormat format, std::string &&pngData);
	bool GetDimensions(std::string_view key, int *width, int *height);
	bool Contains(std::string_view key);

	void SaveToFile(FILE *file);
	bool LoadFromFile(FILE *file);

	void FrameUpdate();

	void ClearTextures();
	void ClearData();

	IconCacheStats GetStats();

private:
	void Decimate(int64_t maxSize);

	struct Entry {
		std::string data;
		IconFormat format;
		Draw::Texture *texture;
		double insertedTimeStamp;
		double usedTimeStamp;
		bool badData;
	};

	std::map<std::string, Entry, std::less<>> cache_;
	std::set<std::string, std::less<>> pending_;

	std::mutex lock_;

	double lastUpdate_ = 0.0;
	double lastDecimate_ = 0.0;
};

extern IconCache g_iconCache;
