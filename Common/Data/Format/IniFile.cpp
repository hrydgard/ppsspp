// IniFile
// Taken from Dolphin but relicensed by me, Henrik Rydgard, under the MIT
// license as I wrote the whole thing originally and it has barely changed.

#include <cstdlib>
#include <cstdio>

#include <inttypes.h>

// Hm, what's this for?
#ifndef _MSC_VER
#include <strings.h>
#endif

#include <algorithm>
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "Common/Data/Format/IniFile.h"
#include "Common/Data/Text/Parsers.h"
#include "Common/File/VFS/VFS.h"
#include "Common/File/FileUtil.h"
#include "Common/Log.h"
#include "Common/Math/math_util.h"

#include "Common/StringUtils.h"

// This unescapes # signs.
// NOTE: These parse functions can make better use of the string_view - the pos argument should not be needed, for example.
static bool ParseLineKey(std::string_view line, size_t &pos, std::string *keyOut) {
	std::string key = "";

	while (pos < line.size()) {
		size_t next = line.find_first_of("=#", pos);
		if (next == line.npos || next == 0) {
			// Key never ended or empty, invalid.
			return false;
		} else if (line[next] == '#') {
			if (line[next - 1] != '\\') {
				// Value commented out before =, so not valid.
				return false;
			}

			// Escaped.
			key += line.substr(pos, next - pos - 1);
			key.push_back('#');
			pos = next + 1;
		} else if (line[next] == '=') {
			// Hurray, done.
			key += line.substr(pos, next - pos);
			pos = next + 1;
			break;
		}
	}

	if (keyOut) {
		*keyOut = StripSpaces(key);
	}
	return true;
}

static bool ParseLineValue(std::string_view line, size_t &pos, std::string *valueOut) {
	std::string value = "";

	std::string_view strippedLine = StripSpaces(line.substr(pos));
	if (strippedLine.size() >= 2 && strippedLine[0] == '"' && strippedLine[strippedLine.size() - 1] == '"') {
		// Don't remove comment if is surrounded by " "
		value += line.substr(pos);
		pos = line.npos; // Won't enter the while below
	}

	while (pos < line.size()) {
		size_t next = line.find('#', pos);
		if (next == line.npos) {
			value += line.substr(pos);
			pos = line.npos;
			break;
		} else if (line[next - 1] != '\\') {
			// It wasn't escaped, so finish before the #.
			value += line.substr(pos, next - pos);
			// Include the comment's # in pos.
			pos = next;
			break;
		} else {
			// Escaped.
			value += line.substr(pos, next - pos - 1);
			value.push_back('#');
			pos = next + 1;
		}
	}

	if (valueOut) {
		*valueOut = StripQuotes(StripSpaces(value));
	}

	return true;
}

static bool ParseLineComment(std::string_view line, size_t &pos, std::string *commentOut) {
	// Don't bother with anything if we don't need the comment data.
	if (commentOut) {
		// Include any whitespace/formatting in the comment.
		size_t commentStartPos = pos;
		if (commentStartPos != line.npos) {
			while (commentStartPos > 0 && line[commentStartPos - 1] <= ' ') {
				--commentStartPos;
			}

			*commentOut = line.substr(commentStartPos);
		} else {
			// There was no comment.
			commentOut->clear();
		}
	}

	pos = line.npos;
	return true;
}

static bool ParseLine(std::string_view line, std::string* keyOut, std::string* valueOut, std::string* commentOut)
{
	// Rules:
	// 1. A line starting with ; is commented out.
	// 2. A # in a line (and all the space before it) is the comment.
	// 3. A \# in a line is not part of a comment and becomes # in the value.
	// 4. Whitespace around values is removed.
	// 5. Double quotes around values is removed.
	// 6. Value surrounded by double quotes don't parsed to strip comment.

	if (line.size() < 2 || line[0] == ';')
		return false;

	size_t pos = 0;
	if (!ParseLineKey(line, pos, keyOut))
		return false;
	if (!ParseLineValue(line, pos, valueOut))
		return false;
	if (!ParseLineComment(line, pos, commentOut))
		return false;

	return true;
}

static std::string EscapeHash(std::string_view value) {
	std::string result = "";

	for (size_t pos = 0; pos < value.size(); ) {
		size_t next = value.find('#', pos);
		if (next == value.npos) {
			result += value.substr(pos);
			pos = value.npos;
		} else {
			result += value.substr(pos, next - pos);
			result += "\\#";
			pos = next + 1;
		}
	}

	return result;
}

void ParsedIniLine::ParseFrom(std::string_view line) {
	line = StripSpaces(line);
	if (line.empty()) {
		key.clear();
		value.clear();
		comment.clear();
	} else if (line[0] == '#') {
		key.clear();
		value.clear();
		comment = line;
	} else {
		ParseLine(line, &key, &value, &comment);
	}
}

void ParsedIniLine::Reconstruct(std::string *output) const {
	if (!key.empty()) {
		*output = EscapeHash(key) + " = " + EscapeHash(value) + comment;
	} else {
		*output = comment;
	}
}

