#include "stdafx.h"
#include "Util.h"
#include <sys/stat.h>
#ifdef _WIN32
#include <windows.h>
#include <shlwapi.h>
#else
#include <unistd.h>
#endif

std::wstring convertUtf8ToWString(const char* source)
{
	std::wstring result;

	int index = 0;
	while (source[index] != 0)
	{
		int extraBytes = 0;
		int value = source[index++];
			
		if ((value & 0xE0) == 0xC0)
		{
			extraBytes = 1;
			value &= 0x1F;
		} else if ((value & 0xF0) == 0xE0)
		{
			extraBytes = 2;
			value &= 0x0F;
		} else if (value > 0x7F)
		{
			// error
			return std::wstring();
		}

		for (int i = 0; i < extraBytes; i++)
		{
			int b = source[index++];
			if ((b & 0xC0) != 0x80)
			{
			// error
			return std::wstring();
			}

			value = (value << 6) | (b & 0x3F);
		}

		result += value;
	}

	return result;
}

std::string convertWCharToUtf8(wchar_t character)
{
	std::string result;
	
	if (character < 0x80)
	{
		result += character & 0x7F;
	} else if (character < 0x800)
	{
		result += 0xC0 | ((character >> 6) & 0x1F);
		result += (0x80 | (character & 0x3F));
	} else {
		result += 0xE0 | ((character >> 12) & 0xF);
		result += 0x80 | ((character >> 6) & 0x3F);
		result += 0x80 | (character & 0x3F);
	}

	return result;
}

std::string convertWStringToUtf8(const std::wstring& source)
{
	std::string result;
	
	for (size_t i = 0; i < source.size(); i++)
	{
		wchar_t character = source[i];
		if (character < 0x80)
		{
			result += character & 0x7F;
		} else if (character < 0x800)
		{
			result += 0xC0 | ((character >> 6) & 0x1F);
			result += (0x80 | (character & 0x3F));
		} else {
			result += 0xE0 | ((character >> 12) & 0xF);
			result += 0x80 | ((character >> 6) & 0x3F);
			result += 0x80 | (character & 0x3F);
		}
	}

	return result;
}

std::wstring intToHexString(unsigned int value, int digits, bool prefix)
{
	std::wstring result;
	result.reserve((digits+prefix) ? 2 : 0);

	if (prefix)
	{
		result += '0';
		result += 'x';
	}

	while (digits > 8)
	{
		result += '0';
		digits--;
	}
	
	wchar_t buf[9];
	swprintf(buf,9,L"%0*X",digits,value);
	result += buf;

	return result;
}

std::wstring intToString(unsigned int value, int digits)
{
	std::wstring result;
	result.reserve(digits);

	while (digits > 8)
	{
		result += ' ';
		digits--;
	}
	
	wchar_t buf[9];
	swprintf(buf,9,L"%*d",digits,value);
	result += buf;

	return result;
}

int32_t getFloatBits(float value)
{
	union { float f; int32_t i; } u;
	u.f = value;
	return u.i;
}

int64_t getDoubleBits(double value)
{
	union { double f; int64_t i; } u;
	u.f = value;
	return u.i;
}

StringList getStringListFromArray(wchar_t** source, int count)
{
	StringList result;
	for (int i = 0; i < count; i++)
	{
		result.push_back(std::wstring(source[i]));
	}

	return result;
}

