#pragma once

#import <UIKit/UIKit.h>

/// Callback invoked on Camera Control button events.
/// @param isFullPress YES for primary action (full click), NO for secondary action (half-press / light press)
/// @param isDown YES for press (began), NO for release (ended)
typedef void (^CameraControlPressCallback)(BOOL isFullPress, BOOL isDown);

/// Manages the AVCaptureEventInteraction for the physical Camera Control button (iPhone 16/16 Pro).
///
/// Uses an invisible hidden UIView attached to the parent view, with an AVCaptureSession
/// running in the background to keep the camera hardware "active" so AVCaptureEventInteraction
/// delivers events. No camera preview or frame data is captured.
///
/// Requires iOS 18+ for Camera Control button support. Falls back to no-op on older versions.
@interface CameraControlInputManager : NSObject

/// Initialize with the parent view to attach the interaction to and a callback for button events.
- (instancetype)initWithParentView:(UIView *)view callback:(CameraControlPressCallback)callback;

/// Start the camera session and enable the interaction. Safe to call multiple times.
- (void)start;

/// Stop the camera session and disable the interaction. Safe to call multiple times.
- (void)stop;

@end
