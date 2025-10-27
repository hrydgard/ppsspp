#include "Common/System/Request.h"
#import "ios/ViewControllerCommon.h"


@interface PPSSPPBaseViewController () {
	int imageRequestId;
	NSString *imageFilename;
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

@end
