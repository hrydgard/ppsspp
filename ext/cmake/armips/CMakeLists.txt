cmake_minimum_required(VERSION 2.8)
project(armips) 

option(ARMIPS_REGEXP "Enable regexp expression functions" ON)

if(NOT WIN32 AND NOT ANDROID)
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")
endif()

set(ARMIPS_PATH ../../armips)

if(ARMIPS_REGEXP)
	add_definitions(-DARMIPS_REGEXP=1)
endif()

include_directories(${ARMIPS_PATH})

add_library(armips
	${ARMIPS_PATH}/stdafx.cpp
	${ARMIPS_PATH}/stdafx.h

	${ARMIPS_PATH}/Util/ByteArray.cpp
	${ARMIPS_PATH}/Util/ByteArray.h
	${ARMIPS_PATH}/Util/CRC.cpp
	${ARMIPS_PATH}/Util/CRC.h
	${ARMIPS_PATH}/Util/EncodingTable.cpp
	${ARMIPS_PATH}/Util/EncodingTable.h
	${ARMIPS_PATH}/Util/FileClasses.cpp
	${ARMIPS_PATH}/Util/FileClasses.h
	${ARMIPS_PATH}/Util/Util.cpp
	${ARMIPS_PATH}/Util/Util.h

	${ARMIPS_PATH}/Core/ELF/ElfTypes.h
	${ARMIPS_PATH}/Core/ELF/ElfRelocator.cpp
	${ARMIPS_PATH}/Core/ELF/ElfRelocator.h
	${ARMIPS_PATH}/Core/ELF/ElfFile.cpp
	${ARMIPS_PATH}/Core/ELF/ElfFile.h
	${ARMIPS_PATH}/Core/Assembler.cpp
	${ARMIPS_PATH}/Core/Assembler.h
	${ARMIPS_PATH}/Core/Common.cpp
	${ARMIPS_PATH}/Core/Common.h
	${ARMIPS_PATH}/Core/Expression.cpp
	${ARMIPS_PATH}/Core/Expression.h
	${ARMIPS_PATH}/Core/ExpressionFunctions.cpp
	${ARMIPS_PATH}/Core/ExpressionFunctions.h
	${ARMIPS_PATH}/Core/FileManager.cpp
	${ARMIPS_PATH}/Core/FileManager.h
	${ARMIPS_PATH}/Core/Misc.cpp
	${ARMIPS_PATH}/Core/Misc.h
	${ARMIPS_PATH}/Core/SymbolData.cpp
	${ARMIPS_PATH}/Core/SymbolData.h
	${ARMIPS_PATH}/Core/SymbolTable.cpp
	${ARMIPS_PATH}/Core/SymbolTable.h

	${ARMIPS_PATH}/Commands/CAssemblerCommand.cpp
	${ARMIPS_PATH}/Commands/CAssemblerCommand.h
	${ARMIPS_PATH}/Commands/CAssemblerLabel.cpp
	${ARMIPS_PATH}/Commands/CAssemblerLabel.h
	${ARMIPS_PATH}/Commands/CDirectiveArea.cpp
	${ARMIPS_PATH}/Commands/CDirectiveArea.h
	${ARMIPS_PATH}/Commands/CDirectiveConditional.cpp
	${ARMIPS_PATH}/Commands/CDirectiveConditional.h
	${ARMIPS_PATH}/Commands/CDirectiveData.cpp
	${ARMIPS_PATH}/Commands/CDirectiveData.h
	${ARMIPS_PATH}/Commands/CDirectiveFile.cpp
	${ARMIPS_PATH}/Commands/CDirectiveFile.h
	${ARMIPS_PATH}/Commands/CDirectiveMessage.cpp
	${ARMIPS_PATH}/Commands/CDirectiveMessage.h
	${ARMIPS_PATH}/Commands/CommandSequence.cpp
	${ARMIPS_PATH}/Commands/CommandSequence.h

	${ARMIPS_PATH}/Archs/Architecture.h
	${ARMIPS_PATH}/Archs/Architecture.cpp

	${ARMIPS_PATH}/Archs/ARM/Arm.cpp
	${ARMIPS_PATH}/Archs/ARM/Arm.h
	${ARMIPS_PATH}/Archs/ARM/ArmOpcodes.cpp
	${ARMIPS_PATH}/Archs/ARM/ArmOpcodes.h
	${ARMIPS_PATH}/Archs/ARM/ArmParser.cpp
	${ARMIPS_PATH}/Archs/ARM/ArmParser.h
	${ARMIPS_PATH}/Archs/ARM/ArmElfRelocator.cpp
	${ARMIPS_PATH}/Archs/ARM/ArmElfRelocator.h
	${ARMIPS_PATH}/Archs/ARM/ArmExpressionFunctions.cpp
	${ARMIPS_PATH}/Archs/ARM/ArmExpressionFunctions.h
	${ARMIPS_PATH}/Archs/ARM/CArmInstruction.cpp
	${ARMIPS_PATH}/Archs/ARM/CArmInstruction.h
	${ARMIPS_PATH}/Archs/ARM/CThumbInstruction.cpp
	${ARMIPS_PATH}/Archs/ARM/CThumbInstruction.h
	${ARMIPS_PATH}/Archs/ARM/Pool.cpp
	${ARMIPS_PATH}/Archs/ARM/Pool.h
	${ARMIPS_PATH}/Archs/ARM/ThumbOpcodes.cpp
	${ARMIPS_PATH}/Archs/ARM/ThumbOpcodes.h

	${ARMIPS_PATH}/Archs/MIPS/CMipsInstruction.cpp
	${ARMIPS_PATH}/Archs/MIPS/CMipsInstruction.h
	${ARMIPS_PATH}/Archs/MIPS/Mips.cpp
	${ARMIPS_PATH}/Archs/MIPS/Mips.h
	${ARMIPS_PATH}/Archs/MIPS/MipsElfFile.cpp
	${ARMIPS_PATH}/Archs/MIPS/MipsElfFile.h
	${ARMIPS_PATH}/Archs/MIPS/MipsElfRelocator.cpp
	${ARMIPS_PATH}/Archs/MIPS/MipsElfRelocator.h
	${ARMIPS_PATH}/Archs/MIPS/MipsExpressionFunctions.cpp
	${ARMIPS_PATH}/Archs/MIPS/MipsExpressionFunctions.h
	${ARMIPS_PATH}/Archs/MIPS/MipsMacros.cpp
	${ARMIPS_PATH}/Archs/MIPS/MipsMacros.h
	${ARMIPS_PATH}/Archs/MIPS/MipsOpcodes.cpp
	${ARMIPS_PATH}/Archs/MIPS/MipsOpcodes.h
	${ARMIPS_PATH}/Archs/MIPS/MipsParser.cpp
	${ARMIPS_PATH}/Archs/MIPS/MipsParser.h
	${ARMIPS_PATH}/Archs/MIPS/PsxRelocator.cpp
	${ARMIPS_PATH}/Archs/MIPS/PsxRelocator.h

	${ARMIPS_PATH}/Parser/DirectivesParser.cpp
	${ARMIPS_PATH}/Parser/DirectivesParser.h
	${ARMIPS_PATH}/Parser/ExpressionParser.cpp
	${ARMIPS_PATH}/Parser/ExpressionParser.h
	${ARMIPS_PATH}/Parser/Parser.cpp
	${ARMIPS_PATH}/Parser/Parser.h
	${ARMIPS_PATH}/Parser/Tokenizer.cpp
	${ARMIPS_PATH}/Parser/Tokenizer.h

	${ARMIPS_PATH}/ext/tinyformat/tinyformat.h
)

target_include_directories(armips BEFORE PUBLIC ${ARMIPS_PATH})
set_target_properties(armips PROPERTIES
  EXCLUDE_FROM_ALL ON
)
