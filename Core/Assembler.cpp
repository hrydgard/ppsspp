#include "stdafx.h"
#include "Assembler.h"
#include "Core/Common.h"
#include "Commands/CAssemblerCommand.h"
#include "Core/FileManager.h"
#include "Parser/Parser.h"
#include "Archs/ARM/Arm.h"
#include "Archs/MIPS/Mips.h"
#include <thread>

void AddFileName(const std::wstring& FileName)
{
	Global.FileInfo.FileNum = (int) Global.FileInfo.FileList.size();
	Global.FileInfo.FileList.push_back(FileName);
	Global.FileInfo.LineNumber = 0;
}

bool encodeAssembly(CAssemblerCommand* content, SymbolData& symData, TempData& tempData)
{
	bool Revalidate;
	
	Arm.Pass2();
	Mips.Pass2();

	int validationPasses = 0;
	do	// loop until everything is constant
	{
		Global.validationPasses = validationPasses;
		Logger::clearQueue();
		Revalidate = false;

		if (validationPasses >= 100)
		{
			Logger::queueError(Logger::Error,L"Stuck in infinite validation loop");
			break;
		}

		g_fileManager->reset();

#ifdef _DEBUG
		if (!Logger::isSilent())
			printf("Validate %d...\n",validationPasses);
#endif

		if (Global.memoryMode)
			g_fileManager->openFile(Global.memoryFile,true);

		Revalidate = content->Validate();

		Arm.Revalidate();
		Mips.Revalidate();

		if (Global.memoryMode)
			g_fileManager->closeFile();

		validationPasses++;
	} while (Revalidate == true);

	Logger::printQueue();
	if (Logger::hasError() == true)
	{
		return false;
	}

#ifdef _DEBUG
	if (!Logger::isSilent())
		printf("Encode...\n");
#endif

	// and finally encode
	if (Global.memoryMode)
		g_fileManager->openFile(Global.memoryFile,false);

	auto writeTempData = [&]()
	{
		tempData.start();
		if (tempData.isOpen())
			content->writeTempData(tempData);
		tempData.end();
	};

	auto writeSymData = [&]()
	{
		content->writeSymData(symData);
		symData.write();
	};

	// writeTempData, writeSymData and encode all access the same
	// memory but never change, so they can run in parallel
	if (Global.multiThreading)
	{
		std::thread tempThread(writeTempData);
		std::thread symThread(writeSymData);

		content->Encode();

		tempThread.join();
		symThread.join();
	} else {
		writeTempData();
		writeSymData();
		content->Encode();
	}

	if (g_fileManager->hasOpenFile())
	{
		if (!Global.memoryMode)
			Logger::printError(Logger::Warning,L"File not closed");
		g_fileManager->closeFile();
	}

	return true;
}

bool runArmips(ArmipsArguments& arguments)
{
	// initialize and reset global data
	Global.Section = 0;
	Global.nocash = false;
	Global.FileInfo.FileCount = 0;
	Global.FileInfo.TotalLineCount = 0;
	Global.relativeInclude = false;
	Global.validationPasses = 0;
	Global.multiThreading = true;
	Arch = &InvalidArchitecture;

	Tokenizer::clearEquValues();
	Logger::clear();
	Global.Table.clear();
	Global.symbolTable.clear();

	Global.FileInfo.FileList.clear();
	Global.FileInfo.FileCount = 0;
	Global.FileInfo.TotalLineCount = 0;
	Global.FileInfo.LineNumber = 0;
	Global.FileInfo.FileNum = 0;

	Arm.clear();

	// process arguments
	Parser parser;
	SymbolData symData;
	TempData tempData;
	
	Logger::setSilent(arguments.silent);
	Logger::setErrorOnWarning(arguments.errorOnWarning);

	if (!arguments.symFileName.empty())
		symData.setNocashSymFileName(arguments.symFileName,arguments.symFileVersion);

	if (!arguments.tempFileName.empty())
		tempData.setFileName(arguments.tempFileName);

	Token token;
	for (size_t i = 0; i < arguments.equList.size(); i++)
	{
		parser.addEquation(token,arguments.equList[i].name, arguments.equList[i].value);
	}

	Global.symbolTable.addLabels(arguments.labels);

	if (Logger::hasError())
		return false;

	// run assembler
	TextFile input;
	switch (arguments.mode)
	{
	case ArmipsMode::FILE:
		Global.memoryMode = false;		
		if (input.open(arguments.inputFileName,TextFile::Read) == false)
		{
			Logger::printError(Logger::Error,L"Could not open file");
			return false;
		}
		break;
	case ArmipsMode::MEMORY:
		Global.memoryMode = true;
		Global.memoryFile = arguments.memoryFile;
		input.openMemory(arguments.content);
		break;
	}

	CAssemblerCommand* content = parser.parseFile(input);
	Logger::printQueue();

	bool result = !Logger::hasError();
	if (result == true && content != nullptr)
		result = encodeAssembly(content, symData, tempData);
	
	if (g_fileManager->hasOpenFile())
	{
		if (!Global.memoryMode)
			Logger::printError(Logger::Warning,L"File not closed");
		g_fileManager->closeFile();
	}

	// return errors
	if (arguments.errorsResult != NULL)
	{
		StringList errors = Logger::getErrors();
		for (size_t i = 0; i < errors.size(); i++)
			arguments.errorsResult->push_back(errors[i]);
	}

	return result;
}
