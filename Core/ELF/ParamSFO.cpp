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

#include <cstdio>
#include <cstring>

#include "Common/CommonTypes.h"
#include "Common/Log.h"
#include "Common/StringUtils.h"
#include "Common/Swap.h"
#include "Core/ELF/ParamSFO.h"
#include "Core/Core.h"

struct Header
{
	u32_le magic; /* Always PSF */
	u32_le version; /* Usually 1.1 */
	u32_le key_table_start; /* Start position of key_table */
	u32_le data_table_start; /* Start position of data_table */
	u32_le index_table_entries; /* Number of entries in index_table*/
};

struct IndexTable
{
	u16_le key_table_offset; /* Offset of the param_key from start of key_table */
	u16_le param_fmt; /* Type of data of param_data in the data_table */
	u32_le param_len; /* Used Bytes by param_data in the data_table */
	u32_le param_max_len; /* Total bytes reserved for param_data in the data_table */
	u32_le data_table_offset; /* Offset of the param_data from start of data_table */
};

void ParamSFOData::SetValue(const std::string &key, unsigned int value, int max_size) {
	values[key].type = VT_INT;
	values[key].i_value = value;
	values[key].max_size = max_size;
}
void ParamSFOData::SetValue(const std::string &key, const std::string &value, int max_size) {
	values[key].type = VT_UTF8;
	values[key].s_value = value;
	values[key].max_size = max_size;
}

void ParamSFOData::SetValue(const std::string &key, const u8 *value, unsigned int size, int max_size) {
	values[key].type = VT_UTF8_SPE;
	values[key].SetData(value, size);
	values[key].max_size = max_size;
}

int ParamSFOData::GetValueInt(const std::string &key) const {
	std::map<std::string,ValueData>::const_iterator it = values.find(key);
	if (it == values.end() || it->second.type != VT_INT)
		return 0;
	return it->second.i_value;
}

std::string ParamSFOData::GetValueString(const std::string &key) const {
	std::map<std::string,ValueData>::const_iterator it = values.find(key);
	if (it == values.end() || (it->second.type != VT_UTF8))
		return "";
	return it->second.s_value;
}

bool ParamSFOData::HasKey(const std::string &key) const {
	return values.find(key) != values.end();
}

const u8 *ParamSFOData::GetValueData(const std::string &key, unsigned int *size) const {
	std::map<std::string,ValueData>::const_iterator it = values.find(key);
	if (it == values.end() || (it->second.type != VT_UTF8_SPE)) {
		return 0;
	}
	if (size) {
		*size = it->second.u_size;
	}
	return it->second.u_value;
}

std::vector<std::string> ParamSFOData::GetKeys() const {
	std::vector<std::string> result;
	for (const auto &pair : values) {
		result.push_back(pair.first);
	}
	return result;
}

std::string ParamSFOData::GetDiscID() {
	const std::string discID = GetValueString("DISC_ID");
	if (discID.empty()) {
		std::string fakeID = GenerateFakeID(Path());
		WARN_LOG(Log::Loader, "No DiscID found - generating a fake one: '%s' (from %s)", fakeID.c_str(), PSP_CoreParameter().fileToStart.c_str());
		ValueData data;
		data.type = VT_UTF8;
		data.s_value = fakeID;
		values["DISC_ID"] = data;
		return fakeID;
	}
	return discID;
}

// I'm so sorry Ced but this is highly endian unsafe :(
bool ParamSFOData::ReadSFO(const u8 *paramsfo, size_t size) {
	if (size < sizeof(Header))
		return false;
	const Header *header = (const Header *)paramsfo;
	if (header->magic != 0x46535000)
		return false;
	if (header->version != 0x00000101)
		WARN_LOG(Log::Loader, "Unexpected SFO header version: %08x", header->version);

	const IndexTable *indexTables = (const IndexTable *)(paramsfo + sizeof(Header));

	if (header->key_table_start > size || header->data_table_start > size) {
		return false;
	}

	const u8 *data_start = paramsfo + header->data_table_start;

	auto readStringCapped = [paramsfo, size](size_t offset, size_t maxLen) -> std::string {
		std::string str;
		while (offset < size) {
			char c = (char)(paramsfo[offset]);
			if (c) {
				str.push_back(c);
			} else {
				break;
			}
			offset++;
			if (maxLen != 0 && str.size() == maxLen)
				break;
		}
		return str;
	};

	for (u32 i = 0; i < header->index_table_entries; i++)
	{
		size_t key_offset = header->key_table_start + indexTables[i].key_table_offset;
		if (key_offset >= size) {
			return false;
		}

		size_t data_offset = header->data_table_start + indexTables[i].data_table_offset;
		if (data_offset >= size) {
			return false;
		}

		std::string key = readStringCapped(key_offset, 0);
		if (key.empty())
			continue;  // Likely ran into a truncated PARAMSFO.

		switch (indexTables[i].param_fmt) {
		case 0x0404:
			{
				if (data_offset + 4 > size)
					continue;
				// Unsigned int
				const u32_le *data = (const u32_le *)(paramsfo + data_offset);
				SetValue(key, *data, indexTables[i].param_max_len);
				VERBOSE_LOG(Log::Loader, "%s %08x", key.c_str(), *data);
			}
			break;
		case 0x0004:
			// Special format UTF-8
			{
				if (data_offset + indexTables[i].param_len > size)
					continue;
				const u8 *utfdata = (const u8 *)(paramsfo + data_offset);
				VERBOSE_LOG(Log::Loader, "%s %s", key.c_str(), utfdata);
				SetValue(key, utfdata, indexTables[i].param_len, indexTables[i].param_max_len);
			}
			break;
		case 0x0204:
			// Regular UTF-8
			{
				// TODO: Likely should use param_len here, but there's gotta be a reason we avoided it before.
				std::string str = readStringCapped(data_offset, indexTables[i].param_max_len);
				VERBOSE_LOG(Log::Loader, "%s %s", key.c_str(), str.c_str());
				SetValue(key, str, indexTables[i].param_max_len);
			}
			break;
		default:
			break;
		}
	}

	return true;
}

