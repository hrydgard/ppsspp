// Copyright (c) 2023- PPSSPP Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official git repository and contact information can be found at
// https://github.com/hrydgard/ppsspp and http://www.ppsspp.org/.

#include "pch.h"

#include "StorageAsync.h"
#include "StorageAccess.h"

using namespace winrt;
using namespace winrt::Windows::Storage;
using namespace winrt::Windows::Storage::Pickers;
using namespace winrt::Windows::Foundation;

// Call folder picker (the selected folder will be added to future list)
std::string PickSingleFolder()
{
	FolderPicker folderPicker;
	folderPicker.SuggestedStartLocation(PickerLocationId::Desktop);
	folderPicker.FileTypeFilter().Append(L"*");

	StorageFolder folder{ nullptr };
	ExecuteTask(folder, folderPicker.PickSingleFolderAsync());

	std::string path;
	if (folder) {
		AddItemToFutureList(folder);
		path = winrt::to_string(folder.Path());
	}
	return path;
}

// Call file picker (the selected file will be added to future list)
std::string PickSingleFile(std::vector<std::string> exts)
{
	FileOpenPicker filePicker;
	filePicker.SuggestedStartLocation(PickerLocationId::Desktop);
	filePicker.ViewMode(PickerViewMode::List);

	if (!exts.empty()) {
		for (const auto& ext : exts) {
			filePicker.FileTypeFilter().Append(winrt::to_hstring(ext));
		}
	}
	else {
		filePicker.FileTypeFilter().Append(L"*");
	}

	StorageFile file{ nullptr };
	ExecuteTask(file, filePicker.PickSingleFileAsync());

	std::string path;
	if (file) {
		AddItemToFutureList(file);
		path = winrt::to_string(file.Path());
	}
	return path;
}

std::string ChooseFile(std::vector<std::string> exts) {
	return PickSingleFile(exts);
}

std::string ChooseFolder() {
	return PickSingleFolder();
}
