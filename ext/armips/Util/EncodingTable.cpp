#include "stdafx.h"
#include "Util/EncodingTable.h"
#include "Util/Util.h"
#include "Core/Common.h"

#define MAXHEXLENGTH 32

Trie::Trie()
{
	Node root { 0, false };
	nodes.push_back(root);
}

void Trie::insert(const wchar_t* text, size_t value)
{
	size_t node = 0;	// root node

	// traverse existing nodes
	while (*text != 0)
	{
		LookupEntry lookupEntry { node, *text };
		auto it = lookup.find(lookupEntry);
		if (it == lookup.end())
			break;

		node = it->second;
		text++;
	}

	// add new nodes as necessary
	while (*text != 0)
	{
		Node newNode { nodes.size(), false };
		nodes.push_back(newNode);

		LookupEntry lookupEntry { node, *text };
		lookup[lookupEntry] = newNode.index;
		node = newNode.index;
		text++;
	}

	// set value
	nodes[node].hasValue = true;
	nodes[node].value = value;
}

void Trie::insert(wchar_t character, size_t value)
{
	wchar_t str[2];
	str[0] = character;
	str[1] = 0;
	insert(str,value);
}

bool Trie::findLongestPrefix(const wchar_t* text, size_t& result)
{
	size_t node = 0;		// root node
	size_t valueNode = 0;	// remember last node that had a value

	while (*text != 0)
	{
		if (nodes[node].hasValue)
			valueNode = node;

		LookupEntry lookupEntry { node, *text++ };
		auto it = lookup.find(lookupEntry);
		
		if (it == lookup.end())
			break;

		node = it->second;
	}
	
	if (nodes[node].hasValue)
		valueNode = node;

	result = nodes[valueNode].value;
	return nodes[valueNode].hasValue;
}

EncodingTable::EncodingTable()
{

}

EncodingTable::~EncodingTable()
{

}

void EncodingTable::clear()
{
	hexData.clear();
	entries.clear();
}

int parseHexString(std::wstring& hex, unsigned char* dest)
{
	for (size_t i = 0; i < hex.size(); i++)
	{
		wchar_t source = towlower(hex[i]);
		int value;

		if (source >= 'a' && source <= 'f')
		{
			value = source-'a'+10;
		} else if (source >= '0' && source <= '9')
		{
			value = source-'0';
		} else {
			return -1;
		}

		size_t index = i/2;
		if (i % 2)
			dest[index] = (dest[index] << 4) | value;
		else
			dest[index] = value;
	}

	return (int) hex.size()/2;
}

bool EncodingTable::load(const std::wstring& fileName, TextFile::Encoding encoding)
{
	unsigned char hexBuffer[MAXHEXLENGTH];

	TextFile input;
	if (input.open(fileName,TextFile::Read,encoding) == false)
		return false;

	hexData.clear();
	entries.clear();
	setTerminationEntry((unsigned char*)"\0",1);

	while (!input.atEnd())
	{
		std::wstring line = input.readLine();
		if (line.empty() || line[0] == '*') continue;
		
		if (line[0] == '/')
		{
			std::wstring hex = line.substr(1);
			if (hex.empty() || hex.length() > 2*MAXHEXLENGTH)
			{
				// error
				continue;
			}

			int length = parseHexString(hex,hexBuffer);
			if (length == -1)
			{
				// error
				continue;
			}

			setTerminationEntry(hexBuffer,length);
		} else {
			size_t pos = line.find(L'=');
			std::wstring hex = line.substr(0,pos);
			std::wstring value = line.substr(pos+1);

			if (hex.empty() || value.empty() || hex.length() > 2*MAXHEXLENGTH)
			{
				// error
				continue;
			}
			
			int length = parseHexString(hex,hexBuffer);
			if (length == -1)
			{
				// error
				continue;
			}

			addEntry(hexBuffer,length,value);
		}
	}

	return true;
}

void EncodingTable::addEntry(unsigned char* hex, size_t hexLength, const std::wstring& value)
{
	if (value.size() == 0)
		return;
	
	// insert into trie
	size_t index = entries.size();
	lookup.insert(value.c_str(),index);

	// add entry
	TableEntry entry;
	entry.hexPos = hexData.append(hex,hexLength);
	entry.hexLen = hexLength;
	entry.valueLen = value.size();

	entries.push_back(entry);
}

void EncodingTable::addEntry(unsigned char* hex, size_t hexLength, wchar_t value)
{
	if (value == '\0')
		return;
	
	// insert into trie
	size_t index = entries.size();
	lookup.insert(value,index);
	
	// add entry
	TableEntry entry;
	entry.hexPos = hexData.append(hex,hexLength);
	entry.hexLen = hexLength;
	entry.valueLen = 1;
	
	entries.push_back(entry);

}

void EncodingTable::setTerminationEntry(unsigned char* hex, size_t hexLength)
{
	terminationEntry.hexPos = hexData.append(hex,hexLength);
	terminationEntry.hexLen = hexLength;
	terminationEntry.valueLen = 0;
}

ByteArray EncodingTable::encodeString(const std::wstring& str, bool writeTermination)
{
	ByteArray result;

	size_t pos = 0;
	while (pos < str.size())
	{
		size_t index;
		if (lookup.findLongestPrefix(str.c_str()+pos,index) == false)
		{
			// error
			return ByteArray();
		}

		TableEntry& entry = entries[index];
		for (size_t i = 0; i < entry.hexLen; i++)
		{
			result.appendByte(hexData[entry.hexPos+i]);
		}

		pos += entry.valueLen;
	}

	if (writeTermination)
	{
		TableEntry& entry = terminationEntry;
		for (size_t i = 0; i < entry.hexLen; i++)
		{
			result.appendByte(hexData[entry.hexPos+i]);
		}
	}

	return result;
}

ByteArray EncodingTable::encodeTermination()
{
	ByteArray result;

	TableEntry& entry = terminationEntry;
	for (size_t i = 0; i < entry.hexLen; i++)
	{
		result.appendByte(hexData[entry.hexPos+i]);
	}

	return result;
}