void Section::Clear() {
	lines_.clear();
}

bool Section::GetKeys(std::vector<std::string> &keys) const {
	keys.clear();
	for (auto liter = lines_.begin(); liter != lines_.end(); ++liter) {
		if (!liter->Key().empty())
			keys.emplace_back(liter->Key());
	}
	return true;
}

ParsedIniLine *Section::GetLine(const char *key) {
	for (auto &line : lines_) {
		if (equalsNoCase(line.Key(), key))
			return &line;
	}
	return nullptr;
}

const ParsedIniLine *Section::GetLine(const char* key) const {
	for (auto &line : lines_) {
		if (equalsNoCase(line.Key(), key))
			return &line;
	}
	return nullptr;
}

void Section::Set(const char* key, uint32_t newValue) {
	char temp[128];
	snprintf(temp, sizeof(temp), "0x%08x", newValue);
	Set(key, (const char *)temp);
}

void Section::Set(const char* key, uint64_t newValue) {
	char temp[128];
	snprintf(temp, sizeof(temp), "0x%016" PRIx64, newValue);
	Set(key, (const char *)temp);
}

void Section::Set(const char* key, float newValue) {
	_dbg_assert_(!my_isnanorinf(newValue));
	char temp[128];
	snprintf(temp, sizeof(temp), "%f", newValue);
	Set(key, (const char *)temp);
}

void Section::Set(const char* key, double newValue) {
	char temp[128];
	snprintf(temp, sizeof(temp), "%f", newValue);
	Set(key, (const char *)temp);
}

void Section::Set(const char* key, int newValue) {
	char temp[128];
	snprintf(temp, sizeof(temp), "%d", newValue);
	Set(key, (const char *)temp);
}

void Section::Set(const char* key, const char* newValue) {
	ParsedIniLine *line = GetLine(key);
	if (line) {
		line->SetValue(newValue);
	} else {
		// The key did not already exist in this section - let's add it.
		lines_.emplace_back(ParsedIniLine(key, newValue));
	}
}

void Section::Set(const char* key, const std::string& newValue, const std::string& defaultValue)
{
	if (newValue != defaultValue)
		Set(key, newValue);
	else
		Delete(key);
}

bool Section::Get(const char* key, std::string* value, const char* defaultValue) const {
	const ParsedIniLine *line = GetLine(key);
	if (!line) {
		if (defaultValue) {
			*value = defaultValue;
		}
		return false;
	} else {
		*value = line->Value();
	}
	return true;
}

void Section::Set(const char* key, const float newValue, const float defaultValue)
{
	if (newValue != defaultValue)
		Set(key, newValue);
	else
		Delete(key);
}

void Section::Set(const char* key, int newValue, int defaultValue)
{
	if (newValue != defaultValue)
		Set(key, newValue);
	else
		Delete(key);
}

void Section::Set(const char* key, bool newValue, bool defaultValue)
{
	if (newValue != defaultValue)
		Set(key, newValue);
	else
		Delete(key);
}

void Section::Set(const char* key, const std::vector<std::string>& newValues) 
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

void Section::AddComment(const std::string &comment) {
	lines_.emplace_back(ParsedIniLine::CommentOnly("# " + comment));
}

bool Section::Get(const char* key, std::vector<std::string>& values) const
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

bool Section::Get(const char* key, int* value, int defaultValue) const
{
	std::string temp;
	bool retval = Get(key, &temp, 0);
	if (retval && TryParse(temp, value))
		return true;
	*value = defaultValue;
	return false;
}

bool Section::Get(const char* key, uint32_t* value, uint32_t defaultValue) const
{
	std::string temp;
	bool retval = Get(key, &temp, 0);
	if (retval && TryParse(temp, value))
		return true;
	*value = defaultValue;
	return false;
}

bool Section::Get(const char* key, uint64_t* value, uint64_t defaultValue) const
{
	std::string temp;
	bool retval = Get(key, &temp, 0);
	if (retval && TryParse(temp, value))
		return true;
	*value = defaultValue;
	return false;
}

bool Section::Get(const char* key, bool* value, bool defaultValue) const
{
	std::string temp;
	bool retval = Get(key, &temp, 0);
	if (retval && TryParse(temp, value))
		return true;
	*value = defaultValue;
	return false;
}

bool Section::Get(const char* key, float* value, float defaultValue) const
{
	std::string temp;
	bool retval = Get(key, &temp, 0);
	if (retval && TryParse(temp, value))
		return true;
	*value = defaultValue;
	return false;
}

bool Section::Get(const char* key, double* value, double defaultValue) const
{
	std::string temp;
	bool retval = Get(key, &temp, 0);
	if (retval && TryParse(temp, value))
		return true;
	*value = defaultValue;
	return false;
}

bool Section::Exists(const char *key) const {
	for (auto &line : lines_) {
		if (equalsNoCase(key, line.Key()))
			return true;
	}
	return false;
}

std::map<std::string, std::string> Section::ToMap() const {
	std::map<std::string, std::string> outMap;
	for (auto &line : lines_) {
		if (!line.Key().empty()) {
			outMap[std::string(line.Key())] = line.Value();
		}
	}
	return outMap;
}

