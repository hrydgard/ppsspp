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

#include <string_view>
#include <map>
#include <vector>

#include "Common/CommonTypes.h"
#include "Common/Log.h"

class Path;

class ParamSFOData {
public:
	void SetValue(std::string_view key, unsigned int value, int max_size);
	void SetValue(std::string_view key, std::string_view value, int max_size);
	void SetValue(std::string_view key, const u8 *value, unsigned int size, int max_size);

	int GetValueInt(std::string_view key) const;
	std::string GetValueString(std::string_view key) const;  // Common keys: "TITLE", "DISC_VERSION"
	bool HasKey(std::string_view key) const;
	const u8 *GetValueData(std::string_view key, unsigned int *size) const;

	std::vector<std::string> GetKeys() const;
	std::string GenerateFakeID(const Path &filename) const;

	std::string GetDiscID();

	// This allocates a buffer (*paramsfo) using new[], whose size is zero-filled up to a multiple of 16 bytes.
	// This is required for SavedataParam::BuildHash.
	void WriteSFO(u8 **paramsfo, size_t *size) const;

	bool ReadSFO(const u8 *paramsfo, size_t size);
	bool ReadSFO(const std::vector<u8> &paramsfo) {
		if (!paramsfo.empty()) {
			return ReadSFO(&paramsfo[0], paramsfo.size());
		} else {
			return false;
		}
	}

	// If not found, returns a negative value.
	int GetDataOffset(const u8 *paramsfo, const char *dataName);

	bool IsValid() const { return !values.empty(); }
	void Clear();

	enum ValueType {
		VT_INT,
		VT_UTF8,
		VT_UTF8_SPE	// raw data in u8
	};

	class ValueData {
	public:
		ValueType type = VT_INT;
		int max_size = 0;  // Is this meaningful for non-strings?
		std::string s_value;
		int i_value = 0;

		u8* u_value = nullptr;
		unsigned int u_size = 0;

		void SetData(const u8* data, int size);

		~ValueData() {
			delete[] u_value;
		}
	};

	// ImDebugger access to the map.
	const std::map<std::string, ValueData, std::less<>> &Values() {
		return values;
	}

	static const char *ValueTypeToString(ValueType t) {
		switch (t) {
		case ParamSFOData::VT_INT: return "INT";
		case ParamSFOData::VT_UTF8: return "UTF8";
		case ParamSFOData::VT_UTF8_SPE: return "UTF8_SPE";
		default: return "N/A";
		}
	}

private:
	std::map<std::string, ValueData, std::less<>> values;
};

// Utilities for parsing the information.

// Guessed from GameID, not necessarily accurate
// Can't change the order of these.
enum class GameRegion {
	JAPAN,
	USA,
	EUROPE,
	HONGKONG,
	ASIA,
	KOREA,
	HOMEBREW,
	UNKNOWN,
	FLAG_COUNT,
	INTERNAL = FLAG_COUNT,
	TEST,
	DIAGNOSTIC,
};

GameRegion DetectGameRegionFromID(std::string_view id_version);
std::string_view GameRegionToString(GameRegion region);  // These strings can be looked up I18NCat::GAME.
