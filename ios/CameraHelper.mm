#include <atomic>
#include <vector>
#include <string>
#include "Core/Config.h"
#import "CameraHelper.h"
#import <UIKit/UIKit.h>

@interface CameraHelper() {
    AVCaptureSession *captureSession;
    id rotationCoordinator;  // AVCaptureDeviceRotationCoordinator, iOS 17+.
    std::atomic<int> interfaceOrientation;
    int mWidth;
    int mHeight;
}
@end

@implementation CameraHelper

static NSArray<AVCaptureDevice *> *GetVideoDevices() {
    NSArray<AVCaptureDeviceType> *types = @[
        AVCaptureDeviceTypeBuiltInWideAngleCamera,
        AVCaptureDeviceTypeBuiltInUltraWideCamera,
        AVCaptureDeviceTypeBuiltInTelephotoCamera,
        AVCaptureDeviceTypeBuiltInTrueDepthCamera,
    ];
    return [AVCaptureDeviceDiscoverySession discoverySessionWithDeviceTypes:types
                                                                  mediaType:AVMediaTypeVideo
                                                                   position:AVCaptureDevicePositionUnspecified].devices;
}

std::vector<std::string> System_GetCameraDeviceList() {
    std::vector<std::string> deviceList;
    for (AVCaptureDevice *device in GetVideoDevices()) {
        deviceList.push_back([device.localizedName UTF8String]);
    }
    return deviceList;
}

NSString *getSelectedCamera() {
    NSString *selectedCamera = [NSString stringWithCString:g_Config.sCameraDevice.c_str() encoding:[NSString defaultCStringEncoding]];
    return selectedCamera;
}

-(int) checkPermission {
    AVAuthorizationStatus status = [AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeVideo];
    NSLog(@"CameraHelper::checkPermission %ld", (long)status);

    switch (status) {
        case AVAuthorizationStatusNotDetermined: {
            [AVCaptureDevice requestAccessForMediaType:AVMediaTypeVideo completionHandler:^(BOOL granted) {
                if (granted) {
                    NSLog(@"camera permission granted");
                    dispatch_async(dispatch_get_main_queue(), ^{
                        [self startVideo:mWidth h:mHeight];
                    });
                } else {
                    NSLog(@"camera permission denied");
                }
            }];
            return 1;
        }
        case AVAuthorizationStatusRestricted:
        case AVAuthorizationStatusDenied: {
            NSLog(@"camera permission denied");
            return 1;
        }
        case AVAuthorizationStatusAuthorized: {
            return 0;
        }
        default: {
            NSLog(@"unknown camera authorization status %ld, treating as denied", (long)status);
            return 1;
        }
    }
}

-(void) startVideo: (int)width h:(int)height {
    NSLog(@"CameraHelper::startVideo %dx%d", width, height);
	mWidth = width;
	mHeight = height;
    if ([self checkPermission]) {
        return;
    }

    dispatch_async(dispatch_get_main_queue(), ^{
        NSError *error = nil;

        captureSession = [[AVCaptureSession alloc] init];
        captureSession.sessionPreset = AVCaptureSessionPresetMedium;

        AVCaptureDeviceInput *videoInput = nil;
        NSString *selectedCamera = getSelectedCamera();
        for (AVCaptureDevice *device in GetVideoDevices()) {
            if ([device.localizedName isEqualToString:selectedCamera]) {
                videoInput = [AVCaptureDeviceInput deviceInputWithDevice:device error:&error];
            }
        }
        if (videoInput == nil || error) {
            NSLog(@"selectedCamera error; try default device");

            AVCaptureDevice *videoDevice = [AVCaptureDevice defaultDeviceWithMediaType:AVMediaTypeVideo];
            if (videoDevice == nil) {
                NSLog(@"videoDevice error");
                return;
            }
            videoInput = [AVCaptureDeviceInput deviceInputWithDevice:videoDevice error:&error];
            if (videoInput == nil) {
                NSLog(@"videoInput error");
                return;
            }
        }
        [captureSession addInput:videoInput];

        rotationCoordinator = nil;
        if (@available(iOS 17.0, *)) {
            rotationCoordinator = [[AVCaptureDeviceRotationCoordinator alloc] initWithDevice:videoInput.device previewLayer:nil];
        }

        AVCaptureVideoDataOutput *videoOutput = [[AVCaptureVideoDataOutput alloc] init];
        videoOutput.videoSettings = [NSDictionary dictionaryWithObject: [NSNumber numberWithInt:kCVPixelFormatType_32BGRA] forKey: (id)kCVPixelBufferPixelFormatTypeKey];

        [captureSession addOutput:videoOutput];

        dispatch_queue_t queue = dispatch_queue_create("cameraQueue", NULL);
        [videoOutput setSampleBufferDelegate:self queue:queue];

        [captureSession startRunning];
    });
}

