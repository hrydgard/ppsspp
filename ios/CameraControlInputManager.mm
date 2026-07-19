#import "CameraControlInputManager.h"

#import <AVFoundation/AVFoundation.h>
#import <AVKit/AVCaptureEventInteraction.h>

@interface CameraControlInputManager ()

@property (nonatomic, copy) CameraControlPressCallback callback;
@property (nonatomic, weak) UIView *parentView;

@property (nonatomic, strong) AVCaptureSession *session;
@property (nonatomic, strong) AVCaptureDeviceInput *videoInput;
@property (nonatomic, strong) AVCaptureVideoDataOutput *videoOutput;
@property (nonatomic, strong) id interaction;

@property (nonatomic, assign, getter=isSessionActive) BOOL sessionActive;

@end

@implementation CameraControlInputManager

- (instancetype)initWithParentView:(UIView *)view callback:(CameraControlPressCallback)callback {
    self = [super init];
    if (self) {
        _callback = [callback copy];
        _parentView = view;
        _sessionActive = NO;

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
            [self setupSession];
            break;

        case AVAuthorizationStatusNotDetermined: {
            [AVCaptureDevice requestAccessForMediaType:AVMediaTypeVideo completionHandler:^(BOOL granted) {
                dispatch_async(dispatch_get_main_queue(), ^{
                    if (granted) {
                        [self setupSession];
                    } else {
                        NSLog(@"CameraControlInputManager: Permission denied.");
                    }
                });
            }];
            break;
        }

        case AVAuthorizationStatusDenied:
            NSLog(@"CameraControlInputManager: Camera access denied.");
            break;

        case AVAuthorizationStatusRestricted:
            NSLog(@"CameraControlInputManager: Camera access restricted.");
            break;
    }

}

#pragma mark - Session Setup (API_AVAILABLE(ios(17.2)))

- (void)setupSession API_AVAILABLE(ios(17.2)) {
    NSError *error = nil;

    // Use discovery session for more reliable device finding.
    AVCaptureDeviceDiscoverySession *discovery = [AVCaptureDeviceDiscoverySession
        discoverySessionWithDeviceTypes:@[AVCaptureDeviceTypeBuiltInWideAngleCamera]
                              mediaType:AVMediaTypeVideo
                               position:AVCaptureDevicePositionUnspecified];
    AVCaptureDevice *camera = discovery.devices.firstObject;
    if (!camera) {
        NSLog(@"CameraControlInputManager: No camera device found.");
        return;
    }

    AVCaptureDeviceInput *input = [AVCaptureDeviceInput deviceInputWithDevice:camera error:&error];
    if (error || !input) {
        NSLog(@"CameraControlInputManager: Failed to create input: %@", error.localizedDescription);
        return;
    }
    self.videoInput = input;

    AVCaptureSession *session = [[AVCaptureSession alloc] init];
    session.sessionPreset = AVCaptureSessionPresetLow;

    if (![session canAddInput:input]) {
        NSLog(@"CameraControlInputManager: Cannot add input to session.");
        return;
    }
    [session addInput:input];

    // Add a video data output to force the camera hardware to actually stream.
    // Without an output, the camera may report isRunning=YES but the hardware
    // never powers on, so the system doesn't show the green indicator and
    // AVCaptureEventInteraction doesn't receive events.
    AVCaptureVideoDataOutput *videoOutput = [[AVCaptureVideoDataOutput alloc] init];
    videoOutput.alwaysDiscardsLateVideoFrames = YES;
    videoOutput.videoSettings = nil; // Use default format.
    if ([session canAddOutput:videoOutput]) {
        [session addOutput:videoOutput];
    } else {
        NSLog(@"CameraControlInputManager: Cannot add video output.");
    }
    self.videoOutput = videoOutput;
    self.session = session;

    // Create interaction and add it directly to parentView.
    // BOTH primary (full press) and secondary (light press) handlers dispatch the
    // same NKCODE_EXT_CAMERA_CONTROL, because the system may not deliver light-press
    // events to the secondary handler outside of a full camera-control interface.
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
            // Route light-press events to the same key code as full press.
            [strongSelf handleEvent:event isFullPress:YES];
        }];
    interaction.enabled = YES;

    UIView *targetView = self.parentView;
    if (!targetView) {
        NSLog(@"CameraControlInputManager: Parent view gone.");
        return;
    }
    [targetView addInteraction:interaction];
    self.interaction = interaction;

    [session startRunning];

    // Async check if session actually started.
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(1.0 * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
        if (session.isRunning) {
            self.sessionActive = YES;
            NSLog(@"CameraControlInputManager: Session is RUNNING. Camera Control events active.");
        } else {
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

@end
