#pragma once
#include <list>
#include "Util.h"

class BinaryFile
{
public:
	enum Mode { Read, Write, ReadWrite };

	BinaryFile();
	~BinaryFile();

	bool open(const std::wstring& fileName, Mode mode);
	bool open(Mode mode);
	bool isOpen() { return handle != NULL; };
	bool atEnd() { return isOpen() && mode != Write && ftell(handle) == size_; };
	void setPos(long pos) { if (isOpen()) fseek(handle,pos,SEEK_SET); };
	long pos() { return isOpen() ? ftell(handle) : -1; }
	long size() { return size_; };
	void close();
	
	void setFileName(const std::wstring& name) { fileName = name; };
	const std::wstring& getFileName() { return fileName; };

	size_t read(void* dest, size_t length);
	size_t write(void* source, size_t length);
private:
	FILE* handle;
	std::wstring fileName;
	Mode mode;
	long size_;
};

class TextFile
{
public:
	enum Encoding { ASCII, UTF8, UTF16LE, UTF16BE, SJIS, GUESS };
	enum Mode { Read, Write };
	
	TextFile();
	~TextFile();
	void openMemory(const std::wstring& content);
	bool open(const std::wstring& fileName, Mode mode, Encoding defaultEncoding = GUESS);
	bool open(Mode mode, Encoding defaultEncoding = GUESS);
	bool isOpen() { return fromMemory || handle != NULL; };
	bool atEnd() { return isOpen() && mode == Read && tell() >= size_; };
	long size() { return size_; };
	void close();

	bool hasGuessedEncoding() { return guessedEncoding; };
	bool isFromMemory() { return fromMemory; }
	int getNumLines() { return lineCount; }

	void setFileName(const std::wstring& name) { fileName = name; };
	const std::wstring& getFileName() { return fileName; };

	wchar_t readCharacter();
	std::wstring readLine();
	StringList readAll();
	void writeCharacter(wchar_t character);
	void write(const wchar_t* line);
	void write(const std::wstring& line);
	void write(const char* value);
	void write(const std::string& value);
	void writeLine(const wchar_t* line);
	void writeLine(const std::wstring& line);
	void writeLine(const char* line);
	void writeLine(const std::string& line);
	void writeLines(StringList& list);
	
	template <typename... Args>
	void writeFormat(const wchar_t* text, const Args&... args)
	{
		std::wstring message = formatString(text,args...);
		write(message);
	}

	bool hasError() { return errorText.size() != 0 && !errorRetrieved; };
	const std::wstring& getErrorText() { errorRetrieved = true; return errorText; };
private:
	long tell();
	void seek(long pos);

	FILE* handle;
	std::wstring fileName;
	Encoding encoding;
	Mode mode;
	bool recursion;
	bool guessedEncoding;
	long size_;
	std::wstring errorText;
	bool errorRetrieved;
	bool fromMemory;
	std::wstring content;
	size_t contentPos;
	int lineCount;

	std::string buf;
	size_t bufPos;

	inline unsigned char bufGetChar()
	{
		if (buf.size() <= bufPos)
		{
			bufFillRead();
			if (buf.size() == 0)
				return 0;
		}
		return buf[bufPos++];
	}
	inline unsigned short bufGet16LE()
	{
		char c1 = bufGetChar();
		char c2 = bufGetChar();
		return c1 | (c2 << 8);
	}
	inline unsigned short bufGet16BE()
	{
		char c1 = bufGetChar();
		char c2 = bufGetChar();
		return c2 | (c1 << 8);
	}

	void bufPut(const void *p, const size_t len);
	void bufPut(const char c);

	void bufFillRead();
	void bufDrainWrite();
};

wchar_t sjisToUnicode(unsigned short);
TextFile::Encoding getEncodingFromString(const std::wstring& str);