- (void) setInterfaceOrientation:(UIInterfaceOrientation)orientation {
    interfaceOrientation = (int)orientation;
}

-(void) stopVideo {
    dispatch_async(dispatch_get_main_queue(), ^{
        [captureSession stopRunning];
    });
}

- (void) captureOutput:(AVCaptureOutput *)captureOutput
         didOutputSampleBuffer:(CMSampleBufferRef)sampleBuffer
         fromConnection:(AVCaptureConnection *)connection {
    // The sensor delivers frames in its own landscape orientation, however the phone is held.
    // Rotating the connection makes the next frames upright.
    if (@available(iOS 17.0, *)) {
        AVCaptureDeviceRotationCoordinator *coordinator = rotationCoordinator;
        CGFloat angle = coordinator ? coordinator.videoRotationAngleForHorizonLevelCapture : 0.0;
        if (connection.videoRotationAngle != angle && [connection isVideoRotationAngleSupported:angle]) {
            connection.videoRotationAngle = angle;
        }
    } else {
        // The UIInterfaceOrientation values match AVCaptureVideoOrientation's.
        AVCaptureVideoOrientation orientation = (AVCaptureVideoOrientation)interfaceOrientation.load();
        if (orientation != 0 && connection.isVideoOrientationSupported && connection.videoOrientation != orientation) {
            connection.videoOrientation = orientation;
        }
    }

    CGImageRef cgImage = [self imageFromSampleBuffer:sampleBuffer];
    UIImage *theImage = [UIImage imageWithCGImage: cgImage];
    CGImageRelease(cgImage);
    NSData *imageData = UIImageJPEGRepresentation(theImage, 0.6);

    [self.delegate PushCameraImageIOS:imageData.length buffer:(unsigned char*)imageData.bytes];
}

- (CGImageRef) imageFromSampleBuffer:(CMSampleBufferRef) sampleBuffer {
    CVImageBufferRef imageBuffer = CMSampleBufferGetImageBuffer(sampleBuffer);
    CVPixelBufferLockBaseAddress(imageBuffer,0);
    void* baseAddress = CVPixelBufferGetBaseAddressOfPlane(imageBuffer, 0);
    size_t bytesPerRow = CVPixelBufferGetBytesPerRow(imageBuffer);
    size_t width = CVPixelBufferGetWidth(imageBuffer);
    size_t height = CVPixelBufferGetHeight(imageBuffer);
    CGColorSpaceRef colorSpace = CGColorSpaceCreateDeviceRGB();

    CGContextRef inContext = CGBitmapContextCreate(baseAddress, width, height, 8, bytesPerRow, colorSpace, kCGBitmapByteOrder32Little | kCGImageAlphaPremultipliedFirst);
    CGImageRef inImage = CGBitmapContextCreateImage(inContext);
    CGContextRelease(inContext);

    // Scale the frame to cover the size the game asked for, cropping what doesn't fit. When the phone
    // is held upright the frame is portrait, and stretching it to the PSP's landscape would distort it.
    const CGFloat scale = MAX((CGFloat)mWidth / width, (CGFloat)mHeight / height);
    const CGFloat drawWidth = width * scale;
    const CGFloat drawHeight = height * scale;
    CGRect outRect = CGRectMake((mWidth - drawWidth) * 0.5, (mHeight - drawHeight) * 0.5, drawWidth, drawHeight);
    CGContextRef outContext = CGBitmapContextCreate(nil, mWidth, mHeight, 8, mWidth * 4, colorSpace, kCGImageAlphaPremultipliedFirst);
    CGContextDrawImage(outContext, outRect, inImage);
    CGImageRelease(inImage);
    CGImageRef outImage = CGBitmapContextCreateImage(outContext);
    CGContextRelease(outContext);

    CGColorSpaceRelease(colorSpace);
    CVPixelBufferUnlockBaseAddress(imageBuffer, 0);
    return outImage;
}

@end
