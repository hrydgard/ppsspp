// IniFile
// Taken from Dolphin but relicensed by me, Henrik Rydgard, under the MIT
// license as I wrote the whole thing originally and it has barely changed.

#include <stdlib.h>
#include <stdio.h>

#ifndef _MSC_VER
#include <strings.h>
#endif

#include <string>
#include <vector>
#include <iostream>
#include <fstream>
#include <algorithm>

#include "base/stringutil.h"
#include "file/ini_file.h"
#include "file/vfs.h"

#ifdef _WIN32
	// Function Cross-Compatibility
#define strcasecmp _stricmp
#endif

// Ugh, this is ugly.
static bool ParseLine(const std::string& line, std::string* keyOut, std::string* valueOut, std::string* commentOut)
{
	int FirstEquals = (int)line.find("=", 0);
	int FirstCommentChar = -1;

	// Comments
	if (FirstCommentChar < 0)
		FirstCommentChar =
			(int)line.find("#", FirstEquals > 0 ? FirstEquals : 0);
	if (FirstCommentChar < 0 && line[0] == ';')
		FirstCommentChar = 0;

	// Allow preservation of spacing before comment
	if (FirstCommentChar > 0)
	{
		while (line[FirstCommentChar - 1] == ' ' || line[FirstCommentChar - 1] == 9) // 9 == tab
		{
			FirstCommentChar--;
		}
	}

	if ((FirstEquals >= 0) && ((FirstCommentChar < 0) || (FirstEquals < FirstCommentChar)))
	{
		// Yes, a valid key/value line!
		*keyOut = StripSpaces(line.substr(0, FirstEquals));
		if (commentOut) *commentOut = FirstCommentChar > 0 ? line.substr(FirstCommentChar) : std::string("");
		if (valueOut) *valueOut = StripQuotes(StripSpaces(line.substr(FirstEquals + 1, FirstCommentChar - FirstEquals - 1)));
		return true;
	}
	return false;
}

std::string* IniFile::Section::GetLine(const char* key, std::string* valueOut, std::string* commentOut)
{
	for (std::vector<std::string>::iterator iter = lines.begin(); iter != lines.end(); ++iter)
	{
		std::string& line = *iter;
		std::string lineKey;
		ParseLine(line, &lineKey, valueOut, commentOut);
		if (!strcasecmp(lineKey.c_str(), key))
			return &line;
	}
	return 0;
}

void IniFile::Section::Set(const char* key, const char* newValue)
{
	std::string value, commented;
	std::string* line = GetLine(key, &value, &commented);
	if (line)
	{
		// Change the value - keep the key and comment
		*line = StripSpaces(key) + " = " + newValue + commented;
	}
	else
	{
		// The key did not already exist in this section - let's add it.
		lines.push_back(std::string(key) + " = " + newValue);
	}
}

void IniFile::Section::Set(const char* key, const std::string& newValue, const std::string& defaultValue)
{
	if (newValue != defaultValue)
		Set(key, newValue);
	else
		Delete(key);
}

bool IniFile::Section::Get(const char* key, std::string* value, const char* defaultValue)
{
	std::string* line = GetLine(key, value, 0);
	if (!line)
	{
		if (defaultValue)
		{
			*value = defaultValue;
		}
		return false;
	}
	return true;
}

void IniFile::Section::Set(const char* key, const float newValue, const float defaultValue)
{
	if (newValue != defaultValue)
		Set(key, newValue);
	else
		Delete(key);
}

void IniFile::Section::Set(const char* key, int newValue, int defaultValue)
{
	if (newValue != defaultValue)
		Set(key, newValue);
	else
		Delete(key);
}

void IniFile::Section::Set(const char* key, bool newValue, bool defaultValue)
{
	if (newValue != defaultValue)
		Set(key, newValue);
	else
		Delete(key);
}

