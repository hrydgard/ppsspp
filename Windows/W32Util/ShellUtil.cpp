#pragma warning(disable:4091)  // workaround bug in VS2015 headers

#include "Windows/stdafx.h"

#include <functional>
#include <thread>

#include "Common/Data/Encoding/Utf8.h"
#include "ShellUtil.h"

#include <shlobj.h>
#include <commdlg.h>
#include <cderr.h>

namespace W32Util
{
	std::string BrowseForFolder(HWND parent, std::string_view title, std::string_view initialPath) {
		std::wstring titleString = ConvertUTF8ToWString(title);
		return BrowseForFolder(parent, titleString.c_str(), initialPath);
	}

	static int CALLBACK BrowseFolderCallback(HWND hwnd, UINT uMsg, LPARAM lParam, LPARAM lpData) {
		if (uMsg == BFFM_INITIALIZED) {
			LPCTSTR path = reinterpret_cast<LPCTSTR>(lpData);
			::SendMessage(hwnd, BFFM_SETSELECTION, true, (LPARAM)path);
		}
		return 0;
	}

	std::string BrowseForFolder(HWND parent, const wchar_t *title, std::string_view initialPath) {
		BROWSEINFO info{};
		info.hwndOwner = parent;
		info.lpszTitle = title;
		info.ulFlags = BIF_EDITBOX | BIF_RETURNONLYFSDIRS | BIF_USENEWUI | BIF_NEWDIALOGSTYLE;

		std::wstring initialPathW;

		if (!initialPath.empty()) {
			initialPathW = ConvertUTF8ToWString(initialPath);
			info.lParam = reinterpret_cast<LPARAM>(initialPathW.c_str());
			info.lpfn = BrowseFolderCallback;
		}

		//info.pszDisplayName
		auto idList = SHBrowseForFolder(&info);
		HMODULE shell32 = GetModuleHandle(L"shell32.dll");
		typedef BOOL (WINAPI *SHGetPathFromIDListEx_f)(PCIDLIST_ABSOLUTE pidl, PWSTR pszPath, DWORD cchPath, GPFIDL_FLAGS uOpts);
		SHGetPathFromIDListEx_f SHGetPathFromIDListEx_ = nullptr;
		if (shell32)
			SHGetPathFromIDListEx_ = (SHGetPathFromIDListEx_f)GetProcAddress(shell32, "SHGetPathFromIDListEx");

		std::string result;
		if (SHGetPathFromIDListEx_) {
			std::wstring temp;
			do {
				// Assume it's failing if it goes on too long.
				if (temp.size() > 32768 * 10) {
					temp.clear();
					break;
				}
				temp.resize(temp.size() + MAX_PATH);
			} while (SHGetPathFromIDListEx_(idList, &temp[0], (DWORD)temp.size(), GPFIDL_DEFAULT) == 0);
			result = ConvertWStringToUTF8(temp);
		} else {
			wchar_t temp[MAX_PATH]{};
			SHGetPathFromIDList(idList, temp);
			result = ConvertWStringToUTF8(temp);
		}

		CoTaskMemFree(idList);
		return result;
	}

