#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Mostly just a toy to understand SPIR-V better. Not an accurate SPIR-V disassembler,
// and the result will not reassemble.

bool DisassembleSPIRV(std::vector<uint32_t> spirv, std::string *output);