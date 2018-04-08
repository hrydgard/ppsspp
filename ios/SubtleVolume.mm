//
//  SubtleVolume.m
//  subtleVolumeObjC
//
//  Created by iMokhles on 24/03/16.
//  Copyright © 2016 iMokhles. All rights reserved.
//

#import "SubtleVolume.h"

MPVolumeView *volume = [[MPVolumeView alloc] initWithFrame:CGRectZero];
UIView *overlay = [[UIView alloc] init];
CGFloat volumeLevel = 0;

@interface SubtleVolume (){
	BOOL runningShowAnimation;
	BOOL showing;
	BOOL runningHideAnimation;
	BOOL lastAnimated;
};

@property (nonatomic, strong) NSTimer *timer;

- (void)timerComplete;
- (void)doHide:(BOOL)animated;
- (void)doShow:(BOOL)animated;
- (void)stopAnimations;

@end


@implementation SubtleVolume

- (instancetype)initWithStyle:(SubtleVolumeStyle)style frame:(CGRect)frame {
	
	self = [super initWithFrame:frame];
	
	if (self) {
		self.animatedByDefault = YES;
		self.style = style;
		[self setup];
	}
	return self;
}

- (instancetype)initWithStyle:(SubtleVolumeStyle)style {
	return [self initWithStyle:style frame:CGRectZero];
}

- (instancetype)initWithCoder:(NSCoder *)aDecoder {
	self = [super initWithCoder:aDecoder];
	if (self) {
		[self setup];
	}
	return self;
}

- (instancetype)initWithFrame:(CGRect)frame {
	
	self = [super initWithFrame:frame];
	
	if (self) {
		[self setup];
	}
	return self;
}

- (instancetype)init {
	self = nil;
	NSAssert(false, @"To init this class please use the designated initializer: initWithStyle or initWithStyle:frame:");
	return nil;
}

- (void)setup {
	@try {
		[[AVAudioSession sharedInstance] setActive:YES error:nil];
	} @catch (NSException *e) {
		NSLog(@"Unable to initialize AVAudioSession");
	}
	
	volumeLevel = [[AVAudioSession sharedInstance] outputVolume];
	[[AVAudioSession sharedInstance] addObserver:self forKeyPath:@"outputVolume" options:NSKeyValueObservingOptionNew  context:NULL];
	[volume setVolumeThumbImage:[[UIImage alloc] init] forState:UIControlStateNormal];
	[volume setUserInteractionEnabled:NO];
	[volume setAlpha:0.0001];
	[volume setShowsRouteButton:NO];
	self.alpha = 0.0001;
	
	[self addSubview:volume];
	
	[self addSubview:overlay];
}

- (void)layoutSubviews {
	[super layoutSubviews];
	overlay.frame = CGRectMake(
	   self.padding,
	   self.padding,
	   (self.frame.size.width - (self.padding*2)) * volumeLevel,
	   self.frame.size.height - (self.padding*2)
	);
	
	self.backgroundColor = self.barBackgroundColor;
	overlay.backgroundColor = self.barTintColor;
	
}
- (void)updateVolume:(CGFloat)value animated:(BOOL)animated {
	[self.delegate subtleVolume:self willChange:value];
	volumeLevel = value;
	lastAnimated = animated;
	[UIView animateWithDuration:(animated ? 0.1 : 0) animations:^{
		CGRect rectOverlayView = overlay.frame;
		CGFloat overlyWidth = self.frame.size.width * volumeLevel;
		rectOverlayView.size.width = overlyWidth;
		overlay.frame = rectOverlayView;
	}];
	
	
	if(self.timer) {
		[self.timer invalidate];
		self.timer = nil;
	}
	
	self.timer = [NSTimer scheduledTimerWithTimeInterval:2 target:self selector:@selector(timerComplete) userInfo:nil repeats:NO];
	[self doShow:animated];
	[self.delegate subtleVolume:self didChange:value];
}

- (void)timerComplete {
	[self doHide:lastAnimated];
	self.timer = nil;
}

- (void)doHide:(BOOL)animated {
	if(!showing) {
		return;
	}
	
	if(runningHideAnimation && !animated) {
		[self stopAnimations];
	}
	
	if(runningHideAnimation) {
		return;
	}
	
	if(animated) {
		runningHideAnimation = YES;
		[UIView animateWithDuration:0.333 animations:^{
			switch (self.animation) {
				case SubtleVolumeAnimationNone:
					break;
				case SubtleVolumeAnimationFadeIn:
					self.alpha = 0.0001;
					break;
				case SubtleVolumeAnimationSlideDown:
					self.alpha = 0.0001;
					self.transform = CGAffineTransformMakeTranslation(0, -self.frame.size.height);
					break;
				default:
					break;
			}
		} completion:^(BOOL finished) {
			showing = NO;
			runningHideAnimation = NO;
		}];
	} else {
		showing = NO;
		self.alpha = 0.0001;
		if(self.animation == SubtleVolumeAnimationSlideDown) {
			self.transform = CGAffineTransformMakeTranslation(0, -self.frame.size.height);
		}
	}
}

- (void)doShow:(BOOL)animated {
	if(showing) {
		return;
	}
	
	if(runningShowAnimation && !animated) {
		[self stopAnimations];
	}
	
	if(runningShowAnimation) {
		return;
	}
	
	if(animated) {
		// set up for first run, assuming the animation has changed
		// between instantiation and first showing
		if(self.animation == SubtleVolumeAnimationSlideDown) {
			self.transform = CGAffineTransformMakeTranslation(0, -self.frame.size.height);
		}
		
		runningShowAnimation = YES;
		[UIView animateWithDuration:0.333 animations:^{
			switch (self.animation) {
				case SubtleVolumeAnimationNone:
					break;
				case SubtleVolumeAnimationFadeIn:
					self.alpha = 1;
					break;
				case SubtleVolumeAnimationSlideDown:
					self.alpha = 1;
					self.transform = CGAffineTransformIdentity;
					break;
				default:
					break;
			}
		} completion:^(BOOL finished) {
			showing = YES;
			runningShowAnimation = NO;
		}];
	} else {
		showing = YES;
		self.alpha = 1;
		self.transform = CGAffineTransformIdentity;
	}
}

- (void)stopAnimations {
	[self.layer removeAllAnimations];
	runningHideAnimation = NO;
	runningShowAnimation = NO;
}

- (void)observeValueForKeyPath:(NSString *)keyPath ofObject:(id)object change:(NSDictionary<NSString *,id> *)change context:(void *)context {
	if ([keyPath isEqual:@"outputVolume"]) {
		CGFloat value = [change[@"new"] floatValue];
		[self updateVolume:value animated:self.animatedByDefault];
	} else {
		return;
	}
	
}

@end
