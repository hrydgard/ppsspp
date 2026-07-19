#import "CameraControlInputManager.h"

#import <AVFoundation/AVFoundation.h>
#import <AVKit/AVCaptureEventInteraction.h>

// Minimum iOS version required for Camera Control button hardware.
// AVCaptureEventInteraction itself is available since iOS 17.2,
// but the Camera Control button shipped with iPhone 16 / iOS 18.
static const NSUInteger kMinIOSVersionMajor = 18;

@interface CameraControlInputManager () <AVCaptureVideoDataOutputSampleBufferDelegate>

@property (nonatomic, copy) CameraControlPressCallback callback;

// Hidden 1x1pt view that holds the interaction (must be in the responder chain).
@property (nonatomic, strong) UIView *hiddenView;

// AVFoundation session + input for keeping camera hardware active.
@property (nonatomic, strong) AVCaptureSession *session;
@property (nonatomic, strong) AVCaptureDeviceInput *videoInput;

// The interaction object that receives Camera Control button events.
@property (nonatomic, strong) id interaction API_AVAILABLE(ios(17.2));

// Plan B: data output to keep session "active" for event delivery.
@property (nonatomic, strong) AVCaptureVideoDataOutput *videoOutput;

@property (nonatomic, assign) BOOL isRunning;

@end

@implementation CameraControlInputManager

- (instancetype)initWithParentView:(UIView *)view callback:(CameraControlPressCallback)callback {
    self = [super init];
    if (self) {
        _callback = [callback copy];

        // Create an invisible 1x1pt view positioned off-screen.
        // The view MUST be in the view hierarchy (responder chain) for the interaction to work.
        _hiddenView = [[UIView alloc] initWithFrame:CGRectMake(0, 0, 1, 1)];
        _hiddenView.alpha = 0.0;
        _hiddenView.clipsToBounds = YES;
        _hiddenView.userInteractionEnabled = YES;
        _hiddenView.translatesAutoresizingMaskIntoConstraints = NO;

        // Position at a safe corner (top-left, behind safe area) so it's never visible.
        [view addSubview:_hiddenView];
        [NSLayoutConstraint activateConstraints:@[
            [_hiddenView.topAnchor constraintEqualToAnchor:view.topAnchor],
            [_hiddenView.leadingAnchor constraintEqualToAnchor:view.leadingAnchor],
            [_hiddenView.widthAnchor constraintEqualToConstant:1],
            [_hiddenView.heightAnchor constraintEqualToConstant:1],
        ]];

        _isRunning = NO;
    }
    return self;
}

- (void)dealloc {
    [self stop];
    [_hiddenView removeFromSuperview];
}

#pragma mark - Public Lifecycle

- (void)start {
    if (self.isRunning) {
        return;
    }

    if (@available(iOS 18, *)) {
        [self startSessionAndInteraction];
    } else {
        // Camera Control button requires iOS 18+. On older versions this is a no-op.
        NSLog(@"CameraControlInputManager: iOS 18+ required for Camera Control button. Feature not available.");
    }
}

- (void)stop {
    if (!self.isRunning) {
        return;
    }

    [self stopSessionAndInteraction];
}

#pragma mark - Internal (iOS 18+)

