//
//  AppleBatteryClient.m
//  PPSSPP
//
//  Created by Serena on 24/01/2023.
//

#include "Battery.h"
#import <Foundation/Foundation.h>

#if PPSSPP_PLATFORM(MAC)
#include <IOKit/ps/IOPSKeys.h>
#include <IOKit/ps/IOPowerSources.h>
#elif PPSSPP_PLATFORM(IOS)
#import <UIKit/UIKit.h>
#endif

@interface AppleBatteryClient : NSObject
+(instancetype)sharedClient;
-(void)setNeedsToUpdateLevel;
@property int batteryLevel;
@end

void _powerSourceRunLoopCallback(void * __unused ctx) {
    // IOKit has told us that battery information has changed, now update the batteryLevel var
    [[AppleBatteryClient sharedClient] setNeedsToUpdateLevel];
}

// You may ask,
// "Why an entire class?
// Why not just call the UIDevice/IOKitPowerSource functions every time getCurrentBatteryCapacity() is called?"
// Well, calling the UIDevice/IOKitPowerSource functions very frequently (every second, it seems?) is expensive
// So, instead, I made a class with a cached batteryLevel property
// that only gets set when it needs to.
@implementation AppleBatteryClient

+ (instancetype)sharedClient {
    static AppleBatteryClient *client;
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        client = [AppleBatteryClient new];
        [client initialSetup];
        [client setNeedsToUpdateLevel];
    });
    
    return client;
}

-(void)initialSetup {
#if TARGET_OS_IOS
    // on iOS, this needs to be true to get the battery level
    // and it needs to be set just once, so do it here
    UIDevice.currentDevice.batteryMonitoringEnabled = YES;
    // Register for when the battery % changes
    [[NSNotificationCenter defaultCenter] addObserver:self
                                             selector:@selector(setNeedsToUpdateLevel)
                                                 name:UIDeviceBatteryLevelDidChangeNotification object:nil];
    
#elif TARGET_OS_MAC
    CFRunLoopSourceRef loop = IOPSNotificationCreateRunLoopSource(_powerSourceRunLoopCallback, nil);
    CFRunLoopAddSource(CFRunLoopGetMain(), loop, kCFRunLoopDefaultMode);
#endif
}

- (void)setNeedsToUpdateLevel {
#if TARGET_OS_IOS
    // `-[UIDevice batteryLevel]` returns the % like '0.(actual %)' (ie, 0.28 when the battery is 28%)
    // so multiply it by 100 to get a visually appropriate version
    self.batteryLevel = [[UIDevice currentDevice] batteryLevel] * 100;
#elif TARGET_OS_MAC
    CFTypeRef snapshot = IOPSCopyPowerSourcesInfo();
    NSArray *sourceList = (__bridge NSArray *)IOPSCopyPowerSourcesList(snapshot);
    if (!sourceList) {
        if (snapshot) CFRelease(snapshot);
        return;
    }
    
    for (NSDictionary *source in sourceList) {
        // kIOPSCurrentCapacityKey = battery level
        NSNumber *currentCapacity = [source objectForKey:@(kIOPSCurrentCapacityKey)];
        if (currentCapacity) {
            // we found what we want
            self.batteryLevel = currentCapacity.intValue;
            break;
        }
    }
    CFRelease(snapshot);
#endif
}

@end


int getCurrentBatteryCapacity() {
    return [[AppleBatteryClient sharedClient] batteryLevel];
}