	bool BrowseForFileName(bool _bLoad, HWND _hParent, const wchar_t *_pTitle,
		const wchar_t *_pInitialFolder, const wchar_t *_pFilter, const wchar_t *_pExtension,
		std::string &_strFileName) {
		// Let's hope this is large enough, don't want to trigger the dialog twice...
		std::wstring filenameBuffer(32768 * 10, '\0');

		OPENFILENAME ofn{ sizeof(OPENFILENAME) };

		auto resetFileBuffer = [&] {
			ofn.nMaxFile = (DWORD)filenameBuffer.size();
			ofn.lpstrFile = &filenameBuffer[0];
			if (!_strFileName.empty())
				wcsncpy(ofn.lpstrFile, ConvertUTF8ToWString(_strFileName).c_str(), filenameBuffer.size() - 1);
		};

		resetFileBuffer();
		ofn.lpstrInitialDir = _pInitialFolder;
		ofn.lpstrFilter = _pFilter;
		ofn.lpstrFileTitle = nullptr;
		ofn.nMaxFileTitle = 0;
		ofn.lpstrDefExt = _pExtension;
		ofn.hwndOwner = _hParent;
		ofn.Flags = OFN_NOCHANGEDIR | OFN_EXPLORER;
		if (!_bLoad)
			ofn.Flags |= OFN_HIDEREADONLY;

		int success = _bLoad ? GetOpenFileName(&ofn) : GetSaveFileName(&ofn);
		if (success == 0 && CommDlgExtendedError() == FNERR_BUFFERTOOSMALL) {
			size_t sz = *(unsigned short *)&filenameBuffer[0];
			// Documentation is unclear if this is WCHARs to CHARs.
			filenameBuffer.resize(filenameBuffer.size() + sz * 2);
			resetFileBuffer();
			success = _bLoad ? GetOpenFileName(&ofn) : GetSaveFileName(&ofn);
		}

		if (success) {
			_strFileName = ConvertWStringToUTF8(ofn.lpstrFile);
			return true;
		}
		return false;
	}
	
	std::vector<std::string> BrowseForFileNameMultiSelect(bool _bLoad, HWND _hParent, const wchar_t *_pTitle,
		const wchar_t *_pInitialFolder, const wchar_t *_pFilter, const wchar_t *_pExtension) {
		// Let's hope this is large enough, don't want to trigger the dialog twice...
		std::wstring filenameBuffer(32768 * 10, '\0');

		OPENFILENAME ofn{ sizeof(OPENFILENAME) };

		auto resetFileBuffer = [&] {
			ofn.nMaxFile = (DWORD)filenameBuffer.size();
			ofn.lpstrFile = &filenameBuffer[0];
		};

		resetFileBuffer();
		ofn.lpstrInitialDir = _pInitialFolder;
		ofn.lpstrFilter = _pFilter;
		ofn.lpstrFileTitle = nullptr;
		ofn.nMaxFileTitle = 0;
		ofn.lpstrDefExt = _pExtension;
		ofn.hwndOwner = _hParent;
		ofn.Flags = OFN_NOCHANGEDIR | OFN_EXPLORER | OFN_ALLOWMULTISELECT;
		if (!_bLoad)
			ofn.Flags |= OFN_HIDEREADONLY;

		std::vector<std::string> files;
		int success = _bLoad ? GetOpenFileName(&ofn) : GetSaveFileName(&ofn);
		if (success == 0 && CommDlgExtendedError() == FNERR_BUFFERTOOSMALL) {
			size_t sz = *(unsigned short *)&filenameBuffer[0];
			// Documentation is unclear if this is WCHARs to CHARs.
			filenameBuffer.resize(filenameBuffer.size() + sz * 2);
			resetFileBuffer();
			success = _bLoad ? GetOpenFileName(&ofn) : GetSaveFileName(&ofn);
		}

		if (success) {
			std::string directory = ConvertWStringToUTF8(ofn.lpstrFile);
			wchar_t *temp = ofn.lpstrFile;
			temp += wcslen(temp) + 1;
			if (*temp == 0) {
				//we only got one file
				files.push_back(directory);
			} else {
				while (*temp) {
					files.emplace_back(directory + "\\" + ConvertWStringToUTF8(temp));
					temp += wcslen(temp) + 1;
				}
			}
		}
		return files;
	}