API_AVAILABLE(ios(18.0))
- (void)startSessionAndInteraction {
    NSError *error = nil;

    // 1. Find the default video camera (wide-angle).
    AVCaptureDevice *camera = [AVCaptureDevice defaultDeviceWithMediaType:AVMediaTypeVideo];
    if (!camera) {
        NSLog(@"CameraControlInputManager: No camera found. Camera Control button will not work.");
        return;
    }

    // 2. Create device input.
    self.videoInput = [AVCaptureDeviceInput deviceInputWithDevice:camera error:&error];
    if (error || !self.videoInput) {
        NSLog(@"CameraControlInputManager: Failed to create camera input: %@", error.localizedDescription);
        return;
    }

    // 3. Create and configure the capture session with the lowest preset.
    self.session = [[AVCaptureSession alloc] init];
    self.session.sessionPreset = AVCaptureSessionPresetLow;

    if (![self.session canAddInput:self.videoInput]) {
        NSLog(@"CameraControlInputManager: Cannot add video input to session.");
        self.session = nil;
        return;
    }
    [self.session addInput:self.videoInput];

    // 4. (Plan A → B) Add a video data output to keep the session "active".
    //    Apple's documentation states: "The system sends capture events only to apps
    //    that actively use the camera." Without a data output, the session may not be
    //    considered "active" enough to route Camera Control events.
    //    The frames are discarded in the delegate callback.
    self.videoOutput = [[AVCaptureVideoDataOutput alloc] init];
    self.videoOutput.videoSettings = @{
        (id)kCVPixelBufferPixelFormatTypeKey : @(kCVPixelFormatType_32BGRA)
    };
    // Use a serial queue for the sample buffer delegate (frames are discarded immediately).
    dispatch_queue_t queue = dispatch_queue_create("com.ppsspp.cameracontrol.frame", DISPATCH_QUEUE_SERIAL);
    [self.videoOutput setSampleBufferDelegate:self queue:queue];
    if ([self.session canAddOutput:self.videoOutput]) {
        [self.session addOutput:self.videoOutput];
    } else {
        NSLog(@"CameraControlInputManager: Warning - could not add video data output. Events may not arrive.");
        self.videoOutput = nil;
    }

    // 5. Create the AVCaptureEventInteraction.
    //    Primary handler: receives Camera Control full-press events.
    //    Secondary handler: receives Camera Control half-press / light-press events.
    //    Use weakSelf to avoid a retain cycle (self → interaction → blocks → self).
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

    // 6. Start the session. The camera indicator will appear in the status bar.
    [self.session startRunning];

    self.isRunning = YES;
    NSLog(@"CameraControlInputManager: Session started. Camera Control button events should now arrive.");
}

API_AVAILABLE(ios(18.0))
- (void)stopSessionAndInteraction {
    // Remove the interaction from the view.
    if (self.interaction) {
        [self.hiddenView removeInteraction:self.interaction];
        self.interaction = nil;
    }

    // Stop and tear down the session.
    [self.session stopRunning];
    self.session = nil;
    self.videoInput = nil;
    self.videoOutput = nil;

    self.isRunning = NO;
    NSLog(@"CameraControlInputManager: Session stopped.");
}

#pragma mark - Event Handling

- (void)handleCaptureEvent:(AVCaptureEvent *)event isFullPress:(BOOL)isFullPress {
    switch (event.phase) {
        case AVCaptureEventPhaseBegan:
            // Button pressed down.
            if (self.callback) {
                self.callback(isFullPress, YES);
            }
            break;

        case AVCaptureEventPhaseEnded:
            // Button released.
            if (self.callback) {
                self.callback(isFullPress, NO);
            }
            break;

        case AVCaptureEventPhaseCancelled:
            // Event cancelled (e.g., app backgrounded during press).
            // Send an UP to avoid a stuck key.
            if (self.callback) {
                self.callback(isFullPress, NO);
            }
            break;

        default:
            break;
    }
}

#pragma mark - AVCaptureVideoDataOutputSampleBufferDelegate

- (void)captureOutput:(AVCaptureOutput *)output
        didOutputSampleBuffer:(CMSampleBufferRef)sampleBuffer
        fromConnection:(AVCaptureConnection *)connection {
    // Frames are intentionally discarded. The session exists only to keep the
    // camera hardware "active" so AVCaptureEventInteraction can deliver events.
    // The system manages the buffer lifecycle - do nothing.
}

- (void)captureOutput:(AVCaptureOutput *)output
        didDropSampleBuffer:(CMSampleBufferRef)sampleBuffer
        fromConnection:(AVCaptureConnection *)connection {
    // Dropped frames are expected and harmless — we discard everything anyway.
}

@end
