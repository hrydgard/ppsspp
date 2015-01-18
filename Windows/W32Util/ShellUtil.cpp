// NOTE: Apologies for the quality of this code, this is really from pre-opensource Dolphin - that is, 2003.

#include "stdafx.h"
#include "shlobj.h"

#include "base/functional.h"
#include "thread/thread.h"
#include "util/text/utf8.h"
#include "ShellUtil.h"
#include "CommDlg.h"

#include <shlobj.h>
#include <commdlg.h>

namespace W32Util
{
	std::string BrowseForFolder(HWND parent, const char *title)
	{
		std::wstring titleString = ConvertUTF8ToWString(title);
		return BrowseForFolder(parent, titleString.c_str());
	}

	std::string BrowseForFolder(HWND parent, const wchar_t *title)
	{
		BROWSEINFO info;
		memset(&info,0,sizeof(info));
		info.hwndOwner = parent;
		info.lpszTitle = title;
		info.ulFlags = BIF_EDITBOX | BIF_RETURNONLYFSDIRS | BIF_USENEWUI;

		//info.pszDisplayName
		LPCITEMIDLIST idList = SHBrowseForFolder(&info);

		wchar_t temp[MAX_PATH];
		SHGetPathFromIDList(idList, temp);
		if (wcslen(temp))
			return ConvertWStringToUTF8(temp);
		else
			return "";
	}

	//---------------------------------------------------------------------------------------------------
	// function WinBrowseForFileName
	//---------------------------------------------------------------------------------------------------
	bool BrowseForFileName (bool _bLoad, HWND _hParent, const wchar_t *_pTitle,
		const wchar_t *_pInitialFolder,const wchar_t *_pFilter,const wchar_t *_pExtension, 
		std::string& _strFileName)
	{
		wchar_t szFile [MAX_PATH+1] = {0};
		wchar_t szFileTitle [MAX_PATH+1] = {0};

		OPENFILENAME ofn;

		ZeroMemory (&ofn,sizeof (ofn));

		ofn.lStructSize		= sizeof (OPENFILENAME);
		ofn.lpstrInitialDir = _pInitialFolder;
		ofn.lpstrFilter		= _pFilter;
		ofn.nMaxFile		= sizeof (szFile);
		ofn.lpstrFile		= szFile;
		ofn.lpstrFileTitle	= szFileTitle;
		ofn.nMaxFileTitle	= sizeof (szFileTitle);
		ofn.lpstrDefExt     = _pExtension;
		ofn.hwndOwner		= _hParent;
		ofn.Flags			= OFN_NOCHANGEDIR | OFN_EXPLORER | OFN_HIDEREADONLY;

		if (!_strFileName.empty())
			wcsncpy(ofn.lpstrFile, ConvertUTF8ToWString(_strFileName).c_str(), MAX_PATH);

		if (((_bLoad) ? GetOpenFileName(&ofn) : GetSaveFileName (&ofn)))
		{
			_strFileName = ConvertWStringToUTF8(ofn.lpstrFile);
			return true;
		}
		else
			return false;
	}
	
	std::vector<std::string> BrowseForFileNameMultiSelect(bool _bLoad, HWND _hParent, const wchar_t *_pTitle,
		const wchar_t *_pInitialFolder,const wchar_t *_pFilter,const wchar_t *_pExtension)
	{
		wchar_t szFile [MAX_PATH+1+2048*2] = {0};
		wchar_t szFileTitle [MAX_PATH+1] = {0};

		OPENFILENAME ofn;

		ZeroMemory (&ofn,sizeof (ofn));

		ofn.lStructSize		= sizeof (OPENFILENAME);
		ofn.lpstrInitialDir = _pInitialFolder;
		ofn.lpstrFilter		= _pFilter;
		ofn.nMaxFile		= sizeof (szFile);
		ofn.lpstrFile		= szFile;
		ofn.lpstrFileTitle	= szFileTitle;
		ofn.nMaxFileTitle	= sizeof (szFileTitle);
		ofn.lpstrDefExt     = _pExtension;
		ofn.hwndOwner		= _hParent;
		ofn.Flags			= OFN_NOCHANGEDIR | OFN_EXPLORER | OFN_HIDEREADONLY | OFN_ALLOWMULTISELECT ;

		std::vector<std::string> files;

		if (((_bLoad) ? GetOpenFileName(&ofn) : GetSaveFileName(&ofn)))
		{
			std::string directory = ConvertWStringToUTF8(ofn.lpstrFile);
			wchar_t *temp = ofn.lpstrFile;
			wchar_t *oldtemp = temp;
			temp += wcslen(temp)+1;
			if (*temp==0)
			{
				//we only got one file
				files.push_back(ConvertWStringToUTF8(oldtemp));
			}
			else
			{
				while (*temp)
				{
					files.push_back(directory+"\\"+ConvertWStringToUTF8(temp));
					temp += wcslen(temp)+1;
				}
			}
			return files;
		}
		else
			return std::vector<std::string>(); // empty vector;
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
			result_ = filename_ != "";
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