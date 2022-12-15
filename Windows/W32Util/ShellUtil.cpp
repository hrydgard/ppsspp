// NOTE: Apologies for the quality of this code, this is really from pre-opensource Dolphin - that is, 2003.

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
	std::string BrowseForFolder(HWND parent, const char *title)
	{
		std::wstring titleString = ConvertUTF8ToWString(title);
		return BrowseForFolder(parent, titleString.c_str());
	}

	std::string BrowseForFolder(HWND parent, const wchar_t *title)
	{
		BROWSEINFO info{};
		info.hwndOwner = parent;
		info.lpszTitle = title;
		info.ulFlags = BIF_EDITBOX | BIF_RETURNONLYFSDIRS | BIF_USENEWUI;

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

	//---------------------------------------------------------------------------------------------------
	// function WinBrowseForFileName
	//---------------------------------------------------------------------------------------------------
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

	AsyncBrowseDialog::AsyncBrowseDialog(HWND parent, UINT completeMsg, std::wstring title)
		: type_(DIR), parent_(parent), completeMsg_(completeMsg), title_(title), complete_(false), result_(false) {
		thread_ = new std::thread(std::bind(&AsyncBrowseDialog::Execute, this));
		thread_->detach();
	}

	AsyncBrowseDialog::AsyncBrowseDialog(Type type, HWND parent, UINT completeMsg, std::wstring title, std::wstring initialFolder, std::wstring filter, std::wstring extension)
		: type_(type), parent_(parent), completeMsg_(completeMsg), title_(title), initialFolder_(initialFolder), filter_(filter), extension_(extension), complete_(false), result_(false) {
		thread_ = new std::thread(std::bind(&AsyncBrowseDialog::Execute, this));
		thread_->detach();
	}

	AsyncBrowseDialog::~AsyncBrowseDialog() {
		delete thread_;
	}

	bool AsyncBrowseDialog::GetResult(std::string &filename) {
		filename = filename_;
		return result_;
	}

	void AsyncBrowseDialog::Execute() {
		switch (type_) {
		case DIR:
			filename_ = BrowseForFolder(parent_, title_.c_str());
			result_ = !filename_.empty();
			complete_ = true;
			break;

		case OPEN:
		case SAVE:
			result_ = BrowseForFileName(type_ == OPEN, parent_, title_.c_str(), initialFolder_.size() ? initialFolder_.c_str() : 0, filter_.c_str(), extension_.c_str(), filename_);
			complete_ = true;
			break;
		}

		PostMessage(parent_, completeMsg_, 0, 0);
	}
}
