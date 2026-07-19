#pragma once

#import <UIKit/UIKit.h>

/// Callback invoked on Camera Control button events.
/// @param isFullPress YES for primary action (full click), NO for secondary action (half-press / light press)
/// @param isDown YES for press (began), NO for release (ended)
typedef void (^CameraControlPressCallback)(BOOL isFullPress, BOOL isDown);

/// Manages the AVCaptureEventInteraction for the physical Camera Control button (iPhone 16/16 Pro).
///
/// Uses an invisible hidden UIView attached to the parent view, with an AVCaptureSession
/// running in the background. The system requires an active camera session to route
/// Camera Control events — this is a platform limitation set by Apple.
///
/// Camera access is required. On first launch, iOS will ask for camera permission.
/// The camera indicator (green dot) appears in the status bar while the session runs.
/// This is unavoidable — Apple designed the Camera Control button exclusively for
/// camera-related apps.
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
