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

#include "Common/CommonTypes.h"
#include "../Globals.h"

class ParamSFOData
{
public:
	void SetValue(std::string key, unsigned int value, int max_size);
	void SetValue(std::string key, std::string value, int max_size);
	void SetValue(std::string key, const u8* value, unsigned int size, int max_size);

	int GetValueInt(std::string key);
	std::string GetValueString(std::string key);
	u8* GetValueData(std::string key, unsigned int *size);

	bool ReadSFO(const u8 *paramsfo, size_t size);
	bool WriteSFO(u8 **paramsfo, size_t *size);

	int GetDataOffset(const u8 *paramsfo, std::string dataName);

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
		ValueType type;
		int max_size;
		std::string s_value;
		int i_value;

		u8* u_value;
		unsigned int u_size;

		void SetData(const u8* data, int size)
		{
			if(u_value)
			{
				delete[] u_value;
				u_value = 0;
			}
			if(size > 0)
			{
				u_value = new u8[size];
				memcpy(u_value, data, size);
			}
			u_size = size;
		}

		ValueData()
		{
			u_value = 0;
			u_size = 0;
			type = VT_INT;
			max_size = 0;
			i_value = 0;
		}

		~ValueData()
		{
			if(u_value)
				delete[] u_value;
		}
	};

	std::map<std::string,ValueData> values;
};

