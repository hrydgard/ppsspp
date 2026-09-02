#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "Common/CommonTypes.h"

class PointerWrap;

void Register_sceReg();

void __RegInit();
void __RegShutdown();
void __RegDoState(PointerWrap &p);

bool __RegTestStoreRoundTrip(std::string *errorString);
bool __RegGetValueForDebugger(std::string_view path, std::string_view name, int *type, std::vector<u8> *data);
bool __RegSetValueForDebugger(std::string_view path, std::string_view name, int type, const std::vector<u8> &data, std::string *errorString);
bool __RegGetInt(std::string_view path, std::string_view name, u32 *value);
bool __RegGetString(std::string_view path, std::string_view name, std::string *value);
bool __RegSetInt(std::string_view path, std::string_view name, u32 value);
bool __RegSetString(std::string_view path, std::string_view name, std::string_view value);
bool __RegGetBinary(std::string_view path, std::string_view name, std::vector<u8> *value);
bool __RegSetBinary(std::string_view path, std::string_view name, const std::vector<u8> &value);
