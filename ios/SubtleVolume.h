//
//  SubtleVolume.h
//  subtleVolumeObjC
//
//  Created by iMokhles on 24/03/16.
//  Copyright © 2016 iMokhles. All rights reserved.
//

#import <UIKit/UIKit.h>
#import <MediaPlayer/MediaPlayer.h>
#import <AVFoundation/AVFoundation.h>

/**
 The style of the volume indicator
 - Plain: A plain bar
 - RoundedLine: A plain bar with rounded corners
 - Dashes: A bar divided in dashes
 - Dots: A bar composed by a line of dots
 */
typedef NS_ENUM(NSInteger, SubtleVolumeStyle) {
	SubtleVolumeStylePlain,
	SubtleVolumeStyleRoundedLine,
	SubtleVolumeStyleDashes,
	SubtleVolumeStyleDots
};


/**
 The entry and exit animation of the volume indicator
 - None: The indicator is always visible
 - SlideDown: The indicator fades in/out and slides from/to the top into position
 - FadeIn: The indicator fades in and out
 */
typedef NS_ENUM(NSInteger, SubtleVolumeAnimation) {
	SubtleVolumeAnimationNone,
	SubtleVolumeAnimationSlideDown,
	SubtleVolumeAnimationFadeIn
};

@class SubtleVolume;
/**
 Delegate protocol fo `SubtleVolume`.
 Notifies the delegate when a change is about to happen (before the entry animation)
 and when a change occurred (and the exit animation is complete)
 */
@protocol SubtleVolumeDelegate <NSObject>
/**
 The volume is about to change. This is fired before performing any entry animation
 - parameter subtleVolume: The current instance of `SubtleVolume`
 - parameter value: The value of the volume (between 0 an 1.0)
 */
- (void)subtleVolume:(SubtleVolume *)volumeView willChange:(CGFloat)value;
/**
 The volume did change. This is fired after the exit animation is done
 - parameter subtleVolume: The current instance of `SubtleVolume`
 - parameter value: The value of the volume (between 0 an 1.0)
 */
- (void)subtleVolume:(SubtleVolume *)volumeView didChange:(CGFloat)value;

@end

/**
 Replace the system volume popup with a more subtle way to display the volume
 when the user changes it with the volume rocker.
 */
@interface SubtleVolume : UIView
/**
 The style of the volume indicator
 */
@property (nonatomic, assign) SubtleVolumeStyle style;
/**
 The entry and exit animation of the indicator. The animation is triggered by the volume
 If the animation is set to `SubtleVolumeAnimationNone`, the volume indicator is always visible
 */
@property (nonatomic, assign) SubtleVolumeAnimation animation;
@property (nonatomic, strong) UIColor *barBackgroundColor;
@property (nonatomic, strong) UIColor *barTintColor;
@property (nonatomic, assign) id <SubtleVolumeDelegate> delegate;
@property (nonatomic, assign) BOOL animatedByDefault;
@property (nonatomic, assign) CGFloat padding;

- (instancetype)initWithStyle:(SubtleVolumeStyle)style;
- (instancetype)initWithStyle:(SubtleVolumeStyle)style frame:(CGRect)frame;
@end
