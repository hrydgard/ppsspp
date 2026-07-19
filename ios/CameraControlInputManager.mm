#import "CameraControlInputManager.h"

#import <AVFoundation/AVFoundation.h>
#import <AVKit/AVCaptureEventInteraction.h>

// Minimum iOS version required for Camera Control button hardware.
static const NSUInteger kMinIOSVersionMajor = 18;

@interface CameraControlInputManager ()

@property (nonatomic, copy) CameraControlPressCallback callback;

// Hidden 1x1pt view that holds the interaction (must be in the responder chain).
@property (nonatomic, strong) UIView *hiddenView;

// AVFoundation session + input for camera access (required by system for event delivery).
@property (nonatomic, strong) AVCaptureSession *session;
@property (nonatomic, strong) AVCaptureDeviceInput *videoInput;

// The interaction object that receives Camera Control button events.
@property (nonatomic, strong) id interaction;

@property (nonatomic, assign) BOOL sessionActive;
@property (nonatomic, assign) BOOL permissionRequested;

@end

@implementation CameraControlInputManager

- (instancetype)initWithParentView:(UIView *)view callback:(CameraControlPressCallback)callback {
    self = [super init];
    if (self) {
        _callback = [callback copy];

        _hiddenView = [[UIView alloc] initWithFrame:CGRectMake(0, 0, 1, 1)];
        _hiddenView.clipsToBounds = YES;
        _hiddenView.userInteractionEnabled = YES;
        _hiddenView.translatesAutoresizingMaskIntoConstraints = NO;

        [view addSubview:_hiddenView];
        [NSLayoutConstraint activateConstraints:@[
            [_hiddenView.topAnchor constraintEqualToAnchor:view.topAnchor],
            [_hiddenView.leadingAnchor constraintEqualToAnchor:view.leadingAnchor],
            [_hiddenView.widthAnchor constraintEqualToConstant:1],
            [_hiddenView.heightAnchor constraintEqualToConstant:1],
        ]];

        _sessionActive = NO;
        _permissionRequested = NO;
    }
    return self;
}

- (void)dealloc {
    [self stop];
    [_hiddenView removeFromSuperview];
}

#pragma mark - Public Lifecycle

- (void)start {
    if (self.sessionActive) {
        return;
    }

    if (![self isIOS18OrLater]) {
        NSLog(@"CameraControlInputManager: iOS 18+ required. Feature not available.");
        return;
    }

    [self requestCameraAccessAndSetup];
}

- (void)stop {
    if (!self.sessionActive) {
        return;
    }

    [self stopSessionAndInteraction];
}

#pragma mark - Camera Permission

- (BOOL)isIOS18OrLater {
    if (@available(iOS 18, *)) {
        return YES;
    }
    return NO;
}

- (void)requestCameraAccessAndSetup {
    AVAuthorizationStatus status = [AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeVideo];

    switch (status) {
        case AVAuthorizationStatusAuthorized:
            [self setupSessionWithCamera];
            break;

        case AVAuthorizationStatusNotDetermined:
            self.permissionRequested = YES;
            [AVCaptureDevice requestAccessForMediaType:AVMediaTypeVideo completionHandler:^(BOOL granted) {
                dispatch_async(dispatch_get_main_queue(), ^{
                    if (granted) {
                        [self setupSessionWithCamera];
                    } else {
                        NSLog(@"CameraControlInputManager: Camera permission denied. Camera Control button will not work.");
                    }
                });
            }];
            break;

        case AVAuthorizationStatusDenied:
        case AVAuthorizationStatusRestricted:
            NSLog(@"CameraControlInputManager: Camera access denied/restricted. Camera Control button will not work.");
            break;
    }
}

#pragma mark - Session Setup (requires camera permission)

- (void)setupSessionWithCamera {
    NSError *error = nil;

    AVCaptureDevice *camera = [AVCaptureDevice defaultDeviceWithMediaType:AVMediaTypeVideo];
    if (!camera) {
        NSLog(@"CameraControlInputManager: No camera found.");
        return;
    }

    AVCaptureDeviceInput *input = [AVCaptureDeviceInput deviceInputWithDevice:camera error:&error];
    if (error || !input) {
        NSLog(@"CameraControlInputManager: Failed to create camera input: %@", error.localizedDescription);
        return;
    }
    self.videoInput = input;

    AVCaptureSession *session = [[AVCaptureSession alloc] init];
    session.sessionPreset = AVCaptureSessionPresetLow;

    if (![session canAddInput:input]) {
        NSLog(@"CameraControlInputManager: Cannot add video input to session.");
        return;
    }
    [session addInput:input];

    // Create the interaction
    __weak typeof(self) weakSelf = self;
    AVCaptureEventInteraction *interaction = [[AVCaptureEventInteraction alloc]
        initWithPrimaryEventHandler:^(AVCaptureEvent *event) {
            typeof(self) strongSelf = weakSelf;
            if (!strongSelf) return;
            [strongSelf handleCaptureEvent:event isFullPress:YES];
        }
        secondaryEventHandler:^(AVCaptureEvent *event) {
            typeof(self) strongSelf = weakSelf;
            if (!strongSelf) return;
            [strongSelf handleCaptureEvent:event isFullPress:NO];
        }];

    interaction.enabled = YES;
    [self.hiddenView addInteraction:interaction];
    self.interaction = interaction;

    self.session = session;
    [session startRunning];

    self.sessionActive = YES;
    NSLog(@"CameraControlInputManager: Session started with camera input. Camera Control button events active.");
}

- (void)stopSessionAndInteraction {
    [self.session stopRunning];
    self.session = nil;
    self.videoInput = nil;

    if (self.interaction) {
        [self.hiddenView removeInteraction:self.interaction];
        self.interaction = nil;
    }

    self.sessionActive = NO;
    NSLog(@"CameraControlInputManager: Session stopped.");
}

#pragma mark - Event Handling

- (void)handleCaptureEvent:(AVCaptureEvent *)event isFullPress:(BOOL)isFullPress {
    switch (event.phase) {
        case AVCaptureEventPhaseBegan:
            if (self.callback) {
                self.callback(isFullPress, YES);
            }
            break;

        case AVCaptureEventPhaseEnded:
            if (self.callback) {
                self.callback(isFullPress, NO);
            }
            break;

        case AVCaptureEventPhaseCancelled:
            if (self.callback) {
                self.callback(isFullPress, NO);
            }
            break;

        default:
            break;
    }
}

@end
