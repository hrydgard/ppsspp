#pragma once
#include <vector>
#include "Util/Util.h"
#include "Util/FileClasses.h"

class Logger
{
public:
	enum ErrorType { Warning, Error, FatalError, Notice };

	static void clear();
	static void printLine(const std::wstring& text);
	static void printLine(const std::string& text);
	
	template <typename... Args>
	static void printLine(const wchar_t* text, const Args&... args)
	{
		std::wstring message = formatString(text,args...);
		printLine(message);
	}

	static void print(const std::wstring& text);
	
	template <typename... Args>
	static void print(const wchar_t* text, const Args&... args)
	{
		std::wstring message = formatString(text,args...);
		print(message);
	}

	static void printError(ErrorType type, const std::wstring& text);
	static void printError(ErrorType type, const wchar_t* text);
	static void queueError(ErrorType type, const std::wstring& text);
	static void queueError(ErrorType type, const wchar_t* text);

	template <typename... Args>
	static void printError(ErrorType type, const wchar_t* text, const Args&... args)
	{
		std::wstring message = formatString(text,args...);
		printError(type,message);
	}
	
	template <typename... Args>
	static void queueError(ErrorType type, const wchar_t* text, const Args&... args)
	{
		std::wstring message = formatString(text,args...);
		queueError(type,message);
	}

	static void printQueue();
	static void clearQueue() { queue.clear(); };
	static StringList getErrors() { return errors; };
	static bool hasError() { return error; };
	static bool hasFatalError() { return fatalError; };
	static void setErrorOnWarning(bool b) { errorOnWarning = b; };
	static void setSilent(bool b) { silent = b; };
	static bool isSilent() { return silent; }
	static void suppressErrors() { ++suppressLevel; }
	static void unsuppressErrors() { if (suppressLevel) --suppressLevel; }
private:
	static std::wstring formatError(ErrorType type, const wchar_t* text);
	static void setFlags(ErrorType type);

	struct QueueEntry
	{
		ErrorType type;
		std::wstring text;
	};

	static std::vector<QueueEntry> queue;
	static std::vector<std::wstring> errors;
	static bool error;
	static bool fatalError;
	static bool errorOnWarning;
	static bool silent;
	static int suppressLevel;
};

class TempData
{
public:
	void setFileName(const std::wstring& name) { file.setFileName(name); };
	void clear() { file.setFileName(L""); }
	void start();
	void end();
	void writeLine(int64_t memoryAddress, const std::wstring& text);
	bool isOpen() { return file.isOpen(); }
private:
	TextFile file;
};
