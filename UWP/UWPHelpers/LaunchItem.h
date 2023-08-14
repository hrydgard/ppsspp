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

using namespace Platform;
using namespace Windows::Storage;
using namespace Windows::Foundation;
using namespace Windows::UI::Core;
using namespace Windows::ApplicationModel;
using namespace Windows::ApplicationModel::Activation;

// LaunchItem can detect launch items in two cases
// 1- StorageFile
// 2- URI [ppsspp:?cmd="fullpath"&launchOnExit=customURI]

// Detect if activate args has launch item
// it will auto start the item unless 'onlyActivate' set to 'true'
void DetectLaunchItem(IActivatedEventArgs^ activateArgs, bool onlyActivate = false);

// Get current launch item path (same as 'DetectLaunchItem' but it doesn't start)
// this function made to handle item on startup
// it will mark the item as 'Handled' by default
// consider to close it if you want to use it for other purposes
std::string GetLaunchItemPath(IActivatedEventArgs^ activateArgs);

// Close current launch item
// it will launch back 'launchOnExit' if passed with URI 'cmd'
// if you want to ignore 'launchOnExit' call set it to 'false'
void CloseLaunchItem(bool launchOnExit = true);
