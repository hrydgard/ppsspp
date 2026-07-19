#import "CameraControlInputManager.h"

#import <AVFoundation/AVFoundation.h>
#import <AVKit/AVCaptureEventInteraction.h>

@interface CameraControlInputManager () <AVCaptureVideoDataOutputSampleBufferDelegate>

@property (nonatomic, copy) CameraControlPressCallback callback;
@property (nonatomic, weak) UIView *parentView;

@property (nonatomic, strong) AVCaptureSession *session;
@property (nonatomic, strong) AVCaptureDeviceInput *videoInput;
@property (nonatomic, strong) AVCaptureVideoDataOutput *videoOutput;
@property (nonatomic, strong) dispatch_queue_t cameraQueue;
@property (nonatomic, strong) id interaction;

@property (nonatomic, assign, getter=isSessionActive) BOOL sessionActive;
@property (nonatomic, copy, readwrite) NSString *status;

@end

@implementation CameraControlInputManager

@synthesize status = _status;

- (instancetype)initWithParentView:(UIView *)view callback:(CameraControlPressCallback)callback {
    self = [super init];
    if (self) {
        _callback = [callback copy];
        _parentView = view;
        _sessionActive = NO;
        _status = @"starting...";

        // Start immediately — don't wait for didBecomeActive (may not fire in LiveContainer).
        [self requestCameraAccessAndSetup];
    }
    return self;
}

- (void)dealloc {
    [self stopInternal];
}

#pragma mark - Public

- (void)start {
    if (self.sessionActive) return;
    [self requestCameraAccessAndSetup];
}

- (void)stop {
    [self stopInternal];
}

#pragma mark - Camera Permission

- (void)requestCameraAccessAndSetup {
    AVAuthorizationStatus status = [AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeVideo];

    switch (status) {
        case AVAuthorizationStatusAuthorized:
            self.status = @"perm: granted";
            [self setupSession];
            break;

        case AVAuthorizationStatusNotDetermined: {
            self.status = @"perm: requesting...";
            [AVCaptureDevice requestAccessForMediaType:AVMediaTypeVideo completionHandler:^(BOOL granted) {
                dispatch_async(dispatch_get_main_queue(), ^{
                    if (granted) {
                        self.status = @"perm: granted";
                        [self setupSession];
                    } else {
                        self.status = @"perm: DENIED";
                        NSLog(@"CameraControlInputManager: Permission denied.");
                    }
                });
            }];
            break;
        }

        case AVAuthorizationStatusDenied:
            self.status = @"perm: DENIED (system)";
            NSLog(@"CameraControlInputManager: Camera access denied.");
            break;

        case AVAuthorizationStatusRestricted:
            self.status = @"perm: RESTRICTED";
            NSLog(@"CameraControlInputManager: Camera access restricted.");
            break;
    }

}

#pragma mark - Session Setup (API_AVAILABLE(ios(17.2)))

