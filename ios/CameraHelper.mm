#include <vector>
#include <string>
#include "Core/Config.h"
#import "CameraHelper.h"
#import <UIKit/UIKit.h>

@interface CameraHelper() {
    AVCaptureSession *captureSession;
    AVCaptureVideoPreviewLayer *previewLayer;
    int mWidth;
    int mHeight;
}
@end

@implementation CameraHelper

std::vector<std::string> System_GetCameraDeviceList() {
    std::vector<std::string> deviceList;
    for (AVCaptureDevice *device in [AVCaptureDevice devicesWithMediaType:AVMediaTypeVideo]) {
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
        for (AVCaptureDevice *device in [AVCaptureDevice devicesWithMediaType:AVMediaTypeVideo]) {
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

        AVCaptureVideoDataOutput *videoOutput = [[AVCaptureVideoDataOutput alloc] init];
        videoOutput.videoSettings = [NSDictionary dictionaryWithObject: [NSNumber numberWithInt:kCVPixelFormatType_32BGRA] forKey: (id)kCVPixelBufferPixelFormatTypeKey];

        [captureSession addOutput:videoOutput];

        dispatch_queue_t queue = dispatch_queue_create("cameraQueue", NULL);
        [videoOutput setSampleBufferDelegate:self queue:queue];

        previewLayer = [[AVCaptureVideoPreviewLayer alloc] initWithSession:captureSession];
        previewLayer.videoGravity = AVLayerVideoGravityResizeAspectFill;

        [previewLayer setFrame:CGRectMake(0, 0, mWidth, mHeight)];

        [captureSession startRunning];
    });
}

-(void) stopVideo {
    dispatch_async(dispatch_get_main_queue(), ^{
        [captureSession stopRunning];
    });
}

- (void) captureOutput:(AVCaptureOutput *)captureOutput
         didOutputSampleBuffer:(CMSampleBufferRef)sampleBuffer
         fromConnection:(AVCaptureConnection *)connection {
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

    CGRect outRect = CGRectMake(0, 0, width, height);
    CGContextRef outContext = CGBitmapContextCreate(nil, mWidth, mHeight, 8, mWidth * 4, colorSpace, kCGImageAlphaPremultipliedFirst);
    CGContextDrawImage(outContext, outRect, inImage);
    CGImageRelease(inImage);
    CGImageRef outImage = CGBitmapContextCreateImage(outContext);

    CGColorSpaceRelease(colorSpace);
    CVPixelBufferUnlockBaseAddress(imageBuffer, 0);
    return outImage;
}

@end
