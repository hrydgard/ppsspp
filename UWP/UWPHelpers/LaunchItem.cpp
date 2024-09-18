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
#include <io.h>
#include <fcntl.h>
#include <regex>

#include "LaunchItem.h"
#include "StorageAccess.h"

#include "Common/Log.h"
#include "Common/System/System.h"
#include "Common/File/Path.h"
#include "UWPUtil.h"

#include <ppl.h>
#include <ppltasks.h>

#pragma region LaunchItemClass
class LaunchItem {
public:
	LaunchItem() {
	}

	~LaunchItem() {
		delete storageFile;
	}

	void Activate(IStorageFile^ file) {
		storageFile = file;
		AddItemToFutureList(storageFile);
		launchPath = std::string();
		launchOnExit = std::string();
	}

	void Activate(ProtocolActivatedEventArgs^ args) {
		try {
			unsigned i;
			Windows::Foundation::WwwFormUrlDecoder^ query = args->Uri->QueryParsed;

			for (i = 0; i < query->Size; i++)
			{
				IWwwFormUrlDecoderEntry^ arg = query->GetAt(i);

				if (arg->Name == "cmd")
				{
					auto command = FromPlatformString(arg->Value);
					DEBUG_LOG(Log::FileSystem, "Launch command %s", command.c_str());

					std::regex rgx("\"(.+[^\\/]+)\"");
					std::smatch match;

					if (std::regex_search(command, match, rgx)) {
						try
						{
							launchPath = match[1];
						}
						catch (...) {
							launchPath = match[0];
						}
						DEBUG_LOG(Log::FileSystem, "Launch target %s", launchPath.c_str());
					}
				}
				else if (arg->Name == "launchOnExit") {
					launchOnExit = FromPlatformString(arg->Value);
					DEBUG_LOG(Log::FileSystem, "On exit URI %s", launchOnExit.c_str());
				}
			}
		}
		catch (...) {

		}
		storageFile = nullptr;
	}

	void Start() {
		if (IsValid()) {
			concurrency::create_task([&] {
				SetState(true);
				std::string path = GetFilePath();
				// Delay to be able to launch on startup too
				std::this_thread::sleep_for(std::chrono::milliseconds(100));
				System_PostUIMessage(UIMessage::REQUEST_GAME_BOOT, path);
			});
		}
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

	void Close(bool callLaunchOnExit) {
		storageFile = nullptr;
		launchPath = std::string();
		handled = false;

		if (!launchOnExit.empty()) {
			if (callLaunchOnExit) {
				DEBUG_LOG(Log::FileSystem, "Calling back %s", launchOnExit.c_str());
				auto uri = ref new Windows::Foundation::Uri(ToPlatformString(launchOnExit));
				Windows::System::Launcher::LaunchUriAsync(uri);
			}
			else {
				DEBUG_LOG(Log::FileSystem, "Ignoring callback %s, due to callLaunchOnExit is false", launchOnExit.c_str());
			}
		}
		launchOnExit = std::string();
	}

private:
	IStorageFile^ storageFile;
	std::string launchPath;
	std::string launchOnExit;
	bool handled = false;
};
#pragma endregion

LaunchItem launchItemHandler;
void DetectLaunchItem(IActivatedEventArgs^ activateArgs, bool onlyActivate) {
	if (activateArgs != nullptr) {
		if (!launchItemHandler.IsHandled()) {
			if (activateArgs->Kind == ActivationKind::File) {
				FileActivatedEventArgs^ fileArgs = dynamic_cast<FileActivatedEventArgs^>(activateArgs);
				launchItemHandler.Activate((StorageFile^)fileArgs->Files->GetAt(0));
			}
			else if (activateArgs->Kind == ActivationKind::Protocol)
			{
				ProtocolActivatedEventArgs^ protocolArgs = dynamic_cast<ProtocolActivatedEventArgs^>(activateArgs);
				launchItemHandler.Activate(protocolArgs);
			}
			if (!onlyActivate) {
				launchItemHandler.Start();
			}
		}
	}
}

std::string GetLaunchItemPath(IActivatedEventArgs^ activateArgs) {
	DetectLaunchItem(activateArgs, true); // Just activate
	if (launchItemHandler.IsValid()) {
		// Expected that 'GetLaunchItemPath' called to handle startup item
		// it should be marked as handled by default
		launchItemHandler.SetState(true);
	}
	return launchItemHandler.GetFilePath();
}

void CloseLaunchItem(bool launchOnExit) {
	if (launchItemHandler.IsValid() && launchItemHandler.IsHandled()) {
		launchItemHandler.Close(launchOnExit);
	}
}
