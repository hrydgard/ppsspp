#pragma once

#import <UIKit/UIKit.h>

typedef void (^CameraControlPressCallback)(BOOL isFullPress, BOOL isDown);

/// Routes iPhone 16/16 Pro Camera Control button events into PPSSPP's input system.
///
/// Uses AVCaptureEventInteraction (iOS 17.2+) with a running AVCaptureSession.
/// Camera access is required — the system only delivers Camera Control events to
/// apps actively using the camera. The green indicator appears in the status bar.
///
/// Requires iOS 18+ for Camera Control button hardware.
/// Falls back to no-op on older versions.
@interface CameraControlInputManager : NSObject

- (instancetype)initWithParentView:(UIView *)view callback:(CameraControlPressCallback)callback;

/// Start/resume the camera session. Safe to call multiple times.
- (void)start;

/// Stop/pause the camera session. Safe to call multiple times.
- (void)stop;

/// Whether the camera session is active (running).
@property (nonatomic, readonly, getter=isSessionActive) BOOL sessionActive;

@end