void IniFile::Section::Set(const char* key, const std::vector<std::string>& newValues) 
{
	std::string temp;
	// Join the strings with , 
	std::vector<std::string>::const_iterator it;
	for (it = newValues.begin(); it != newValues.end(); ++it)
	{
		temp += (*it) + ",";
	}
	// remove last ,
  if (temp.length())
	  temp.resize(temp.length() - 1);
	Set(key, temp.c_str());
}

bool IniFile::Section::Get(const char* key, std::vector<std::string>& values) 
{
	std::string temp;
	bool retval = Get(key, &temp, 0);
	if (!retval || temp.empty())
	{
		return false;
	}
	// ignore starting , if any
	size_t subStart = temp.find_first_not_of(",");
	size_t subEnd;

	// split by , 
	while (subStart != std::string::npos) {
		
		// Find next , 
		subEnd = temp.find_first_of(",", subStart);
		if (subStart != subEnd) 
			// take from first char until next , 
			values.push_back(StripSpaces(temp.substr(subStart, subEnd - subStart)));
	
		// Find the next non , char
		subStart = temp.find_first_not_of(",", subEnd);
	} 
	
	return true;
}

bool IniFile::Section::Get(const char* key, int* value, int defaultValue)
{
	std::string temp;
	bool retval = Get(key, &temp, 0);
	if (retval && TryParse(temp.c_str(), value))
		return true;
	*value = defaultValue;
	return false;
}

bool IniFile::Section::Get(const char* key, uint32_t* value, uint32_t defaultValue)
{
	std::string temp;
	bool retval = Get(key, &temp, 0);
	if (retval && TryParse(temp, value))
		return true;
	*value = defaultValue;
	return false;
}

bool IniFile::Section::Get(const char* key, bool* value, bool defaultValue)
{
	std::string temp;
	bool retval = Get(key, &temp, 0);
	if (retval && TryParse(temp.c_str(), value))
		return true;
	*value = defaultValue;
	return false;
}

bool IniFile::Section::Get(const char* key, float* value, float defaultValue)
{
	std::string temp;
	bool retval = Get(key, &temp, 0);
	if (retval && TryParse(temp.c_str(), value))
		return true;
	*value = defaultValue;
	return false;
}

bool IniFile::Section::Get(const char* key, double* value, double defaultValue)
{
	std::string temp;
	bool retval = Get(key, &temp, 0);
	if (retval && TryParse(temp.c_str(), value))
		return true;
	*value = defaultValue;
	return false;
}

bool IniFile::Section::Exists(const char *key) const
{
	for (std::vector<std::string>::const_iterator iter = lines.begin(); iter != lines.end(); ++iter)
	{
		std::string lineKey;
		ParseLine(*iter, &lineKey, NULL, NULL);
		if (!strcasecmp(lineKey.c_str(), key))
			return true;
	}
	return false;
}

std::map<std::string, std::string> IniFile::Section::ToMap() const
{
	std::map<std::string, std::string> outMap;
	for (std::vector<std::string>::const_iterator iter = lines.begin(); iter != lines.end(); ++iter)
	{
		std::string lineKey, lineValue;
		if (ParseLine(*iter, &lineKey, &lineValue, NULL)) {
			outMap[lineKey] = lineValue;
		}
	}
	return outMap;
}


bool IniFile::Section::Delete(const char *key)
{
	std::string* line = GetLine(key, 0, 0);
	for (std::vector<std::string>::iterator liter = lines.begin(); liter != lines.end(); ++liter)
	{
		if (line == &*liter)
		{
			lines.erase(liter);
			return true;
		}
	}
	return false;
}

// IniFile

const IniFile::Section* IniFile::GetSection(const char* sectionName) const
{
	for (std::vector<Section>::const_iterator iter = sections.begin(); iter != sections.end(); ++iter)
		if (!strcasecmp(iter->name().c_str(), sectionName))
			return (&(*iter));
	return 0;
}

IniFile::Section* IniFile::GetSection(const char* sectionName)
{
	for (std::vector<Section>::iterator iter = sections.begin(); iter != sections.end(); ++iter)
		if (!strcasecmp(iter->name().c_str(), sectionName))
			return (&(*iter));
	return 0;
}

