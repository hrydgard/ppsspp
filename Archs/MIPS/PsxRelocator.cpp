#include "stdafx.h"
#include "PsxRelocator.h"
#include "Core/Misc.h"
#include "Core/Common.h"
#include <map>
#include "Util/CRC.h"
#include "Core/FileManager.h"
#include "Util/Util.h"

struct PsxLibEntry
{
	std::wstring name;
	ByteArray data;
};

const unsigned char psxObjectFileMagicNum[6] = { 'L', 'N', 'K', '\x02', '\x2E', '\x07' };

std::vector<PsxLibEntry> loadPsxLibrary(const std::wstring& inputName)
{
	ByteArray input = ByteArray::fromFile(inputName);
	std::vector<PsxLibEntry> result;

	if (input.size() == 0)
		return result;

	if (memcmp(input.data(),psxObjectFileMagicNum,sizeof(psxObjectFileMagicNum)) == 0)
	{
		PsxLibEntry entry;
		entry.name = getFileNameFromPath(inputName);
		entry.data = input;
		result.push_back(entry);
		return result;
	}
	
	if (memcmp(input.data(),"LIB\x01",4) != 0)
		return result;

	size_t pos = 4;
	while (pos < input.size())
	{
		PsxLibEntry entry;
		
		for (int i = 0; i < 16 && input[pos+i] != ' '; i++)
		{
			entry.name += input[pos+i];
		}

		int size = input.getDoubleWord(pos+16);
		int skip = 20;

		while (input[pos+skip] != 0)
		{
			skip += input[pos+skip++];
		}

		skip++;

		entry.data = input.mid(pos+skip,size-skip);
		pos += size;

		result.push_back(entry);
	}

	return result;
}

size_t PsxRelocator::loadString(ByteArray& data, size_t pos, std::wstring& dest)
{
	dest = L"";
	int len = data[pos++];

	for (int i = 0; i < len; i++)
	{
		dest += data[pos++];
	}

	return len+1;
}