bool Section::Delete(const char *key) {
	ParsedIniLine *line = GetLine(key);
	for (auto liter = lines_.begin(); liter != lines_.end(); ++liter) {
		if (line == &*liter) {
			lines_.erase(liter);
			return true;
		}
	}
	return false;
}

// IniFile

const Section* IniFile::GetSection(const char* sectionName) const {
	for (const auto &iter : sections)
		if (!strcasecmp(iter->name().c_str(), sectionName))
			return iter.get();
	return nullptr;
}

Section* IniFile::GetSection(const char* sectionName) {
	for (const auto &iter : sections)
		if (!strcasecmp(iter->name().c_str(), sectionName))
			return iter.get();
	return nullptr;
}

Section* IniFile::GetOrCreateSection(const char* sectionName) {
	Section* section = GetSection(sectionName);
	if (!section) {
		sections.push_back(std::make_unique<Section>(sectionName));
		section = sections.back().get();
	}
	return section;
}

bool IniFile::DeleteSection(const char* sectionName) {
	Section* s = GetSection(sectionName);
	if (!s)
		return false;

	for (auto iter = sections.begin(); iter != sections.end(); ++iter) {
		if (iter->get() == s) {
			sections.erase(iter);
			return true;
		}
	}
	return false;
}

bool IniFile::Exists(const char* sectionName, const char* key) const {
	const Section* section = GetSection(sectionName);
	if (!section)
		return false;
	return section->Exists(key);
}

bool IniFile::DeleteKey(const char* sectionName, const char* key) {
	Section* section = GetSection(sectionName);
	if (!section)
		return false;
	ParsedIniLine *line = section->GetLine(key);
	for (auto liter = section->lines_.begin(); liter != section->lines_.end(); ++liter) {
		if (line == &(*liter)) {
			section->lines_.erase(liter);
			return true;
		}
	}
	return false; //shouldn't happen
}

// Return a list of all keys in a section
bool IniFile::GetKeys(const char* sectionName, std::vector<std::string>& keys) const {
	const Section *section = GetSection(sectionName);
	if (!section)
		return false;
	return section->GetKeys(keys);
}

void IniFile::SortSections()
{
	std::sort(sections.begin(), sections.end());
}

bool IniFile::Load(const Path &path)
{
	sections.clear();
	sections.push_back(std::make_unique<Section>(""));
	// first section consists of the comments before the first real section

	// Open file
	std::string data;
	if (!File::ReadTextFileToString(path, &data)) {
		return false;
	}
	std::stringstream sstream(data);
	bool success = Load(sstream);
	return success;
}

bool IniFile::LoadFromVFS(VFSInterface &vfs, const std::string &filename) {
	size_t size;
	uint8_t *data = vfs.ReadFile(filename.c_str(), &size);
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
	char *templine = new char[MAX_BYTES];  // avoid using up massive stack space

	while (!(in.eof() || in.fail()))
	{
		in.getline(templine, MAX_BYTES);
		std::string line = templine;

		// Remove UTF-8 byte order marks.
		if (line.substr(0, 3) == "\xEF\xBB\xBF") {
			line = line.substr(3);
		}
		 
#ifndef _WIN32
		// Check for CRLF eol and convert it to LF
		if (!line.empty() && line.at(line.size()-1) == '\r') {
			line.erase(line.size()-1);
		}
#endif

		if (!line.empty()) {
			size_t sectionNameEnd = std::string::npos;
			if (line[0] == '[') {
				sectionNameEnd = line.find(']');
			}

			if (sectionNameEnd != std::string::npos) {
				// New section!
				std::string sub = line.substr(1, sectionNameEnd - 1);
				sections.push_back(std::make_unique<Section>(sub));

				if (sectionNameEnd + 1 < line.size()) {
					sections.back()->comment = line.substr(sectionNameEnd + 1);
				}
			} else {
				if (sections.empty()) {
					sections.push_back(std::make_unique<Section>(""));
				}
				ParsedIniLine parsedLine;
				parsedLine.ParseFrom(line);
				sections.back()->lines_.push_back(parsedLine);
			}
		}
	}

	delete[] templine;
	return true;
}

bool IniFile::Save(const Path &filename)
{
	FILE *file = File::OpenCFile(filename, "w");
	if (!file) {
		return false;
	}

	// UTF-8 byte order mark. To make sure notepad doesn't go nuts.
	// TODO: Do we still need this? It's annoying.
	fprintf(file, "\xEF\xBB\xBF");

	for (const auto &section : sections) {
		if (!section->name().empty() && (!section->lines_.empty() || !section->comment.empty())) {
			fprintf(file, "[%s]%s\n", section->name().c_str(), section->comment.c_str());
		}
		for (const auto &line : section->lines_) {
			std::string buffer;
			line.Reconstruct(&buffer);
			fprintf(file, "%s\n", buffer.c_str());
		}
	}

	fclose(file);
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

bool IniFile::Get(const char* sectionName, const char* key, uint64_t* value, uint64_t defaultValue)
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