IniFile::Section* IniFile::GetOrCreateSection(const char* sectionName)
{
	Section* section = GetSection(sectionName);
	if (!section)
	{
		sections.push_back(Section(sectionName));
		section = &sections[sections.size() - 1];
	}
	return section;
}

bool IniFile::DeleteSection(const char* sectionName)
{
	Section* s = GetSection(sectionName);
	if (!s)
		return false;
	for (std::vector<Section>::iterator iter = sections.begin(); iter != sections.end(); ++iter)
	{
		if (&(*iter) == s)
		{
			sections.erase(iter);
			return true;
		}
	}
	return false;
}

bool IniFile::Exists(const char* sectionName, const char* key) const
{
	const Section* section = GetSection(sectionName);
	if (!section)
		return false;
	return section->Exists(key);
}

void IniFile::SetLines(const char* sectionName, const std::vector<std::string> &lines)
{
	Section* section = GetOrCreateSection(sectionName);
	section->lines.clear();
	for (std::vector<std::string>::const_iterator iter = lines.begin(); iter != lines.end(); ++iter)
	{
		section->lines.push_back(*iter);
	}
}

bool IniFile::DeleteKey(const char* sectionName, const char* key)
{
	Section* section = GetSection(sectionName);
	if (!section)
		return false;
	std::string* line = section->GetLine(key, 0, 0);
	for (std::vector<std::string>::iterator liter = section->lines.begin(); liter != section->lines.end(); ++liter)
	{
		if (line == &(*liter))
		{
			section->lines.erase(liter);
			return true;
		}
	}
	return false; //shouldn't happen
}

// Return a list of all keys in a section
bool IniFile::GetKeys(const char* sectionName, std::vector<std::string>& keys) const
{
	const Section* section = GetSection(sectionName);
	if (!section)
		return false;
	keys.clear();
	for (std::vector<std::string>::const_iterator liter = section->lines.begin(); liter != section->lines.end(); ++liter)
	{
		std::string key;
		ParseLine(*liter, &key, 0, 0);
		keys.push_back(key);
	}
	return true;
}

// Return a list of all lines in a section
bool IniFile::GetLines(const char* sectionName, std::vector<std::string>& lines, const bool remove_comments) const
{
	const Section* section = GetSection(sectionName);
	if (!section)
		return false;

	lines.clear();
	for (std::vector<std::string>::const_iterator iter = section->lines.begin(); iter != section->lines.end(); ++iter)
	{
		std::string line = StripSpaces(*iter);

		if (remove_comments)
		{
			int commentPos = (int)line.find('#');
			if (commentPos == 0)
			{
				continue;
			}

			if (commentPos != (int)std::string::npos)
			{
				line = StripSpaces(line.substr(0, commentPos));
			}
		}

		lines.push_back(line);
	}

	return true;
}


void IniFile::SortSections()
{
	std::sort(sections.begin(), sections.end());
}

bool IniFile::Load(const char* filename)
{
	sections.clear();
	sections.push_back(Section(""));
	// first section consists of the comments before the first real section

	// Open file
	std::ifstream in;
	in.open(filename, std::ios::in);

	if (in.fail()) return false;

	bool success = Load(in);
	in.close();
	return success;
}

bool IniFile::LoadFromVFS(const std::string &filename) {
	size_t size;
	uint8_t *data = VFSReadFile(filename.c_str(), &size);
	if (!data)
		return false;
	std::string str((const char*)data, size);
	delete [] data;

	std::stringstream sstream(str);
	return Load(sstream);
}

