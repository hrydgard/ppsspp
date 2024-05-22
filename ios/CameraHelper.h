#import <Foundation/Foundation.h>
#import <AVFoundation/AVFoundation.h>

@protocol CameraFrameDelegate <NSObject>
@required
- (void) PushCameraImageIOS:(long long)len buffer:(unsigned char*)data;
@end

@interface CameraHelper : NSObject<AVCaptureVideoDataOutputSampleBufferDelegate>

@property (nonatomic, strong) id<CameraFrameDelegate> delegate;

- (void) startVideo:(int)width h:(int)height;
- (void) stopVideo;

@end
