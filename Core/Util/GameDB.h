#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <string_view>

class VFSInterface;

// Serial/id doesn't need including here since we look up by it.
struct GameDBInfo {
	std::string title;
	std::string foreignTitle;
	uint32_t crc;
	uint64_t size;
};

class GameDB {
public:
	bool LoadFromVFS(VFSInterface &vfs, const char *filename);
	bool GetGameInfos(std::string_view id, std::vector<GameDBInfo> *infos);

private:
	size_t GetColumnIndex(std::string_view name) const;

	struct Line {
		// The exact same ISO can have multiple serials.
		std::vector<std::string_view> serials;
		// The below fields should match GameDBInfo.
		std::string_view title;
		std::string_view foreignTitle;
		std::string_view size;
		std::string_view crc;
	};

	std::string contents_;
	std::vector<Line> lines_;
	std::vector<std::string_view> columns_;
};

extern GameDB g_gameDB;
