#pragma once

#pragma once

#include <string>

#include "file/dialog.h"

bool OpenFileDialog(const char *title, const char *extension, std::string *filename);
bool SaveFileDialog(const char *title, const char *extension, std::string *filename);
