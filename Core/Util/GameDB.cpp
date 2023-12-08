#include <cstdint>

#include "Core/Util/GameDB.h"
#include "Common/Log.h"
#include "Common/File/VFS/VFS.h"
#include "Common/StringUtils.h"

GameDB g_gameDB;

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

static std::vector<std::string_view> splitSV(std::string_view strv, char delim, bool removeWhiteSpace) {
	std::vector<std::string_view> output;
	size_t first = 0;
	while (first < strv.size()) {
		const auto second = strv.find(delim, first);
		if (first != second) {
			std::string_view line = strv.substr(first, second - first);
			if (line.back() == '\r') {
				line = strv.substr(first, second - first - 1);
			}
			if (removeWhiteSpace) {
				line = StripSpaces(line);
			}
			output.emplace_back(line);
		}
		if (second == std::string_view::npos)
			break;
		first = second + 1;
	}
	return output;
}

bool GameDB::LoadFromVFS(VFSInterface &vfs, const char *filename) {
	size_t size;
	uint8_t *data = vfs.ReadFile(filename, &size);
	if (!data)
		return false;
	contents_ = std::string((const char *)data, size);
	delete[] data;

	// Split the string into views of each line, keeping the original.
	std::vector<std::string_view> lines = splitSV(contents_, '\n', false);

	SplitCSVLine(lines[0], columns_);

	const size_t titleColumn = GetColumnIndex("Title");
	const size_t foreignTitleColumn = GetColumnIndex("Foreign Title");
	const size_t serialColumn = GetColumnIndex("Serial");
	const size_t crcColumn = GetColumnIndex("CRC32");
	const size_t sizeColumn = GetColumnIndex("Size");

	std::vector<std::string_view> items;
	for (size_t i = 1; i < lines.size(); i++) {
		auto &lineString = lines[i];
		SplitCSVLine(lineString, items);
		if (items.size() != columns_.size()) {
			// Bad line
			ERROR_LOG(SYSTEM, "Bad line in CSV file: %s", std::string(lineString).c_str());
			continue;
		}

		Line line;
		line.title = items[titleColumn];
		line.foreignTitle = items[foreignTitleColumn];
		line.serials = splitSV(items[serialColumn], ',', true);
		line.crc = items[crcColumn];
		line.size = items[sizeColumn];
		lines_.push_back(line);
	}
	return true;
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
	
	for (auto &line : lines_) {
		for (auto serial : line.serials) {
			// Ignore version and stuff for now
			if (IDMatches(id, serial)) {
				GameDBInfo info;
				if (1 != sscanf(line.crc.data(), "%08x", &info.crc)) {
					continue;
				}
				if (1 != sscanf(line.size.data(), "%llu", (long long *)&info.size)) {
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
