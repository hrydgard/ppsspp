#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <thread>

namespace W32Util
{
	// Can't make initialPath a string_view, need the null so might as well require it.
	std::string BrowseForFolder(HWND parent, std::string_view title, std::string_view initialPath);
	std::string BrowseForFolder(HWND parent, const wchar_t *title, std::string_view initialPath);
	bool BrowseForFileName (bool _bLoad, HWND _hParent, const wchar_t*_pTitle,
		const wchar_t *_pInitialFolder,const wchar_t *_pFilter,const wchar_t*_pExtension, 
		std::string& _strFileName);
	std::vector<std::string> BrowseForFileNameMultiSelect(bool _bLoad, HWND _hParent, const wchar_t*_pTitle,
		const wchar_t*_pInitialFolder,const wchar_t*_pFilter,const wchar_t*_pExtension);

	std::string UserDocumentsPath();

	bool CreateDesktopShortcut(std::string_view argumentPath, std::string_view gameTitle);
}  // namespace
