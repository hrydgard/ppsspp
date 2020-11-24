//
//  DisplayManager.h
//  native
//
//  Created by xieyi on 2019/6/9.
//

#import <Foundation/Foundation.h>
#import <UIKit/UIKit.h>

NS_ASSUME_NONNULL_BEGIN

@interface DisplayManager : NSObject

- (void)setupDisplayListener;
- (void)updateResolution:(UIScreen *)screen;

@property (nonatomic, strong) UIScreen *mainScreen;

@property (class, readonly, strong) DisplayManager *shared;

@end

NS_ASSUME_NONNULL_END
