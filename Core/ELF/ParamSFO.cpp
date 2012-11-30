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

#include "../Globals.h"
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

void ParseDataString(const char *key, const char *utfdata, ParamSFOData *sfodata)
{
	if (!strcmp(key, "DISC_ID"))
	{
		sfodata->discID = utfdata;
	}
}

// I'm so sorry Ced but this is highly endian unsafe :(
bool ParseParamSFO(const u8 *paramsfo, size_t size, ParamSFOData *data)
{
	const Header *header = (const Header *)paramsfo;
	if (header->magic != 0x46535000) 
		return false;
	if (header->version != 0x00000101)
		WARN_LOG(LOADER, "Unexpected SFO header version: %08x", header->version);
	
	const IndexTable *indexTables = (const IndexTable *)(paramsfo + sizeof(Header));

	const u8 *key_start = paramsfo + header->key_table_start;
	const u8 *data_start = paramsfo + header->data_table_start;

	for (int i = 0; i < header->index_table_entries; i++) 
	{
		const char *key = (const char *)(key_start + indexTables[i].key_table_offset);

		switch (indexTables[i].param_fmt) {
		case 0x0404:
			{
				// Unsigned int
				const u32 *data = (const u32 *)(data_start + indexTables[i].data_table_offset);
				DEBUG_LOG(LOADER, "%s %08x", key, *data);
			}
			break; 
		case 0x0004:
			// Special format UTF-8
			{
				const char *utfdata = (const char *)(data_start + indexTables[i].data_table_offset);
				DEBUG_LOG(LOADER, "%s %s", key, utfdata);
				ParseDataString(key, utfdata, data);
			}
			break;
		case 0x0204:
			// Regular UTF-8
			{
				const char *utfdata = (const char *)(data_start + indexTables[i].data_table_offset);
				DEBUG_LOG(LOADER, "%s %s", key, utfdata);
				ParseDataString(key, utfdata, data);
			}
			break;
		}
	}

	return true;
}