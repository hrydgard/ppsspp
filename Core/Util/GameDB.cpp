#include <cstdint>

#include "Core/Util/GameDB.h"
#include "Common/Log.h"
#include "Common/File/VFS/VFS.h"
#include "Common/StringUtils.h"

GameDB g_gameDB;

static const char *const g_gameDBFilename = "redump.csv";

static void SplitCSVLine(const std::string_view str, std::vector<std::string_view> &result) {
	result.clear();

	int indexCommaToLeftOfColumn = 0;
	int indexCommaToRightOfColumn = -1;

	bool inQuote = false;

	for (int i = 0; i < static_cast<int>(str.size()); i++) {
		if (str[i] == '\"') {
			inQuote = !inQuote;
		} else if (str[i] == ',' && !inQuote) {
			indexCommaToLeftOfColumn = indexCommaToRightOfColumn;
			indexCommaToRightOfColumn = i;
			int index = indexCommaToLeftOfColumn + 1;
			int length = indexCommaToRightOfColumn - index;
			std::string_view column(str.data() + index, length);
			// Remove quotes if possible
			column = StripQuotes(column);
			result.push_back(column);
		}
	}

	const std::string_view finalColumn(str.data() + indexCommaToRightOfColumn + 1, str.size() - indexCommaToRightOfColumn - 1);
	result.push_back(finalColumn);
}

static void SplitSV(std::string_view strv, char delim, bool removeWhiteSpace, std::vector<std::string_view> *output) {
	size_t first = 0;
	while (first < strv.size()) {
		const auto second = strv.find(delim, first);
		if (first != second) {
			std::string_view line = strv.substr(first, second - first);
			_dbg_assert_(!line.empty());
			if (line.back() == '\r') {
				line.remove_suffix(1);
			}
			if (removeWhiteSpace) {
				line = StripSpaces(line);
			}
			if (!line.empty()) {
				output->emplace_back(line);
			}
		}
		if (second == std::string_view::npos)
			break;
		first = second + 1;
	}
}

void GameDB::LoadIfNeeded() {
	if (loaded_) {
		// Already loaded
		return;
	}

	loaded_ = true;

	size_t size;
	uint8_t *data = g_VFS.ReadFile(g_gameDBFilename, &size);
	if (!data)
		return;

	contents_ = std::string((const char *)data, size);
	delete[] data;

	const size_t RESERVE_COUNT = 2820;  // ~ known current line count

	// Split the string into views of each line, keeping the original.
	std::vector<std::string_view> lines;
	lines.reserve(RESERVE_COUNT);
	SplitSV(contents_, '\n', false, &lines);
	SplitCSVLine(lines[0], columns_);

	const size_t titleColumn = GetColumnIndex("Title");
	const size_t foreignTitleColumn = GetColumnIndex("Foreign Title");
	const size_t serialColumn = GetColumnIndex("Serial");
	const size_t crcColumn = GetColumnIndex("CRC32");
	const size_t sizeColumn = GetColumnIndex("Size");

	std::vector<std::string_view> items;
	items.reserve(8);
	lines_.reserve(RESERVE_COUNT);
	for (size_t i = 1; i < lines.size(); i++) {
		auto &lineString = lines[i];
		SplitCSVLine(lineString, items);
		if (items.size() != columns_.size()) {
			// Bad line
			ERROR_LOG(Log::System, "Bad line in CSV file: %s", std::string(lineString).c_str());
			continue;
		}

		Line line;
		line.title = items[titleColumn];
		line.foreignTitle = items[foreignTitleColumn];
		SplitSV(items[serialColumn], ',', true, &line.serials);
		line.crc = items[crcColumn];
		line.size = items[sizeColumn];
		lines_.push_back(line);
	}
}

size_t GameDB::GetColumnIndex(std::string_view name) const {
	for (size_t i = 0; i < columns_.size(); i++) {
		if (name == columns_[i]) {
			return i;
		}
	}
	return (size_t)-1;
}

// Our IDs are ULUS12345, while the DB has them in some different forms, with a space or dash as separator.
// TODO: report to redump
static bool IDMatches(std::string_view id, std::string_view dbId) {
	if (id.size() < 9 || dbId.size() < 10)
		return false;
	if (id.substr(0, 4) != dbId.substr(0, 4))
		return false;
	if (id.substr(4, 5) != dbId.substr(5, 5))
		return false;
	return true;
}

bool GameDB::GetGameInfos(std::string_view id, std::vector<GameDBInfo> *infos) {
	if (id.size() < 9) {
		// Not a game.
		return false;
	}

	std::lock_guard<std::mutex> guard(loadMutex_);

	LoadIfNeeded();

	for (auto &line : lines_) {
		for (auto &serial : line.serials) {
			// Ignore version and stuff for now
			if (IDMatches(id, serial)) {
				GameDBInfo info;
				// zero-terminate before sscanf
				std::string crc(line.crc);
				if (1 != sscanf(crc.c_str(), "%08x", &info.crc)) {
					continue;
				}
				std::string size(line.size);
				if (1 != sscanf(size.c_str(), "%llu", (long long *)&info.size)) {
					continue;
				}
				info.title = line.title;
				info.foreignTitle = line.foreignTitle;
				infos->push_back(info);
			}
		}
	}

	return !infos->empty();
}