bool IniFile::Load(std::istream &in) {
	// Maximum number of letters in a line
	static const int MAX_BYTES = 1024*32;

	while (!in.eof())
	{
		char templine[MAX_BYTES];
		in.getline(templine, MAX_BYTES);
		std::string line = templine;

		// Remove UTF-8 byte order marks.
		if (line.substr(0, 3) == "\xEF\xBB\xBF")
			line = line.substr(3);
		 
#ifndef _WIN32
		// Check for CRLF eol and convert it to LF
		if (!line.empty() && line.at(line.size()-1) == '\r')
		{
			line.erase(line.size()-1);
		}
#endif

		if (in.eof()) break;

		if (line.size() > 0)
		{
			if (line[0] == '[')
			{
				size_t endpos = line.find("]");

				if (endpos != std::string::npos)
				{
					// New section!
					std::string sub = line.substr(1, endpos - 1);
					sections.push_back(Section(sub));

					if (endpos + 1 < line.size())
					{
						sections[sections.size() - 1].comment = line.substr(endpos + 1);
					}
				}
			}
			else
			{
				if (sections.size() > 0)
					sections[sections.size() - 1].lines.push_back(line);
			}
		}
	}

	return true;
}

bool IniFile::Save(const char* filename)
{
	std::ofstream out;
	out.open(filename, std::ios::out);

	if (out.fail())
	{
		return false;
	}

	// UTF-8 byte order mark. To make sure notepad doesn't go nuts.
	out << "\xEF\xBB\xBF";

	// Currently testing if dolphin community can handle the requirements of C++11 compilation
	// If you get a compiler error on this line, your compiler is probably old.
	// Update to g++ 4.4 or a recent version of clang (XCode 4.2 on OS X).
	// If you don't want to update, complain in a google code issue, the dolphin forums or #dolphin-emu.
	for (std::vector<Section>::iterator iter = sections.begin(); iter != sections.end(); ++iter)
	{
		const Section& section = *iter;

		if (section.name() != "")
		{
			out << "[" << section.name() << "]" << section.comment << std::endl;
		}

		for (std::vector<std::string>::const_iterator liter = section.lines.begin(); liter != section.lines.end(); ++liter)
		{
			std::string s = *liter;
			out << s << std::endl;
		}
	}

	out.close();
	return true;
}


bool IniFile::Get(const char* sectionName, const char* key, std::string* value, const char* defaultValue)
{
	Section* section = GetSection(sectionName);
	if (!section) {
		if (defaultValue) {
			*value = defaultValue;
		}
		return false;
	}
	return section->Get(key, value, defaultValue);
}

bool IniFile::Get(const char *sectionName, const char* key, std::vector<std::string>& values) 
{
	Section *section = GetSection(sectionName);
	if (!section)
		return false;
	return section->Get(key, values);
}

bool IniFile::Get(const char* sectionName, const char* key, int* value, int defaultValue)
{
	Section *section = GetSection(sectionName);
	if (!section) {
		*value = defaultValue;
		return false;
	} else {
		return section->Get(key, value, defaultValue);
	}
}

bool IniFile::Get(const char* sectionName, const char* key, uint32_t* value, uint32_t defaultValue)
{
	Section *section = GetSection(sectionName);
	if (!section) {
		*value = defaultValue;
		return false;
	} else {
		return section->Get(key, value, defaultValue);
	}
}

bool IniFile::Get(const char* sectionName, const char* key, bool* value, bool defaultValue)
{
	Section *section = GetSection(sectionName);
	if (!section) {
		*value = defaultValue;
		return false;
	} else {
		return section->Get(key, value, defaultValue);
	}
}


// Unit test. TODO: Move to the real unit test framework.
/*
   int main()
   {
    IniFile ini;
    ini.Load("my.ini");
    ini.Set("Hej", "A", "amaskdfl");
    ini.Set("Mossa", "A", "amaskdfl");
    ini.Set("Aissa", "A", "amaskdfl");
    //ini.Read("my.ini");
    std::string x;
    ini.Get("Hej", "B", &x, "boo");
    ini.DeleteKey("Mossa", "A");
    ini.DeleteSection("Mossa");
    ini.SortSections();
    ini.Save("my.ini");
    //UpdateVars(ini);
    return 0;
   }
 */
