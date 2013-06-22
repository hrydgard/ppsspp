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

#include <stdio.h>
#include <string.h>
#include "ParamSFO.h"

struct Header
{
	u32 magic; /* Always PSF */
	u32 version; /* Usually 1.1 */
	u32 key_table_start; /* Start position of key_table */
	u32 data_table_start; /* Start position of data_table */
	u32 index_table_entries; /* Number of entries in index_table*/
};

struct IndexTable
{
	u16 key_table_offset; /* Offset of the param_key from start of key_table */
	u16 param_fmt; /* Type of data of param_data in the data_table */
	u32 param_len; /* Used Bytes by param_data in the data_table */
	u32 param_max_len; /* Total bytes reserved for param_data in the data_table */
	u32 data_table_offset; /* Offset of the param_data from start of data_table */
};

void ParamSFOData::SetValue(std::string key, unsigned int value, int max_size)
{
	values[key].type = VT_INT;
	values[key].i_value = value;
	values[key].max_size = max_size;
}
void ParamSFOData::SetValue(std::string key, std::string value, int max_size)
{
	values[key].type = VT_UTF8;
	values[key].s_value = value;
	values[key].max_size = max_size;
}

void ParamSFOData::SetValue(std::string key, const u8* value, unsigned int size, int max_size)
{
	values[key].type = VT_UTF8_SPE;
	values[key].SetData(value,size);
	values[key].max_size = max_size;
}

int ParamSFOData::GetValueInt(std::string key)
{
	std::map<std::string,ValueData>::iterator it = values.find(key);
	if(it == values.end() || it->second.type != VT_INT)
		return 0;
	return it->second.i_value;
}
std::string ParamSFOData::GetValueString(std::string key)
{
	std::map<std::string,ValueData>::iterator it = values.find(key);
	if(it == values.end() || (it->second.type != VT_UTF8))
		return "";
	return it->second.s_value;
}
u8* ParamSFOData::GetValueData(std::string key, unsigned int *size)
{
	std::map<std::string,ValueData>::iterator it = values.find(key);
	if(it == values.end() || (it->second.type != VT_UTF8_SPE))
		return 0;
	if(size)
	{
		*size = it->second.u_size;
	}
	return it->second.u_value;
}

// I'm so sorry Ced but this is highly endian unsafe :(
bool ParamSFOData::ReadSFO(const u8 *paramsfo, size_t size)
{
	const Header *header = (const Header *)paramsfo;
	if (header->magic != 0x46535000)
		return false;
	if (header->version != 0x00000101)
		WARN_LOG(LOADER, "Unexpected SFO header version: %08x", header->version);

	const IndexTable *indexTables = (const IndexTable *)(paramsfo + sizeof(Header));

	const u8 *key_start = paramsfo + header->key_table_start;
	const u8 *data_start = paramsfo + header->data_table_start;

	for (u32 i = 0; i < header->index_table_entries; i++)
	{
		const char *key = (const char *)(key_start + indexTables[i].key_table_offset);

		switch (indexTables[i].param_fmt) {
		case 0x0404:
			{
				// Unsigned int
				const u32 *data = (const u32 *)(data_start + indexTables[i].data_table_offset);
				SetValue(key,*data,indexTables[i].param_max_len);
				DEBUG_LOG(LOADER, "%s %08x", key, *data);
			}
			break;
		case 0x0004:
			// Special format UTF-8
			{
				const u8 *utfdata = (const u8 *)(data_start + indexTables[i].data_table_offset);
				DEBUG_LOG(LOADER, "%s %s", key, utfdata);
				SetValue(key, utfdata, indexTables[i].param_len, indexTables[i].param_max_len);
			}
			break;
		case 0x0204:
			// Regular UTF-8
			{
				const char *utfdata = (const char *)(data_start + indexTables[i].data_table_offset);
				DEBUG_LOG(LOADER, "%s %s", key, utfdata);
				SetValue(key,std::string(utfdata /*, indexTables[i].param_len*/), indexTables[i].param_max_len);
			}
			break;
		}
	}

	return true;
}

int ParamSFOData::GetDataOffset(const u8 *paramsfo, std::string dataName)
{
	const Header *header = (const Header *)paramsfo;
	if (header->magic != 0x46535000)
		return -1;
	if (header->version != 0x00000101)
		WARN_LOG(LOADER, "Unexpected SFO header version: %08x", header->version);

	const IndexTable *indexTables = (const IndexTable *)(paramsfo + sizeof(Header));

	const u8 *key_start = paramsfo + header->key_table_start;
	int data_start = header->data_table_start;

	for (u32 i = 0; i < header->index_table_entries; i++)
	{
		const char *key = (const char *)(key_start + indexTables[i].key_table_offset);
		if(std::string(key) == dataName)
		{
			return data_start + indexTables[i].data_table_offset;
		}
	}

	return -1;
}

bool ParamSFOData::WriteSFO(u8 **paramsfo, size_t *size)
{
	size_t total_size = 0;
	size_t key_size = 0;
	size_t data_size = 0;

	Header header;
	header.magic = 0x46535000;
	header.version = 0x00000101;
	header.index_table_entries = 0;

	total_size += sizeof(Header);

	// Get size info
	for (std::map<std::string,ValueData>::iterator it = values.begin(); it != values.end(); it++)
	{
		key_size += it->first.size()+1;
		data_size += it->second.max_size;

		header.index_table_entries++;
	}

	// Padding
	while((key_size%4)) key_size++;

	header.key_table_start = sizeof(Header) + header.index_table_entries * sizeof(IndexTable);
	header.data_table_start = header.key_table_start + (u32)key_size;

	total_size += sizeof(IndexTable) * header.index_table_entries;
	total_size += key_size;
	total_size += data_size;
	*size = total_size;

	u8* data = new u8[total_size];
	*paramsfo = data;
	memset(data, 0, total_size);
	memcpy(data, &header, sizeof(Header));

	// Now fill
	IndexTable *index_ptr = (IndexTable*)(data + sizeof(Header));
	u8* key_ptr = data + header.key_table_start;
	u8* data_ptr = data + header.data_table_start;

	for (std::map<std::string,ValueData>::iterator it = values.begin(); it != values.end(); it++)
	{
		u16 offset = (u16)(key_ptr - (data+header.key_table_start));
		index_ptr->key_table_offset = offset;
		offset = (u16)(data_ptr - (data+header.data_table_start));
		index_ptr->data_table_offset = offset;
		index_ptr->param_max_len = it->second.max_size;
		if (it->second.type == VT_INT)
		{
			index_ptr->param_fmt = 0x0404;
			index_ptr->param_len = 4;

			*(int*)data_ptr = it->second.i_value;
		}
		else if (it->second.type == VT_UTF8_SPE)
		{
			index_ptr->param_fmt = 0x0004;
			index_ptr->param_len = it->second.u_size;

			memset(data_ptr,0,index_ptr->param_max_len);
			memcpy(data_ptr,it->second.u_value,index_ptr->param_len);
		}
		else if (it->second.type == VT_UTF8)
		{
			index_ptr->param_fmt = 0x0204;
			index_ptr->param_len = (u32)it->second.s_value.size()+1;

			memcpy(data_ptr,it->second.s_value.c_str(),index_ptr->param_len);
			data_ptr[index_ptr->param_len] = 0;
		}

		memcpy(key_ptr,it->first.c_str(),it->first.size());
		key_ptr[it->first.size()] = 0;

		data_ptr += index_ptr->param_max_len;
		key_ptr += it->first.size()+1;
		index_ptr++;

	}

	return true;


}