bool PsxRelocator::parseObject(ByteArray data, PsxRelocatorFile& dest)
{
	if (memcmp(data.data(),psxObjectFileMagicNum,sizeof(psxObjectFileMagicNum)) != 0)
		return false;

	size_t pos = 6;

	std::vector<PsxSegment>& segments = dest.segments;
	std::vector<PsxSymbol>& syms = dest.symbols;

	int activeSegment = -1;
	int lastSegmentPartStart = -1;
	while (pos < data.size())
	{
		switch (data[pos])
		{
		case 0x10:	// segment definition
			{
				PsxSegment seg;
				seg.id = data.getDoubleWord(pos+1);
				segments.push_back(seg);
				pos += 5;

				if (data[pos] != 8)
					return false;

				std::wstring& name = segments[segments.size()-1].name;
				pos += 1 + loadString(data,pos+1,name);
			}
			break;
		case 0x14:	// group?
			pos += data[pos+4]+5;
			break;
		case 0x1C:	// source file name
			pos += data[pos+3]+4;
			break;

		case 0x06:	// set segment id
			{
				int id = data.getWord(pos+1);
				pos += 3;
				
				int num = -1;
				for (size_t i = 0; i < segments.size(); i++)
					{
					if (segments[i].id == id)
					{
						num = (int) i;
						break;
					}
				}

				activeSegment = num;
			}
			break;
		case 0x02:	// append to data segment
			{
				int size = data.getWord(pos+1);
				pos += 3;

				ByteArray d = data.mid(pos,size);
				pos += size;

				lastSegmentPartStart = (int) segments[activeSegment].data.size();
				segments[activeSegment].data.append(d);
			}
			break;
		case 0x08:	// append zeroes data segment
			{
				int size = data.getWord(pos+1);
				pos += 3;

				ByteArray d;
				d.reserveBytes(size);
				segments[activeSegment].data.append(d);
			}
			break;
		case 0x0A:	// relocation data
			{
				int type = data[pos+1];
				pos += 2;

				PsxRelocation rel;
				rel.relativeOffset = 0;
				rel.filePos = (int) pos-2;

				switch (type)
				{
				case 0x10:	// 32 bit word
					rel.type = PsxRelocationType::WordLiteral;
					rel.segmentOffset = data.getWord(pos);
					pos += 2;
					break;
				case 0x4A:	// jal
					rel.type = PsxRelocationType::FunctionCall;
					rel.segmentOffset = data.getWord(pos);
					pos += 2;
					break;
				case 0x52:	// upper immerdiate
					rel.type = PsxRelocationType::UpperImmediate;
					rel.segmentOffset = data.getWord(pos);
					pos += 2;
					break;
				case 0x54:	// lower immediate (add)
					rel.type = PsxRelocationType::LowerImmediate;
					rel.segmentOffset = data.getWord(pos);
					pos += 2;
					break;
				default:
					return false;
				}

				rel.segmentOffset += lastSegmentPartStart;
checkothertype:
				int otherType = data[pos++];
				switch (otherType)
				{
				case 0x02:	// reference to symbol with id num
					rel.refType = PsxRelocationRefType::SymblId;
					rel.referenceId = data.getWord(pos);
					pos += 2;
					break;
				case 0x2C:	// ref to other segment?
					rel.refType = PsxRelocationRefType::SegmentOffset;

					switch (data[pos++])
					{
					case 0x00:
						rel.relativeOffset = data.getDoubleWord(pos);
						pos += 4;
						goto checkothertype;
					case 0x04:					
						rel.referenceId = data.getWord(pos);	// segment id
						pos += 2;
					
						if (data[pos++] != 0x00)
						{
							return false;
						}

						rel.referencePos = data.getDoubleWord(pos);
						pos += 4;
						break;
					default:
						return false;
					}
					break;
				case 0x2E:	// negative ref?
					rel.refType = PsxRelocationRefType::SegmentOffset;

					switch (data[pos++])
					{
					case 0x00:
						rel.relativeOffset = -data.getDoubleWord(pos);
						pos += 4;
						goto checkothertype;
					default:
						return false;
					}
					break;
				default:
					return false;
				}

				segments[activeSegment].relocations.push_back(rel);
			}
			break;
		case 0x12:	// internal symbol
			{
				PsxSymbol sym;
				sym.type = PsxSymbolType::Internal;
				sym.segment = data.getWord(pos+1);
				sym.offset = data.getDoubleWord(pos+3);
				pos += 7 + loadString(data,pos+7,sym.name);
				syms.push_back(sym);
			}
			break;
		case 0x0E:	// external symbol
			{
				PsxSymbol sym;
				sym.type = PsxSymbolType::External;
				sym.id = data.getWord(pos+1);
				pos += 3 + loadString(data,pos+3,sym.name);
				syms.push_back(sym);
			}
			break;
		case 0x30:	// bss symbol?
			{
				PsxSymbol sym;
				sym.type = PsxSymbolType::BSS;
				sym.id = data.getWord(pos+1);
				sym.segment = data.getWord(pos+3);
				sym.size = data.getDoubleWord(pos+5);
				pos += 9 + loadString(data,pos+9,sym.name);
				syms.push_back(sym);
			}
			break;
		case 0x0C:	// internal with id
			{
				PsxSymbol sym;
				sym.type = PsxSymbolType::InternalID;
				sym.id = data.getWord(pos+1);
				sym.segment = data.getWord(pos+3);
				sym.offset = data.getDoubleWord(pos+5);
				pos += 9 + loadString(data,pos+9,sym.name);
				syms.push_back(sym);
			}
			break;
		case 0x4A:	// function
			{
				PsxSymbol sym;
				sym.type = PsxSymbolType::Function;
				sym.segment = data.getWord(pos+1);
				sym.offset = data.getDoubleWord(pos+3);
				pos += 0x1D + loadString(data,pos+0x1D,sym.name);
				syms.push_back(sym);
			}
			break;
		case 0x4C:	// function size
			pos += 11;
			break;
		case 0x3C:	// ??
			pos += 3;
			break;
		case 0x00:	// ??
			pos++;
			break;
		case 0x32:	// ??
			pos += 3;
			break;
		case 0x3A:	// ??
			pos += 9;
			break;
		default:
			return false;
		}
	}

	return true;
}

bool PsxRelocator::init(const std::wstring& inputName)
{
	auto inputFiles = loadPsxLibrary(inputName);
	if (inputFiles.size() == 0)
	{
		Logger::printError(Logger::Error,L"Could not load library");
		return false;
	}

	reloc = new MipsElfRelocator();

	for (PsxLibEntry& entry: inputFiles)
	{
		PsxRelocatorFile file;
		file.name = entry.name;

		if (parseObject(entry.data,file) == false)
		{
			Logger::printError(Logger::Error,L"Could not load object file %s",entry.name);
			return false;
		}

		// init symbols
		for (PsxSymbol& sym: file.symbols)
		{
			std::wstring lowered = sym.name;
			std::transform(lowered.begin(), lowered.end(), lowered.begin(), ::towlower);

			sym.label = Global.symbolTable.getLabel(lowered,-1,-1);
			if (sym.label == NULL)
			{
				Logger::printError(Logger::Error,L"Invalid label name \"%s\"",sym.name);
				continue;
			}

			if (sym.label->isDefined() && sym.type != PsxSymbolType::External)
			{
				Logger::printError(Logger::Error,L"Label \"%s\" already defined",sym.name);
				continue;
			}

			sym.label->setOriginalName(sym.name);
		}

		files.push_back(file);
	}

	return true;
}