int64_t fileSize(const std::wstring& fileName)
{
#ifdef _WIN32
	WIN32_FILE_ATTRIBUTE_DATA attr;
	if (!GetFileAttributesEx(fileName.c_str(),GetFileExInfoStandard,&attr)
		|| (attr.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
		return 0;
	return ((int64_t) attr.nFileSizeHigh << 32) | (int64_t) attr.nFileSizeLow;
#else	
	std::string utf8 = convertWStringToUtf8(fileName);
	struct stat fileStat;
	int err = stat(utf8.c_str(),&fileStat);
	if (0 != err)
		return 0; 
	return fileStat.st_size; 
#endif
}

bool fileExists(const std::wstring& strFilename)
{
#ifdef _WIN32
	int OldMode = SetErrorMode(SEM_FAILCRITICALERRORS);
	bool success = GetFileAttributes(strFilename.c_str()) != INVALID_FILE_ATTRIBUTES;
	SetErrorMode(OldMode);
	return success;
#else
	std::string utf8 = convertWStringToUtf8(strFilename);
	struct stat stFileInfo;
	int intStat = stat(utf8.c_str(),&stFileInfo);
	return intStat == 0;
#endif
}

bool copyFile(const std::wstring& existingFile, const std::wstring& newFile)
{
#ifdef _WIN32
	return CopyFileW(existingFile.c_str(),newFile.c_str(),false) != FALSE;
#else
	unsigned char buffer[BUFSIZ];
	bool error = false;

	std::string existingUtf8 = convertWStringToUtf8(existingFile);
	std::string newUtf8 = convertWStringToUtf8(newFile);

	FILE* input = fopen(existingUtf8.c_str(),"rb");
	FILE* output = fopen(newUtf8.c_str(),"wb");

	if (input == NULL || output == NULL)
		return false;

	size_t n;
	while ((n = fread(buffer,1,BUFSIZ,input)) > 0)
	{
		if (fwrite(buffer,1,n,output) != n)
			error = true;
	}

	fclose(input);
	fclose(output);
	return !error;
#endif
}

bool deleteFile(const std::wstring& fileName)
{
#ifdef _WIN32
	return DeleteFileW(fileName.c_str()) != FALSE;
#else
	std::string utf8 = convertWStringToUtf8(fileName);
	return unlink(utf8.c_str()) == 0;
#endif
}

FILE* openFile(const std::wstring& fileName, OpenFileMode mode)
{
#ifdef _WIN32
	switch (mode)
	{
	case OpenFileMode::ReadBinary:
		return _wfopen(fileName.c_str(),L"rb");
	case OpenFileMode::WriteBinary:
		return _wfopen(fileName.c_str(),L"wb");
	case OpenFileMode::ReadWriteBinary:
		return _wfopen(fileName.c_str(),L"rb+");
	}
#else
	std::string nameUtf8 = convertWStringToUtf8(fileName);
	
	switch (mode)
	{
	case OpenFileMode::ReadBinary:
		return fopen(nameUtf8.c_str(),"rb");
	case OpenFileMode::WriteBinary:
		return fopen(nameUtf8.c_str(),"wb");
	case OpenFileMode::ReadWriteBinary:
		return fopen(nameUtf8.c_str(),"rb+");
	}
#endif

	return NULL;
}

std::wstring getCurrentDirectory()
{
#ifdef _WIN32
	wchar_t dir[MAX_PATH];
	_wgetcwd(dir,MAX_PATH-1);
	return dir;
#else
	char* dir = getcwd(NULL,0);
	std::wstring result = convertUtf8ToWString(dir);
	free(dir);
	return result;
#endif
}

void changeDirectory(const std::wstring& dir)
{
#ifdef _WIN32
	_wchdir(dir.c_str());
#else
	std::string utf8 = convertWStringToUtf8(dir);
	chdir(utf8.c_str());
#endif
}

std::wstring toWLowercase(const std::string& str)
{
	std::wstring result;
	for (size_t i = 0; i < str.size(); i++)
	{
		result += tolower(str[i]);
	}

	return result;
}

std::wstring getFileNameFromPath(const std::wstring& path)
{
	size_t n = path.find_last_of(L"/\\");
	if (n == path.npos)
		return path;
	return path.substr(n);
}

size_t replaceAll(std::wstring& str, const wchar_t* oldValue,const std::wstring& newValue)
{
	size_t pos = 0;
	size_t len = wcslen(oldValue);

	size_t count = 0;
	while ((pos = str.find(oldValue, pos)) != std::string::npos)
	{
		str.replace(pos,len,newValue);
		pos += newValue.length();
		count++;
	}

	return count;
}

bool startsWith(const std::wstring& str, const wchar_t* value, size_t stringPos)
{
	while (*value != 0 && stringPos < str.size())
	{
		if (str[stringPos++] != *value++)
			return false;
	}

	return *value == 0;
}

bool isAbsolutePath(const std::wstring& path)
{
#ifdef _WIN32
	return path.size() > 2 && (path[1] == ':' || (path[0] == '\\' && path[1] == '\\'));
#else
	return path.size() >= 1 && path[0] == '/';
#endif
}