int ParamSFOData::GetDataOffset(const u8 *paramsfo, const char *dataName) {
	const Header *header = (const Header *)paramsfo;
	if (header->magic != 0x46535000)
		return -1;
	if (header->version != 0x00000101)
		WARN_LOG(Log::Loader, "Unexpected SFO header version: %08x", header->version);

	const IndexTable *indexTables = (const IndexTable *)(paramsfo + sizeof(Header));

	const u8 *key_start = paramsfo + header->key_table_start;
	int data_start = header->data_table_start;

	for (u32 i = 0; i < header->index_table_entries; i++)
	{
		const char *key = (const char *)(key_start + indexTables[i].key_table_offset);
		if (!strcmp(key, dataName))
		{
			return data_start + indexTables[i].data_table_offset;
		}
	}

	return -1;
}

void ParamSFOData::WriteSFO(u8 **paramsfo, size_t *size) const {
	size_t total_size = 0;
	size_t key_size = 0;
	size_t data_size = 0;

	Header header;
	header.magic = 0x46535000;
	header.version = 0x00000101;
	header.index_table_entries = 0;

	total_size += sizeof(Header);

	// Get size info
	for (const auto &[k, v] : values)
	{
		key_size += k.size() + 1;
		data_size += v.max_size;

		header.index_table_entries++;
	}

	// Padding
	while ((key_size % 4) != 0) key_size++;

	header.key_table_start = sizeof(Header) + header.index_table_entries * sizeof(IndexTable);
	header.data_table_start = header.key_table_start + (u32)key_size;

	total_size += sizeof(IndexTable) * header.index_table_entries;
	total_size += key_size;
	total_size += data_size;
	*size = total_size;

	size_t aligned_size = (total_size + 15) & ~15;
	u8* data = new u8[aligned_size];
	*paramsfo = data;
	memset(data, 0, aligned_size);
	memcpy(data, &header, sizeof(Header));

	// Now fill
	IndexTable *index_ptr = (IndexTable*)(data + sizeof(Header));
	u8* key_ptr = data + header.key_table_start;
	u8* data_ptr = data + header.data_table_start;

	for (const auto &[k, v] : values)
	{
		u16 offset = (u16)(key_ptr - (data+header.key_table_start));
		index_ptr->key_table_offset = offset;
		offset = (u16)(data_ptr - (data+header.data_table_start));
		index_ptr->data_table_offset = offset;
		index_ptr->param_max_len = v.max_size;
		if (v.type == VT_INT)
		{
			index_ptr->param_fmt = 0x0404;
			index_ptr->param_len = 4;

			*(s32_le *)data_ptr = v.i_value;
		}
		else if (v.type == VT_UTF8_SPE)
		{
			index_ptr->param_fmt = 0x0004;
			index_ptr->param_len = v.u_size;

			memset(data_ptr,0,index_ptr->param_max_len);
			memcpy(data_ptr,v.u_value,index_ptr->param_len);
		}
		else if (v.type == VT_UTF8)
		{
			index_ptr->param_fmt = 0x0204;
			index_ptr->param_len = (u32)v.s_value.size()+1;

			memcpy(data_ptr,v.s_value.c_str(),index_ptr->param_len);
			data_ptr[index_ptr->param_len] = 0;
		}

		memcpy(key_ptr,k.c_str(),k.size());
		key_ptr[k.size()] = 0;

		data_ptr += index_ptr->param_max_len;
		key_ptr += k.size() + 1;
		index_ptr++;

	}
}

void ParamSFOData::Clear() {
	values.clear();
}

void ParamSFOData::ValueData::SetData(const u8* data, int size) {
	if (u_value) {
		delete[] u_value;
		u_value = 0;
	}
	if (size > 0) {
		u_value = new u8[size];
		memcpy(u_value, data, size);
	}
	u_size = size;
}

std::string ParamSFOData::GenerateFakeID(const Path &filename) const {
	// Generates fake gameID for homebrew based on it's folder name.
	// Should probably not be a part of ParamSFO, but it'll be called in same places.
	// FileToStart here is actually a directory name, not a file, so taking GetFilename on it gets what we want.
	Path path = PSP_CoreParameter().fileToStart;
	if (!filename.empty())
		path = filename;

	std::string file = path.GetFilename();

	int sumOfAllLetters = 0;
	for (char &c : file) {
		sumOfAllLetters += c;
		// Get rid of some garbage characters than can arise when opening content URIs. Well, I've only seen '%', but...
		if (strchr("%() []", c) != nullptr) {
			c = 'X';
		} else {
			c = toupper(c);
		}
	}

	if (file.size() < 4) {
		file += "HOME";
	}
	file = file.substr(0, 4);

	std::string fakeID = file + StringFromFormat("%05d", sumOfAllLetters);
	return fakeID;
}
