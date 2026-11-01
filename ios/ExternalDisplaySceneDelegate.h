// Copyright (c) 2012- PPSSPP Project.

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

#import <UIKit/UIKit.h>

// The scene iOS gives us for an external display, like an AirPlay TV. With Metal, the game goes there
// (a Game output, see docs/multi-swapchain.md), and the phone keeps the touch controls and the menus.
// Otherwise we leave the scene without a window, and iOS mirrors the phone.
@interface ExternalDisplaySceneDelegate : UIResponder <UIWindowSceneDelegate>

@property (strong, nonatomic) UIWindow *window;

@end

// Called by the app's SceneDelegate when PPSSPP has (re)started, and the config is loaded.
void ExternalDisplayNotifyLaunched();
// g_Config.iExternalDisplay changed. On the main thread.
void ExternalDisplaySettingChanged();
