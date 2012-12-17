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

#include <map>

class ParamSFOData
{
public:
	void SetValue(std::string key, unsigned int value, int max_size);
	void SetValue(std::string key, std::string value, int max_size);

	int GetValueInt(std::string key);
	std::string GetValueString(std::string key);

	bool ReadSFO(const u8 *paramsfo, size_t size);
	bool WriteSFO(u8 **paramsfo, size_t *size);
private:

	enum ValueType
	{
		VT_INT,
		VT_UTF8,
		VT_UTF8_SPE
	};
	struct ValueData
	{
		ValueType type;
		int max_size;
		std::string s_value;
		int i_value;
	};

	std::map<std::string,ValueData> values;
};

