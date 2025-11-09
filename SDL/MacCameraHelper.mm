#import <AVFoundation/AVFoundation.h>
#import <AppKit/AppKit.h>

#include <pthread.h>
#include <cstdio>
#include <string>
#include <vector>

#include "ppsspp_config.h"

#if PPSSPP_PLATFORM(MAC)

#include "Common/Log.h"
#include "Core/Config.h"
#include "Core/HW/Camera.h"

namespace {

NSInteger ParseSelectedDeviceIndex() {
	int deviceIndex = 0;
	if (sscanf(g_Config.sCameraDevice.c_str(), "%d:", &deviceIndex) != 1) {
		deviceIndex = 0;
	}
	return deviceIndex;
}

void RunOnMainQueue(dispatch_block_t block) {
	if (pthread_main_np()) {
		block();
	} else {
		dispatch_sync(dispatch_get_main_queue(), block);
	}
}

}  // namespace

@interface MacCameraHelper : NSObject<AVCaptureVideoDataOutputSampleBufferDelegate>
@property(nonatomic, strong) AVCaptureSession *session;
@property(nonatomic, strong) AVCaptureVideoDataOutput *videoOutput;
@property(nonatomic) int targetWidth;
@property(nonatomic) int targetHeight;
@end

@implementation MacCameraHelper
{
	dispatch_queue_t captureQueue_;
}

+ (instancetype)sharedInstance {
	static MacCameraHelper *sharedInstance = nil;
	static dispatch_once_t onceToken;
	dispatch_once(&onceToken, ^{
		sharedInstance = [[MacCameraHelper alloc] init];
	});
	return sharedInstance;
}

- (BOOL)ensurePermission {
	if (@available(macOS 10.14, *)) {
		AVAuthorizationStatus status = [AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeVideo];
		if (status == AVAuthorizationStatusAuthorized) {
			return YES;
		}

		if (status == AVAuthorizationStatusNotDetermined) {
			dispatch_semaphore_t sema = dispatch_semaphore_create(0);
			[AVCaptureDevice requestAccessForMediaType:AVMediaTypeVideo completionHandler:^(BOOL granted) {
				(void)granted;
				dispatch_semaphore_signal(sema);
			}];
			dispatch_semaphore_wait(sema, DISPATCH_TIME_FOREVER);
			status = [AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeVideo];
		}

		if (status != AVAuthorizationStatusAuthorized) {
			ERROR_LOG(Log::HLE, "Camera permission denied on macOS");
			return NO;
		}
		return YES;
	}
	// macOS < 10.14 does not require runtime permission checks.
	return YES;
}

- (BOOL)startCaptureWithWidth:(int)width height:(int)height deviceIndex:(NSInteger)deviceIndex {
	if (self.session) {
		[self stopCapture];
	}

	if (![self ensurePermission]) {
		ERROR_LOG(Log::HLE, "MacCameraHelper: ensurePermission failed");
		return NO;
	}

	NSArray<AVCaptureDevice *> *devices = [AVCaptureDevice devicesWithMediaType:AVMediaTypeVideo];
	if (devices.count == 0) {
		ERROR_LOG(Log::HLE, "No macOS camera devices detected");
		return NO;
	}

	AVCaptureDevice *selectedDevice = nil;
	if (deviceIndex >= 0 && deviceIndex < (NSInteger)devices.count) {
		selectedDevice = devices[deviceIndex];
	}
	if (!selectedDevice) {
		selectedDevice = devices.firstObject;
	}

	NSError *error = nil;
	AVCaptureDeviceInput *input = [AVCaptureDeviceInput deviceInputWithDevice:selectedDevice error:&error];
	if (!input || error) {
		const char *msg = error ? error.localizedDescription.UTF8String : "unknown";
		ERROR_LOG(Log::HLE, "Unable to create camera input: %s", msg ? msg : "unknown");
		return NO;
	}

	AVCaptureSession *session = [[AVCaptureSession alloc] init];
	if ([session canAddInput:input]) {
		[session addInput:input];
	} else {
		ERROR_LOG(Log::HLE, "Cannot add camera input to session");
		return NO;
	}

	AVCaptureVideoDataOutput *output = [[AVCaptureVideoDataOutput alloc] init];
	output.videoSettings = @{
		(id)kCVPixelBufferPixelFormatTypeKey : @(kCVPixelFormatType_32BGRA),
	};
	output.alwaysDiscardsLateVideoFrames = YES;

	if (!captureQueue_) {
		captureQueue_ = dispatch_queue_create("org.ppsspp.macCameraQueue", DISPATCH_QUEUE_SERIAL);
	}
	[output setSampleBufferDelegate:self queue:captureQueue_];

	if ([session canAddOutput:output]) {
		[session addOutput:output];
	} else {
		ERROR_LOG(Log::HLE, "Cannot add camera output to session");
		return NO;
	}

	self.session = session;
	self.videoOutput = output;
	self.targetWidth = width;
	self.targetHeight = height;

	[self.session startRunning];
	INFO_LOG(Log::HLE, "Mac camera session started with device %s (%dx%d)", selectedDevice.localizedName.UTF8String, width, height);
	return YES;
}

