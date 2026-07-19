#import "CameraControlInputManager.h"

#import <AVFoundation/AVFoundation.h>
#import <AVKit/AVCaptureEventInteraction.h>

// Minimum iOS version required for Camera Control button hardware.
// AVCaptureEventInteraction itself is available since iOS 17.2,
// but the Camera Control button shipped with iPhone 16 / iOS 18.
static const NSUInteger kMinIOSVersionMajor = 18;

@interface CameraControlInputManager ()

@property (nonatomic, copy) CameraControlPressCallback callback;

// Hidden 1x1pt view that holds the interaction (must be in the responder chain).
@property (nonatomic, strong) UIView *hiddenView;

// Lightweight AVCaptureSession (no inputs/outputs) to satisfy system requirement
// for Camera Control event delivery.
@property (nonatomic, strong) AVCaptureSession *session;

// The interaction object that receives Camera Control button events.
@property (nonatomic, strong) id interaction;

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

- (void)startSessionAndInteraction {
    // 1. Create the AVCaptureEventInteraction.
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

    // 2. Create an empty AVCaptureSession (no inputs or outputs).
    //    The system requires a running AVCaptureSession in the app to deliver
    //    Camera Control events via AVCaptureEventInteraction. An empty session
    //    satisfies this requirement without accessing camera hardware — no
    //    camera permission needed, no camera indicator in the status bar.
    self.session = [[AVCaptureSession alloc] init];
    [self.session startRunning];

    self.isRunning = YES;
    NSLog(@"CameraControlInputManager: Session started (no camera access). Camera Control button events should now arrive.");
}

- (void)stopSessionAndInteraction {
    // Stop the session first.
    [self.session stopRunning];
    self.session = nil;

    // Remove the interaction from the view.
    if (self.interaction) {
        [self.hiddenView removeInteraction:self.interaction];
        self.interaction = nil;
    }

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

@end
