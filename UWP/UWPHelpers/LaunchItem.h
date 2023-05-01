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

#pragma once 

#include "pch.h"
#include <io.h>
#include <fcntl.h>

#include "Common/Log.h"
#include "UWPUtil.h"
#include <regex>


using namespace Platform;
using namespace Windows::Storage;
using namespace Windows::Foundation;
using namespace Windows::ApplicationModel::Activation;
extern void AddItemToFutureList(IStorageItem^ item);

class LaunchItem {
public:
	LaunchItem() {
	}

	LaunchItem(IStorageFile^ file) {
		storageFile = file;
		AddItemToFutureList(storageFile);
		launchPath = std::string();
		launchOnExit = std::string();
	}

	LaunchItem(ProtocolActivatedEventArgs^ args) {
		try {
			unsigned i;
			Windows::Foundation::WwwFormUrlDecoder^ query = args->Uri->QueryParsed;

			for (i = 0; i < query->Size; i++)
			{
				IWwwFormUrlDecoderEntry^ arg = query->GetAt(i);

				if (arg->Name == "cmd")
				{
					auto command = FromPlatformString(arg->Value);
					DEBUG_LOG(FILESYS, "Launch command %s", command.c_str());

					std::regex rgx("\"(.+\[^\/]+)\"");
					std::smatch match;

					if (std::regex_search(command, match, rgx)) {
						try
						{
							launchPath = match[1];
						}
						catch (...) {
							launchPath = match[0];
						}
						DEBUG_LOG(FILESYS, "Launch target %s", launchPath.c_str());
					}
				}
				else if (arg->Name == "launchOnExit") {
					launchOnExit = FromPlatformString(arg->Value);
					DEBUG_LOG(FILESYS, "On exit URI %s", launchOnExit.c_str());
				}
			}
		}
		catch (...) {

		}
		storageFile = nullptr;
	}
	
	~LaunchItem() {
		delete storageFile;
	}

	
	bool IsHandled() {
		return handled;
	}
	void SetState(bool fileHandled) {
		handled = fileHandled;
	}

	bool IsValid() {
		return storageFile != nullptr || !launchPath.empty();
	}

	std::string GetFilePath() {
		std::string path = launchPath;
		if (storageFile != nullptr) {
			path = FromPlatformString(storageFile->Path);
		}
		return path;
	}

	void Close() {
		storageFile = nullptr;
		launchPath = std::string();
		handled = false;

		if (!launchOnExit.empty()) {
			DEBUG_LOG(FILESYS, "Calling back %s", launchOnExit.c_str());
			auto uri = ref new Windows::Foundation::Uri(ToPlatformString(launchOnExit));
			Windows::System::Launcher::LaunchUriAsync(uri);
		}
		launchOnExit = std::string();
	}

private:
	IStorageFile^ storageFile;
	std::string launchPath;
	std::string launchOnExit;
	bool handled = false;
};
