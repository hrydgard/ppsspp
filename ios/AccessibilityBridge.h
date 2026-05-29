#pragma once

#import <UIKit/UIKit.h>

@class PPSSPPAccessibilityBridge;

@interface PPSSPPAccessibilityBridge : NSObject

- (instancetype)initWithView:(UIView *)view;
- (void)refresh;
- (void)reset;
- (void)uiStateChanged;
- (void)willResignActive;
- (BOOL)accessibilityPerformEscape;
- (BOOL)accessibilityPerformMagicTap;

@end
