#include "stdafx.h"
#include "Commands/CAssemblerCommand.h"
#include "Core/Common.h"

CAssemblerCommand::CAssemblerCommand()
{
	FileNum = Global.FileInfo.FileNum;
	FileLine = Global.FileInfo.LineNumber;
	section = Global.Section;
}

void CAssemblerCommand::applyFileInfo()
{
	Global.FileInfo.FileNum = FileNum;
	Global.FileInfo.LineNumber = FileLine;
}
