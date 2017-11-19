#pragma once
#include <vector>
#include "../Util/FileClasses.h"
#include "SymbolData.h"

enum class Endianness { Big, Little };

class AssemblerFile
{
public:
	virtual ~AssemblerFile() { };
	
	virtual bool open(bool onlyCheck) = 0;
	virtual void close() = 0;
	virtual bool isOpen() = 0;
	virtual bool write(void* data, size_t length) = 0;
	virtual int64_t getVirtualAddress() = 0;
	virtual int64_t getPhysicalAddress() = 0;
	virtual int64_t getHeaderSize() = 0;
	virtual bool seekVirtual(int64_t virtualAddress) = 0;
	virtual bool seekPhysical(int64_t physicalAddress) = 0;
	virtual bool getModuleInfo(SymDataModuleInfo& info) { return false; };
	virtual bool hasFixedVirtualAddress() { return false; };
	virtual void beginSymData(SymbolData& symData) { };
	virtual void endSymData(SymbolData& symData) { };
	virtual const std::wstring& getFileName() = 0;
};

class GenericAssemblerFile: public AssemblerFile
{
public:
	GenericAssemblerFile(const std::wstring& fileName, int64_t headerSize, bool overwrite);
	GenericAssemblerFile(const std::wstring& fileName, const std::wstring& originalFileName, int64_t headerSize);

	virtual bool open(bool onlyCheck);
	virtual void close() { if (handle.isOpen()) handle.close(); };
	virtual bool isOpen() { return handle.isOpen(); };
	virtual bool write(void* data, size_t length);
	virtual int64_t getVirtualAddress() { return virtualAddress; };
	virtual int64_t getPhysicalAddress() { return virtualAddress-headerSize; };
	virtual int64_t getHeaderSize() { return headerSize; };
	virtual bool seekVirtual(int64_t virtualAddress);
	virtual bool seekPhysical(int64_t physicalAddress);
	virtual bool hasFixedVirtualAddress() { return true; };

	virtual const std::wstring& getFileName() { return fileName; };
	const std::wstring& getOriginalFileName() { return originalName; };
	int64_t getOriginalHeaderSize() { return originalHeaderSize; };
	void setHeaderSize(int64_t size) { headerSize = size; };

private:
	enum Mode { Open, Create, Copy };

	Mode mode;
	int64_t originalHeaderSize;
	int64_t headerSize;
	int64_t virtualAddress;
	BinaryFile handle;
	std::wstring fileName;
	std::wstring originalName;
};


class FileManager
{
public:
	FileManager();
	~FileManager();
	void reset();
	bool openFile(AssemblerFile* file, bool onlyCheck);
	void addFile(AssemblerFile* file);
	bool hasOpenFile() { return activeFile != NULL; };
	void closeFile();
	bool write(void* data, size_t length);
	bool writeU8(uint8_t data);
	bool writeU16(uint16_t data);
	bool writeU32(uint32_t data);
	bool writeU64(uint64_t data);
	int64_t getVirtualAddress();
	int64_t getPhysicalAddress();
	int64_t getHeaderSize();
	bool seekVirtual(int64_t virtualAddress);
	bool seekPhysical(int64_t physicalAddress);
	bool advanceMemory(size_t bytes);
	AssemblerFile* getOpenFile() { return activeFile; };
	void setEndianness(Endianness endianness) { this->endianness = endianness; };
	Endianness getEndianness() { return endianness; }
private:
	bool checkActiveFile();
	std::vector<AssemblerFile*> files;
	AssemblerFile* activeFile;
	Endianness endianness;
	Endianness ownEndianness;
};