bool PsxRelocator::relocateFile(PsxRelocatorFile& file, int& relocationAddress)
{
	std::map<int,int> relocationOffsets;
	std::map<int,int> symbolOffsets;
	int start = relocationAddress;

	// assign addresses to segments
	for (PsxSegment& seg: file.segments)
	{
		int index = seg.id;
		size_t size = seg.data.size();
		
		relocationOffsets[index] = relocationAddress;
		relocationAddress += (int) size;

		while (relocationAddress % 4)
			relocationAddress++;
	}
	
	// parse/add/relocate symbols
	bool error = false;
	for (PsxSymbol& sym: file.symbols)
	{
		int pos;
		switch (sym.type)
		{
		case PsxSymbolType::Internal:
		case PsxSymbolType::Function:
			sym.label->setValue(relocationOffsets[sym.segment]+sym.offset);
			sym.label->setDefined(true);
			break;
		case PsxSymbolType::InternalID:
			pos = relocationOffsets[sym.segment]+sym.offset;
			sym.label->setValue(pos);
			sym.label->setDefined(true);
			symbolOffsets[sym.id] = pos;
			break;
		case PsxSymbolType::BSS:
			sym.label->setValue(relocationAddress);
			sym.label->setDefined(true);
			symbolOffsets[sym.id] = relocationAddress;
			relocationAddress += sym.size;
			
			while (relocationAddress % 4)
				relocationAddress++;
			break;
		case PsxSymbolType::External:
			if (sym.label->isDefined() == false)
			{
				Logger::queueError(Logger::Error,L"Undefined external symbol %s in file %s",sym.name,file.name);
				error = true;
				continue;
			}
			
			symbolOffsets[sym.id] = (int) sym.label->getValue();
			break;
		}
	}

	if (error)
		return false;

	size_t dataStart = outputData.size();
	outputData.reserveBytes(relocationAddress-start);

	// load code and data
	for (PsxSegment& seg: file.segments)
	{
		// relocate
		ByteArray sectionData = seg.data;
		for (PsxRelocation& rel: seg.relocations)
		{
			RelocationData relData;
			int pos = rel.segmentOffset;
			relData.opcode = sectionData.getDoubleWord(pos);

			switch (rel.refType)
			{
			case PsxRelocationRefType::SymblId:
				relData.relocationBase = symbolOffsets[rel.referenceId]+rel.relativeOffset;
				break;
			case PsxRelocationRefType::SegmentOffset:
				relData.relocationBase = relocationOffsets[rel.referenceId] + rel.referencePos+rel.relativeOffset;
				break;
			}
			
			switch (rel.type)
			{
			case PsxRelocationType::WordLiteral:
				reloc->relocateOpcode(R_MIPS_32,relData);
				break;
			case PsxRelocationType::UpperImmediate:
				reloc->relocateOpcode(R_MIPS_HI16,relData);
				break;
			case PsxRelocationType::LowerImmediate:
				reloc->relocateOpcode(R_MIPS_LO16,relData);
				break;
			case PsxRelocationType::FunctionCall:
				reloc->relocateOpcode(R_MIPS_26,relData);
				break;
			}

			sectionData.replaceDoubleWord(pos,relData.opcode);
		}

		size_t arrayStart = dataStart+relocationOffsets[seg.id]-start;
		memcpy(outputData.data(arrayStart),sectionData.data(),sectionData.size());
	}

	return true;
}

bool PsxRelocator::relocate(int& memoryAddress)
{
	int oldCrc = getCrc32(outputData.data(),outputData.size());
	outputData.clear();
	dataChanged = false;

	bool error = false;
	int start = memoryAddress;

	for (PsxRelocatorFile& file: files)
	{
		if (relocateFile(file,memoryAddress) == false)
			error = true;
	}
	
	int newCrc = getCrc32(outputData.data(),outputData.size());
	if (oldCrc != newCrc)
		dataChanged = true;

	memoryAddress -= start;
	return !error;
}


void PsxRelocator::writeSymbols(SymbolData& symData) const
{
	for (const PsxRelocatorFile& file: files)
	{
		for (const PsxSymbol& sym: file.symbols)
		{
			if (sym.type != PsxSymbolType::External)
				symData.addLabel(sym.label->getValue(),sym.name.c_str());
		}
	}
}

//
// DirectivePsxObjImport
//

DirectivePsxObjImport::DirectivePsxObjImport(const std::wstring& fileName)
{
	if (rel.init(fileName))
	{
	}
}

bool DirectivePsxObjImport::Validate()
{
	int memory = (int) g_fileManager->getVirtualAddress();
	rel.relocate(memory);
	g_fileManager->advanceMemory(memory);
	return rel.hasDataChanged();
}

void DirectivePsxObjImport::Encode() const
{
	const ByteArray& data = rel.getData();
	g_fileManager->write(data.data(),data.size());
}

void DirectivePsxObjImport::writeSymData(SymbolData& symData) const
{
	rel.writeSymbols(symData);
}
