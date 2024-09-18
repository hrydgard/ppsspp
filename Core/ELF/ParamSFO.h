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

#include <string>
#include <map>
#include <vector>

#include "Common/CommonTypes.h"
#include "Common/Log.h"

class Path;

class ParamSFOData
{
public:
	void SetValue(const std::string &key, unsigned int value, int max_size);
	void SetValue(const std::string &key, const std::string &value, int max_size);
	void SetValue(const std::string &key, const u8 *value, unsigned int size, int max_size);

	int GetValueInt(const std::string &key) const;
	std::string GetValueString(const std::string &key) const;
	bool HasKey(const std::string &key) const;
	const u8 *GetValueData(const std::string &key, unsigned int *size) const;

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

	void Clear();

private:
	enum ValueType
	{
		VT_INT,
		VT_UTF8,
		VT_UTF8_SPE	// raw data in u8
	};

	class ValueData
	{
	public:
		ValueType type = VT_INT;
		int max_size = 0;
		std::string s_value;
		int i_value = 0;

		u8* u_value = nullptr;
		unsigned int u_size = 0;

		void SetData(const u8* data, int size);

		~ValueData() {
			delete[] u_value;
		}
	};

	std::map<std::string,ValueData> values;
};

