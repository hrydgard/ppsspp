#import <Foundation/Foundation.h>
#import <AVFoundation/AVFoundation.h>
#import <UIKit/UIKit.h>

@protocol CameraFrameDelegate <NSObject>
@required
- (void) PushCameraImageIOS:(long long)len buffer:(unsigned char*)data;
@end

@interface CameraHelper : NSObject<AVCaptureVideoDataOutputSampleBufferDelegate>

@property (nonatomic, strong) id<CameraFrameDelegate> delegate;

- (void) startVideo:(int)width h:(int)height;
- (void) stopVideo;
// Below iOS 17, frames are rotated to match this. From 17, they're kept level with the horizon instead.
- (void) setInterfaceOrientation:(UIInterfaceOrientation)orientation;

@end