- (void)setupSession API_AVAILABLE(ios(17.2)) {
    NSError *error = nil;

    self.status = @"discovering camera...";

    // Use discovery session for more reliable device finding.
    AVCaptureDeviceDiscoverySession *discovery = [AVCaptureDeviceDiscoverySession
        discoverySessionWithDeviceTypes:@[AVCaptureDeviceTypeBuiltInWideAngleCamera]
                              mediaType:AVMediaTypeVideo
                               position:AVCaptureDevicePositionUnspecified];
    AVCaptureDevice *camera = discovery.devices.firstObject;
    if (!camera) {
        self.status = @"❌ no camera found";
        NSLog(@"CameraControlInputManager: No camera device found.");
        return;
    }

    self.status = @"creating camera input...";
    AVCaptureDeviceInput *input = [AVCaptureDeviceInput deviceInputWithDevice:camera error:&error];
    if (error || !input) {
        self.status = [NSString stringWithFormat:@"❌ input failed: %@", error.localizedDescription];
        NSLog(@"CameraControlInputManager: Failed to create input: %@", error.localizedDescription);
        return;
    }
    self.videoInput = input;

    self.status = @"creating session...";
    AVCaptureSession *session = [[AVCaptureSession alloc] init];
    session.sessionPreset = AVCaptureSessionPresetLow;

    if (![session canAddInput:input]) {
        self.status = @"❌ cannot add input";
        NSLog(@"CameraControlInputManager: Cannot add input to session.");
        return;
    }
    [session addInput:input];

    // Add a video data output to force the camera hardware to actually stream.
    // Without an output, the camera may report isRunning=YES but the hardware
    // never powers on, so the system doesn't show the green indicator and
    // AVCaptureEventInteraction doesn't receive events.
    self.status = @"adding video output...";
    AVCaptureVideoDataOutput *videoOutput = [[AVCaptureVideoDataOutput alloc] init];
    videoOutput.alwaysDiscardsLateVideoFrames = YES;
    videoOutput.videoSettings = nil; // Use default format.
    // Set delegate to force the camera pipeline to fully activate.
    self.cameraQueue = dispatch_queue_create("com.ppsspp.camera-frame-queue", DISPATCH_QUEUE_SERIAL);
    [videoOutput setSampleBufferDelegate:self queue:self.cameraQueue];
    if ([session canAddOutput:videoOutput]) {
        [session addOutput:videoOutput];
    } else {
        self.status = @"⚠️ cannot add video output";
        NSLog(@"CameraControlInputManager: Cannot add video output.");
    }
    self.videoOutput = videoOutput;
    self.session = session;

    self.status = @"creating interaction...";

    // Create interaction and add it directly to parentView.
    __weak typeof(self) weakSelf = self;
    AVCaptureEventInteraction *interaction = [[AVCaptureEventInteraction alloc]
        initWithPrimaryEventHandler:^(AVCaptureEvent *event) {
            typeof(self) strongSelf = weakSelf;
            if (!strongSelf) return;
            [strongSelf handleEvent:event isFullPress:YES];
        }
        secondaryEventHandler:^(AVCaptureEvent *event) {
            typeof(self) strongSelf = weakSelf;
            if (!strongSelf) return;
            [strongSelf handleEvent:event isFullPress:NO];
        }];
    interaction.enabled = YES;

    UIView *targetView = self.parentView;
    if (!targetView) {
        self.status = @"❌ parent view gone";
        NSLog(@"CameraControlInputManager: Parent view gone.");
        return;
    }
    [targetView addInteraction:interaction];
    self.interaction = interaction;

    self.status = @"starting session...";
    [session startRunning];

    // Async check if session actually started.
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(1.0 * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
        if (session.isRunning) {
            self.sessionActive = YES;
            self.status = @"✅ ACTIVE (session running)";
            NSLog(@"CameraControlInputManager: Session is RUNNING. Camera Control events active.");
        } else {
            self.status = @"❌ session NOT running (startRunning failed)";
            NSLog(@"CameraControlInputManager: Session failed to start (isRunning=NO).");
        }
    });
}

- (void)stopInternal {
    if (self.interaction) {
        UIView *targetView = self.parentView;
        if (targetView) {
            [targetView removeInteraction:self.interaction];
        }
        self.interaction = nil;
    }

    // Remove video output delegate to break retain cycle.
    [self.videoOutput setSampleBufferDelegate:nil queue:nil];

    [self.session stopRunning];
    self.session = nil;
    self.videoInput = nil;
    self.videoOutput = nil;
    self.sessionActive = NO;
}

#pragma mark - Event Handling (API_AVAILABLE(ios(17.2)))

- (void)handleEvent:(AVCaptureEvent *)event isFullPress:(BOOL)isFullPress API_AVAILABLE(ios(17.2)) {
    BOOL isDown;
    switch (event.phase) {
        case AVCaptureEventPhaseBegan:
            isDown = YES;
            break;
        case AVCaptureEventPhaseEnded:
        case AVCaptureEventPhaseCancelled:
            isDown = NO;
            break;
        default:
            return;
    }
    if (self.callback) {
        self.callback(isFullPress, isDown);
    }
}

#pragma mark - AVCaptureVideoDataOutputSampleBufferDelegate

- (void)captureOutput:(AVCaptureOutput *)output didDropSampleBuffer:(CMSampleBufferRef)sampleBuffer fromConnection:(AVCaptureConnection *)connection {
    // Frames are intentionally discarded — we only need the camera hardware active.
}

- (void)captureOutput:(AVCaptureOutput *)output didOutputSampleBuffer:(CMSampleBufferRef)sampleBuffer fromConnection:(AVCaptureConnection *)connection {
    // Frames are intentionally discarded — we only need the camera hardware active.
}

@end
