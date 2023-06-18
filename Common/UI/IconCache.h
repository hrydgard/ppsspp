#pragma once

#include <map>
#include <string>
#include <cstdio>
#include <mutex>
#include <cstdint>

#include "Common/GPU/thin3d.h"

class UIContext;

enum class IconFormat : uint32_t {
	PNG,
};

// TODO: Possibly make this smarter and use instead of ManagedTexture?

class IconCache {
public:
	bool BindIconTexture(UIContext *context, const std::string &key);

	// It's okay to call this from any thread.
	bool InsertIcon(const std::string &key, IconFormat format, std::string &&pngData);

	void SaveToFile(FILE *file);
	bool LoadFromFile(FILE *file);

	void ClearTextures();
	void ClearData();

private:
	struct Entry {
		std::string data;
		IconFormat format;
		Draw::Texture *texture;
		double insertedTimeStamp;
		double usedTimeStamp;
		bool badData;
	};

	void Decimate();

	std::map<std::string, Entry> cache_;

	std::mutex lock_;
};

extern IconCache g_iconCache;
