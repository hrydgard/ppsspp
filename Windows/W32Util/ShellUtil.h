#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <cstdint>

class Path;

namespace W32Util {

std::string BrowseForFolder2(HWND parent, std::string_view title, std::string_view initialPath);
bool BrowseForFileName(bool _bLoad, HWND _hParent, const wchar_t*_pTitle,
	const wchar_t *_pInitialFolder, const wchar_t *_pFilter, const wchar_t*_pExtension,
	std::string& _strFileName);
std::vector<std::string> BrowseForFileNameMultiSelect(bool _bLoad, HWND _hParent, const wchar_t*_pTitle,
	const wchar_t*_pInitialFolder, const wchar_t*_pFilter, const wchar_t*_pExtension);

bool MoveToTrash(const Path &path);
void OpenDisplaySettings();
std::string UserDocumentsPath();

bool CreateDesktopShortcut(std::string_view argumentPath, std::string_view gameTitle, const Path &icoFile);
bool CreateICOFromPNGData(const uint8_t *imageData, size_t imageDataSize, const Path &icoPath);

}  // namespace W32Util
