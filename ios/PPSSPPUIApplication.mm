//
//  PPSSPPUIApplication.m
//  PPSSPP
//
//  Created by xieyi on 2017/9/4.
//
//

#import "PPSSPPUIApplication.h"

#import <Foundation/Foundation.h>
#import <Foundation/NSObjCRuntime.h>
#import <GLKit/GLKit.h>

#include "Common/System/Display.h"
#include "Common/System/System.h"
#include "Common/System/NativeApp.h"
#include "Common/TimeUtil.h"
#include "Common/File/VFS/VFS.h"
#include "Common/File/VFS/DirectoryReader.h"
#include "Common/Input/InputState.h"
#include "Common/Net/Resolve.h"
#include "Common/UI/Screen.h"
#include "Common/GPU/thin3d.h"
#include "Common/Input/KeyCodes.h"
#include "Common/GPU/OpenGL/GLFeatures.h"

#import "ios/AppDelegate.h"
#include "ios/SmartKeyboardMap.hpp"

#include "Core/Config.h"
#include "Common/GraphicsContext.h"

#include <sys/types.h>
#include <sys/sysctl.h>
#include <mach/machine.h>

// This stuff reads directly from _gsEvent which is not allowed on the App Store.

#ifndef IS_IOS7
#define IS_IOS7 ([[UIDevice currentDevice].systemVersion floatValue]>=7.0)
#endif
#ifndef IS_IOS9
#define IS_IOS9 ([[UIDevice currentDevice].systemVersion floatValue]>=9.0)
#endif
#define IS_64BIT (sizeof(NSUInteger)==8)

#define GSEVENT_TYPE            2
#define GSEVENT_FLAGS           (IS_64BIT?10:12)

#define GSEVENTKEY_KEYCODE      (IS_64BIT?(IS_IOS9?13:19):(IS_IOS7?17:15))

#define GSEVENT_TYPE_KEYUP      11
#define GSEVENT_TYPE_KEYDOWN    10
#define GSEVENT_TYPE_MODIFIER   12

#define GSEVENT_FLAG_LCMD       65536           // 0x00010000
#define GSEVENT_FLAG_LSHIFT     131072          // 0x00020000
#define GSEVENT_FLAG_LCTRL      1048576         // 0x00100000
#define GSEVENT_FLAG_LALT       524288          // 0x00080000

#define GSEVENT_FLAG_RSHIFT     2097152         // 0x00200000 - not sent IOS9
#define GSEVENT_FLAG_RCTRL      8388608         // 0x00800000 - not sent IOS9
#define GSEVENT_FLAG_RALT       4194304         // 0x00400000 - not sent IOS9

@implementation PPSSPPUIApplication

#if !PPSSPP_PLATFORM(IOS_APP_STORE)

- (void)decodeKeyEvent:(NSInteger *)eventMem {
    NSInteger eventType = eventMem[GSEVENT_TYPE];
    NSInteger eventScanCode = eventMem[GSEVENTKEY_KEYCODE];

    //NSLog(@"Got key: %d", (int)eventScanCode);

    if (eventType == GSEVENT_TYPE_KEYUP) {
        struct KeyInput key;
        key.flags = KEY_UP;
        key.keyCode = getSmartKeyboardMap((int)eventScanCode);
        key.deviceId = DEVICE_ID_KEYBOARD;
        NativeKey(key);
    } else if (GSEVENT_TYPE_KEYDOWN) {
        struct KeyInput key;
        key.flags = KEY_DOWN;
        key.keyCode = getSmartKeyboardMap((int)eventScanCode);
        key.deviceId = DEVICE_ID_KEYBOARD;
        NativeKey(key);
    }

}

- (void)handleKeyUIEvent:(UIEvent *) event {
    if ([event respondsToSelector:@selector(_gsEvent)]) {
        NSInteger *eventMem;

        eventMem = (NSInteger *) (__bridge void*)[event performSelector:@selector(_gsEvent)];
        if (eventMem) {
            [self decodeKeyEvent:eventMem];
        }
    }
}

- (void)sendEvent:(UIEvent *)event {
    [super sendEvent:event];
    if ([event respondsToSelector:@selector(_gsEvent)]) {
        NSInteger *eventMem;

        eventMem = (NSInteger *) (__bridge void*)[event performSelector:@selector(_gsEvent)];
        if (eventMem) {
            [self decodeKeyEvent:eventMem];
        }
    }
}

#endif  // IOS_APP_STORE

@end
