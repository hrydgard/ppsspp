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

// How long an icon is kept in the saved cache, in seconds. It's downloaded again after that.
constexpr double ICON_MAX_AGE_DEFAULT = 30 * 24 * 60 * 60.0;  // Most icons, like achievement badges, don't change.
constexpr double ICON_MAX_AGE_AVATAR = 24 * 60 * 60.0;  // User avatars can change at any time.

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
	// Like CancelPending, but MarkPending refuses the key for a while, so a failing download isn't retried every frame.
	void MarkFailed(std::string_view key);
	bool InsertIcon(std::string_view key, IconFormat format, std::string &&pngData, double maxAge = ICON_MAX_AGE_DEFAULT);
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
		double expireTimeStamp;  // Wall-clock (time_now_unix_utc), since it's saved.
		double usedTimeStamp;
		double uploadFailedTime;  // When CreateTexture last failed, or 0.
		int width;  // From the image header, 0 if unknown.
		int height;
	};

	std::map<std::string, Entry, std::less<>> cache_;
	std::set<std::string, std::less<>> pending_;
	std::map<std::string, double, std::less<>> failed_;  // key -> time of failure

	std::mutex lock_;

	double lastUpdate_ = 0.0;
	double lastDecimate_ = 0.0;
};

extern IconCache g_iconCache;
