#import "ios/CameraHelper.h"
#import "ios/ViewControllerCommon.h"
#include "Common/System/Request.h"
#include "Core/HLE/sceUsbCam.h"
#include "Core/HLE/sceUsbGps.h"

@interface PPSSPPBaseViewController () {
	int imageRequestId;
	NSString *imageFilename;
	CameraHelper *cameraHelper;
	LocationHelper *locationHelper;
}

@end

@implementation PPSSPPBaseViewController {
}

- (void)pickPhoto:(NSString *)saveFilename requestId:(int)requestId {
	imageRequestId = requestId;
	imageFilename = saveFilename;
	NSLog(@"Picking photo to save to %@ (id: %d)", saveFilename, requestId);

	UIImagePickerController *picker = [[UIImagePickerController alloc] init];
	picker.sourceType = UIImagePickerControllerSourceTypePhotoLibrary;
	picker.delegate = self;
	[self presentViewController:picker animated:YES completion:nil];
}

- (void)imagePickerController:(UIImagePickerController *)picker
		didFinishPickingMediaWithInfo:(NSDictionary<UIImagePickerControllerInfoKey,id> *)info {

	UIImage *image = info[UIImagePickerControllerOriginalImage];

	// Convert to JPEG with 90% quality
	NSData *jpegData = UIImageJPEGRepresentation(image, 0.9);
	if (jpegData) {
		// Do something with the JPEG data (e.g., save to file)
		[jpegData writeToFile:imageFilename atomically:YES];
		NSLog(@"Saved JPEG image to %@", imageFilename);
		g_requestManager.PostSystemSuccess(imageRequestId, "", 1);
	} else {
		g_requestManager.PostSystemFailure(imageRequestId);
	}

	[picker dismissViewControllerAnimated:YES completion:nil];
	[self hideKeyboard];
}

- (void)imagePickerControllerDidCancel:(UIImagePickerController *)picker {
	NSLog(@"User cancelled image picker");

	[picker dismissViewControllerAnimated:YES completion:nil];

	// You can also call your custom callback or use the requestId here
	g_requestManager.PostSystemFailure(imageRequestId);
	[self hideKeyboard];
}

- (void)startVideo:(int)width height:(int)height {
	[cameraHelper startVideo:width h:height];
}

- (void)stopVideo {
	[cameraHelper stopVideo];
}

- (void)PushCameraImageIOS:(long long)len buffer:(unsigned char*)data {
	Camera::pushCameraImage(len, data);
}

- (void)startLocation {
	[locationHelper startLocationUpdates];
}

- (void)stopLocation {
	[locationHelper stopLocationUpdates];
}

- (void)SetGpsDataIOS:(CLLocation *)newLocation {
	GPS::setGpsData((long long)newLocation.timestamp.timeIntervalSince1970,
					newLocation.horizontalAccuracy/5.0,
					newLocation.coordinate.latitude, newLocation.coordinate.longitude,
					newLocation.altitude,
					MAX(newLocation.speed * 3.6, 0.0), /* m/s to km/h */
					0 /* bearing */);
}

- (void)viewDidLoad {
	[super viewDidLoad];

	cameraHelper = [[CameraHelper alloc] init];
	[cameraHelper setDelegate:self];

	locationHelper = [[LocationHelper alloc] init];
	[locationHelper setDelegate:self];
}

@end
