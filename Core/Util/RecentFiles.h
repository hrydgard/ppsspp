#pragma once

#include <functional>
#include <vector>
#include <string>

#include "Common/Data/Format/IniFile.h"

void ResetRecentIsosThread();
void SetRecentIsosThread(std::function<void()> f);
void LoadRecentIsos(const Section *recent, int maxRecent);
void SaveRecentIsos(Section *recent, int maxRecent);
void AddRecent(const std::string &resolvedFilename, int maxRecent);
void RemoveRecent(const std::string &resolvedFilename);
void CleanRecentIsos();
std::vector<std::string> GetRecentIsos();
bool HasRecentIsos();
void ClearRecentIsos();
