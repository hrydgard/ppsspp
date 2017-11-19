#pragma once
#include "Util/ByteArray.h"
#include "Util/FileClasses.h"
#include <map>

class Trie
{
public:
	Trie();
	void insert(const wchar_t* text, size_t value);
	void insert(wchar_t character, size_t value);
	bool findLongestPrefix(const wchar_t* text, size_t& result);
private:
	struct LookupEntry
	{
		size_t node;
		wchar_t input;

		bool operator<(const LookupEntry& other) const
		{
			if (node != other.node)
				return node < other.node;
			return input < other.input;
		}
	};

	struct Node
	{
		size_t index;
		bool hasValue;
		size_t value;
	};

	std::vector<Node> nodes;
	std::map<LookupEntry,size_t> lookup;
};

class EncodingTable
{
public:
	EncodingTable();
	~EncodingTable();
	void clear();
	bool load(const std::wstring& fileName, TextFile::Encoding encoding = TextFile::GUESS);
	bool isLoaded() { return entries.size() != 0; };
	void addEntry(unsigned char* hex, size_t hexLength, const std::wstring& value);
	void addEntry(unsigned char* hex, size_t hexLength, wchar_t value);
	void setTerminationEntry(unsigned char* hex, size_t hexLength);
	ByteArray encodeString(const std::wstring& str, bool writeTermination = true);
	ByteArray encodeTermination();
private:
	struct TableEntry
	{
		size_t hexPos;
		size_t hexLen;
		size_t valueLen;
	};

	ByteArray hexData;
	std::vector<TableEntry> entries;
	Trie lookup;
	TableEntry terminationEntry;
};