- (void)stopCapture {
	if (!self.session) {
		return;
	}

	[self.session stopRunning];
	INFO_LOG(Log::HLE, "Mac camera session stopped");

	for (AVCaptureInput *input in self.session.inputs) {
		[self.session removeInput:input];
	}
	if (self.videoOutput) {
		[self.session removeOutput:self.videoOutput];
		self.videoOutput = nil;
	}
	self.session = nil;
}

- (void)captureOutput:(AVCaptureOutput *)captureOutput
 didOutputSampleBuffer:(CMSampleBufferRef)sampleBuffer
		fromConnection:(AVCaptureConnection *)connection {
	(void)captureOutput;
	(void)connection;

	@autoreleasepool {
		CVImageBufferRef imageBuffer = CMSampleBufferGetImageBuffer(sampleBuffer);
		if (!imageBuffer) {
			return;
		}

		CVPixelBufferLockBaseAddress(imageBuffer, kCVPixelBufferLock_ReadOnly);
		size_t bufferWidth = CVPixelBufferGetWidth(imageBuffer);
		size_t bufferHeight = CVPixelBufferGetHeight(imageBuffer);
		void *baseAddress = CVPixelBufferGetBaseAddress(imageBuffer);
		size_t bytesPerRow = CVPixelBufferGetBytesPerRow(imageBuffer);

		CGColorSpaceRef colorSpace = CGColorSpaceCreateDeviceRGB();
		CGContextRef inContext = CGBitmapContextCreate(baseAddress, bufferWidth, bufferHeight, 8, bytesPerRow, colorSpace,
			kCGBitmapByteOrder32Little | kCGImageAlphaPremultipliedFirst);
		CGImageRef inImage = CGBitmapContextCreateImage(inContext);
		CGContextRelease(inContext);

		if (!inImage) {
			CVPixelBufferUnlockBaseAddress(imageBuffer, kCVPixelBufferLock_ReadOnly);
			CGColorSpaceRelease(colorSpace);
			return;
		}

		CGContextRef outContext = CGBitmapContextCreate(nullptr, self.targetWidth, self.targetHeight, 8,
			self.targetWidth * 4, colorSpace, kCGBitmapByteOrder32Little | kCGImageAlphaPremultipliedFirst);
		CGRect drawRect = CGRectMake(0, 0, self.targetWidth, self.targetHeight);
		CGContextDrawImage(outContext, drawRect, inImage);

		CGImageRef scaledImage = CGBitmapContextCreateImage(outContext);

		CGContextRelease(outContext);
		CGImageRelease(inImage);
		CGColorSpaceRelease(colorSpace);
		CVPixelBufferUnlockBaseAddress(imageBuffer, kCVPixelBufferLock_ReadOnly);

		if (!scaledImage) {
			return;
		}

		NSBitmapImageRep *bitmapRep = [[NSBitmapImageRep alloc] initWithCGImage:scaledImage];
		CGImageRelease(scaledImage);

		if (!bitmapRep) {
			return;
		}

		NSData *jpegData = [bitmapRep representationUsingType:NSBitmapImageFileTypeJPEG
												   properties:@{ NSImageCompressionFactor : @(0.6f) }];
		if (jpegData && jpegData.length > 0) {
			Camera::pushCameraImage(jpegData.length, (unsigned char *)jpegData.bytes);
		}
	}
}

@end

std::vector<std::string> __mac_getDeviceList() {
	std::vector<std::string> devices;
	NSArray<AVCaptureDevice *> *availableDevices = [AVCaptureDevice devicesWithMediaType:AVMediaTypeVideo];
	for (NSInteger i = 0; i < (NSInteger)availableDevices.count; ++i) {
		AVCaptureDevice *device = availableDevices[i];
		std::string entry = std::to_string(i) + ": " + device.localizedName.UTF8String;
		devices.emplace_back(std::move(entry));
	}
	return devices;
}

int __mac_startCapture(int width, int height) {
	__block BOOL success = NO;
	RunOnMainQueue(^{
		MacCameraHelper *helper = [MacCameraHelper sharedInstance];
		success = [helper startCaptureWithWidth:width height:height deviceIndex:ParseSelectedDeviceIndex()];
	});
	if (!success) {
		ERROR_LOG(Log::HLE, "Mac camera startCapture failed");
		return -1;
	}
	INFO_LOG(Log::HLE, "__mac_startCapture succeeded (%dx%d)", width, height);
	return 0;
}

int __mac_stopCapture() {
	RunOnMainQueue(^{
		[[MacCameraHelper sharedInstance] stopCapture];
	});
	INFO_LOG(Log::HLE, "__mac_stopCapture");
	return 0;
}

#endif  // PPSSPP_PLATFORM(MAC)