	std::string UserDocumentsPath() {
		std::string result;
		HMODULE shell32 = GetModuleHandle(L"shell32.dll");
		typedef HRESULT(WINAPI *SHGetKnownFolderPath_f)(REFKNOWNFOLDERID rfid, DWORD dwFlags, HANDLE hToken, PWSTR *ppszPath);
		SHGetKnownFolderPath_f SHGetKnownFolderPath_ = nullptr;
		if (shell32)
			SHGetKnownFolderPath_ = (SHGetKnownFolderPath_f)GetProcAddress(shell32, "SHGetKnownFolderPath");
		if (SHGetKnownFolderPath_) {
			PWSTR path = nullptr;
			if (SHGetKnownFolderPath_(FOLDERID_Documents, 0, nullptr, &path) == S_OK) {
				result = ConvertWStringToUTF8(path);
			}
			if (path)
				CoTaskMemFree(path);
		} else {
			wchar_t path[MAX_PATH];
			if (SHGetFolderPath(nullptr, CSIDL_PERSONAL, nullptr, SHGFP_TYPE_CURRENT, path) == S_OK) {
				result = ConvertWStringToUTF8(path);
			}
		}

		return result;
	}


// http://msdn.microsoft.com/en-us/library/aa969393.aspx
HRESULT CreateLink(LPCWSTR lpszPathObj, LPCWSTR lpszArguments, LPCWSTR lpszPathLink, LPCWSTR lpszDesc) {
	HRESULT hres;
	IShellLink *psl = nullptr;
	hres = CoInitializeEx(NULL, COINIT_MULTITHREADED);
	if (FAILED(hres))
		return hres;

	// Get a pointer to the IShellLink interface. It is assumed that CoInitialize
	// has already been called.
	hres = CoCreateInstance(CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, IID_IShellLink, (LPVOID *)&psl);
	if (SUCCEEDED(hres) && psl) {
		IPersistFile *ppf = nullptr;

		// Set the path to the shortcut target and add the description. 
		psl->SetPath(lpszPathObj);
		psl->SetArguments(lpszArguments);
		psl->SetDescription(lpszDesc);
		// psl->SetIconLocation(..)

		// Query IShellLink for the IPersistFile interface, used for saving the 
		// shortcut in persistent storage. 
		hres = psl->QueryInterface(IID_IPersistFile, (LPVOID *)&ppf);

		if (SUCCEEDED(hres) && ppf) {
			// Save the link by calling IPersistFile::Save. 
			hres = ppf->Save(lpszPathLink, TRUE);
			ppf->Release();
		}
		psl->Release();
	}
	CoUninitialize();

	return hres;
}

bool CreateDesktopShortcut(const std::string &argumentPath, std::string gameTitle) {
	// Get the desktop folder
	wchar_t *pathbuf = new wchar_t[4096];
	SHGetFolderPath(0, CSIDL_DESKTOPDIRECTORY, NULL, SHGFP_TYPE_CURRENT, pathbuf);

	// Sanitize the game title for banned characters.
	const char bannedChars[] = "<>:\"/\\|?*";
	for (size_t i = 0; i < gameTitle.size(); i++) {
		for (char c : bannedChars) {
			if (gameTitle[i] == c) {
				gameTitle[i] = '_';
				break;
			}
		}
	}

	wcscat(pathbuf, L"\\");
	wcscat(pathbuf, ConvertUTF8ToWString(gameTitle).c_str());
	wcscat(pathbuf, L".lnk");

	std::wstring moduleFilename;
	size_t sz;
	do {
		moduleFilename.resize(moduleFilename.size() + MAX_PATH);
		// On failure, this will return the same value as passed in, but success will always be one lower.
		sz = GetModuleFileName(nullptr, &moduleFilename[0], (DWORD)moduleFilename.size());
	} while (sz >= moduleFilename.size());
	moduleFilename.resize(sz);

	// Need to flip the slashes in the filename.

	std::string sanitizedArgument = argumentPath;
	for (size_t i = 0; i < sanitizedArgument.size(); i++) {
		if (sanitizedArgument[i] == '/') {
			sanitizedArgument[i] = '\\';
		}
	}

	sanitizedArgument = "\"" + sanitizedArgument + "\"";

	CreateLink(moduleFilename.c_str(), ConvertUTF8ToWString(sanitizedArgument).c_str(), pathbuf, ConvertUTF8ToWString(gameTitle).c_str());

	// TODO: Also extract the game icon and convert to .ico, put it somewhere under Memstick, and set it.

	delete[] pathbuf;
	return false;
}

}  // namespace
