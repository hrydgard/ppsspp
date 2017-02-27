#pragma once

#include <string>
#include <vector>
#include <thread>

namespace W32Util
{
	std::string BrowseForFolder(HWND parent, const char *title);
	std::string BrowseForFolder(HWND parent, const wchar_t *title);
	bool BrowseForFileName (bool _bLoad, HWND _hParent, const wchar_t*_pTitle,
		const wchar_t *_pInitialFolder,const wchar_t *_pFilter,const wchar_t*_pExtension, 
		std::string& _strFileName);
	std::vector<std::string> BrowseForFileNameMultiSelect(bool _bLoad, HWND _hParent, const wchar_t*_pTitle,
		const wchar_t*_pInitialFolder,const wchar_t*_pFilter,const wchar_t*_pExtension);

	struct AsyncBrowseDialog {
	public:
		enum Type {
			OPEN,
			SAVE,
			DIR,
		};

		// For a directory.
		AsyncBrowseDialog(HWND parent, UINT completeMsg, std::wstring title);
		// For a file (OPEN or SAVE.)
		AsyncBrowseDialog(Type type, HWND parent, UINT completeMsg, std::wstring title, std::wstring initialFolder, std::wstring filter, std::wstring extension);

		~AsyncBrowseDialog();

		bool GetResult(std::string &filename);
		Type GetType() {
			return type_;
		}

	private:
		void Execute();

		std::thread *thread_;
		Type type_;
		HWND parent_;
		UINT completeMsg_;
		std::wstring title_;
		std::wstring initialFolder_;
		std::wstring filter_;
		std::wstring extension_;
		bool complete_;
		bool result_;
		std::string filename_;
	};
}