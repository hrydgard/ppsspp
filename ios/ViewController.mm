//
// ViewController.m
//
// Created by rock88
// Modified by xSacha
//

#import "ViewController.h"
#import "AudioEngine.h"
#import <GLKit/GLKit.h>

#include "base/display.h"
#include "base/timeutil.h"
#include "file/zip_read.h"
#include "input/input_state.h"
#include "net/resolve.h"
#include "ui/screen.h"
#include "input/keycodes.h"

#include "Core/Config.h"
#include "gfx_es2/fbo.h"

#define IS_IPAD() ([UIDevice currentDevice].userInterfaceIdiom == UIUserInterfaceIdiomPad)

float dp_xscale = 1.0f;
float dp_yscale = 1.0f;

double lastSelectPress = 0.0f;
double lastStartPress = 0.0f;
bool simulateAnalog = false;

extern ScreenManager *screenManager;
InputState input_state;

extern std::string ram_temp_file;
extern bool iosCanUseJit;

ViewController* sharedViewController;

@interface ViewController ()
{
	std::map<uint16_t, uint16_t> iCadeToKeyMap;
}

@property (strong, nonatomic) EAGLContext *context;
@property (nonatomic,retain) NSString* documentsPath;
@property (nonatomic,retain) NSString* bundlePath;
@property (nonatomic,retain) NSMutableArray* touches;
@property (nonatomic,retain) AudioEngine* audioEngine;
@property (nonatomic,retain) iCadeReaderView *iCadeView;
@property (nonatomic,retain) GCController *gameController __attribute__((weak_import));

@end

@implementation ViewController
@synthesize documentsPath,bundlePath,touches,audioEngine,iCadeView;

- (id)init
{
	self = [super init];
	if (self) {
		sharedViewController = self;
		self.touches = [[[NSMutableArray alloc] init] autorelease];

		self.documentsPath = [NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES) objectAtIndex:0];
		self.bundlePath = [[[NSBundle mainBundle] resourcePath] stringByAppendingString:@"/assets/"];

		memset(&input_state, 0, sizeof(input_state));

		net::Init();

		ram_temp_file = [[NSTemporaryDirectory() stringByAppendingPathComponent:@"ram_tmp.file"] fileSystemRepresentation];
		NativeInit(0, NULL, [self.documentsPath UTF8String], [self.bundlePath UTF8String], NULL);
    iosCanUseJit = false;

		NSArray *jailPath = [NSArray arrayWithObjects:
							@"/Applications/Cydia.app",
							@"/private/var/lib/apt" ,
							@"/private/var/stash" ,
							@"/usr/sbin/sshd" ,
							@"/usr/bin/sshd" , nil];

		for(NSString *string in jailPath)
		{
			if ([[NSFileManager defaultManager] fileExistsAtPath:string])
        iosCanUseJit = true;
		}

		iCadeToKeyMap[iCadeJoystickUp]		= NKCODE_DPAD_UP;
		iCadeToKeyMap[iCadeJoystickRight]	= NKCODE_DPAD_RIGHT;
		iCadeToKeyMap[iCadeJoystickDown]	= NKCODE_DPAD_DOWN;
		iCadeToKeyMap[iCadeJoystickLeft]	= NKCODE_DPAD_LEFT;
		iCadeToKeyMap[iCadeButtonA]			= NKCODE_BUTTON_9; // Select
		iCadeToKeyMap[iCadeButtonB]			= NKCODE_BUTTON_7; // LTrigger
		iCadeToKeyMap[iCadeButtonC]			= NKCODE_BUTTON_10; // Start
		iCadeToKeyMap[iCadeButtonD]			= NKCODE_BUTTON_8; // RTrigger
		iCadeToKeyMap[iCadeButtonE]			= NKCODE_BUTTON_4; // Square
		iCadeToKeyMap[iCadeButtonF]			= NKCODE_BUTTON_2; // Cross
		iCadeToKeyMap[iCadeButtonG]			= NKCODE_BUTTON_1; // Triangle
		iCadeToKeyMap[iCadeButtonH]			= NKCODE_BUTTON_3; // Circle
        
		if ([GCController class]) { // Checking the availability of a GameController framework
            [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(controllerDidConnect:) name:GCControllerDidConnectNotification object:nil];
            [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(controllerDidDisconnect:) name:GCControllerDidDisconnectNotification object:nil];
		}
	}
	return self;
}

- (void)viewDidLoad
{
	[super viewDidLoad];

	self.view.frame = [[UIScreen mainScreen] bounds];
	self.view.multipleTouchEnabled = YES;
	self.context = [[[EAGLContext alloc] initWithAPI:kEAGLRenderingAPIOpenGLES2] autorelease];

	GLKView *view = (GLKView *)self.view;
	view.context = self.context;
	view.drawableDepthFormat = GLKViewDrawableDepthFormat24;
	[EAGLContext setCurrentContext:self.context];
	self.preferredFramesPerSecond = 60;

	float scale = [UIScreen mainScreen].scale;
	CGSize size = [[UIApplication sharedApplication].delegate window].frame.size;

	if (size.height > size.width) {
		float h = size.height;
		size.height = size.width;
		size.width = h;
	}

	g_dpi = (IS_IPAD() ? 200 : 150) * scale;
	g_dpi_scale = 240.0f / (float)g_dpi;
	pixel_xres = size.width * scale;
	pixel_yres = size.height * scale;
	pixel_in_dps = (float)pixel_xres / (float)dp_xres;

	dp_xres = pixel_xres * g_dpi_scale;
	dp_yres = pixel_yres * g_dpi_scale;

	NativeInitGraphics();

	dp_xscale = (float)dp_xres / (float)pixel_xres;
	dp_yscale = (float)dp_yres / (float)pixel_yres;

/*
	UISwipeGestureRecognizer* gesture = [[[UISwipeGestureRecognizer alloc] initWithTarget:self action:@selector(swipeGesture:)] autorelease];
	[self.view addGestureRecognizer:gesture];
*/

	self.iCadeView = [[iCadeReaderView alloc] init];
	[self.view addSubview:self.iCadeView];
	self.iCadeView.delegate = self;
	self.iCadeView.active = YES;
}

- (void)viewDidUnload
{
	[super viewDidUnload];

	if ([EAGLContext currentContext] == self.context) {
		[EAGLContext setCurrentContext:nil];
	}
	self.context = nil;
}

- (void)didReceiveMemoryWarning
{
	[super didReceiveMemoryWarning];
}

- (void)dealloc
{
	[self viewDidUnload];

	if ([GCController class]) self.gameController = nil;
	self.iCadeView = nil;
	self.audioEngine = nil;
	self.touches = nil;
	self.documentsPath = nil;
	self.bundlePath = nil;

	NativeShutdown();
	[super dealloc];
}

// For iOS before 6.0
- (BOOL)shouldAutorotateToInterfaceOrientation:(UIInterfaceOrientation)toInterfaceOrientation
{
	return UIInterfaceOrientationIsLandscape(toInterfaceOrientation);
}

// For iOS 6.0 and up
- (NSUInteger)supportedInterfaceOrientations
{
	return UIInterfaceOrientationMaskLandscape;
}

- (void)glkView:(GLKView *)view drawInRect:(CGRect)rect
{
	{
		lock_guard guard(input_state.lock);
		UpdateInputState(&input_state);
		NativeUpdate(input_state);
		EndInputState(&input_state);
	}

	NativeRender();
	time_update();
}

- (void)swipeGesture:(id)sender
{
	// TODO: Use a swipe gesture to handle BACK
}

- (void)touchX:(float)x y:(float)y code:(int)code pointerId:(int)pointerId
{
	lock_guard guard(input_state.lock);

	float scale = [UIScreen mainScreen].scale;

	float scaledX = (int)(x * dp_xscale) * scale;
	float scaledY = (int)(y * dp_yscale) * scale;

	TouchInput input;

	input_state.pointer_x[pointerId] = scaledX;
	input_state.pointer_y[pointerId] = scaledY;
	input.x = scaledX;
	input.y = scaledY;
	switch (code) {
		case 1 :
			input_state.pointer_down[pointerId] = true;
			input.flags = TOUCH_DOWN;
			break;

		case 2 :
			input_state.pointer_down[pointerId] = false;
			input.flags = TOUCH_UP;
			break;

		default :
			input.flags = TOUCH_MOVE;
			break;
	}
	input_state.mouse_valid = true;
	input.id = pointerId;
	NativeTouch(input);
}

- (NSDictionary*)touchDictBy:(UITouch*)touch
{
	for (NSDictionary* dict in self.touches) {
		if ([dict objectForKey:@"touch"] == touch)
			return dict;
	}
	return nil;
}

- (int)freeTouchIndex
{
	int index = 0;

	for (NSDictionary* dict in self.touches)
	{
		int i = [[dict objectForKey:@"index"] intValue];
		if (index == i)
			index = i+1;
	}

	return index;
}

- (void)touchesBegan:(NSSet *)_touches withEvent:(UIEvent *)event
{
	for(UITouch* touch in _touches) {
		NSDictionary* dict = @{@"touch":touch,@"index":@([self freeTouchIndex])};
		[self.touches addObject:dict];
		CGPoint point = [touch locationInView:self.view];
		[self touchX:point.x y:point.y code:1 pointerId:[[dict objectForKey:@"index"] intValue]];
	}
}

- (void)touchesMoved:(NSSet *)_touches withEvent:(UIEvent *)event
{
	for(UITouch* touch in _touches) {
		CGPoint point = [touch locationInView:self.view];
		NSDictionary* dict = [self touchDictBy:touch];
		[self touchX:point.x y:point.y code:0 pointerId:[[dict objectForKey:@"index"] intValue]];
	}
}

- (void)touchesEnded:(NSSet *)_touches withEvent:(UIEvent *)event
{
	for(UITouch* touch in _touches) {
		CGPoint point = [touch locationInView:self.view];
		NSDictionary* dict = [self touchDictBy:touch];
		[self touchX:point.x y:point.y code:2 pointerId:[[dict objectForKey:@"index"] intValue]];
		[self.touches removeObject:dict];
	}
}

- (void)bindDefaultFBO
{
	[(GLKView*)self.view bindDrawable];
}

void LaunchBrowser(char const* url)
{
	[[UIApplication sharedApplication] openURL:[NSURL URLWithString:[NSString stringWithCString:url encoding:NSStringEncodingConversionAllowLossy]]];
}

void bindDefaultFBO()
{
	[sharedViewController bindDefaultFBO];
}

void EnableFZ(){};
void DisableFZ(){};

- (void)buttonDown:(iCadeState)button
{
	if (simulateAnalog &&
		((button == iCadeJoystickUp) ||
		 (button == iCadeJoystickDown) ||
		 (button == iCadeJoystickLeft) ||
		 (button == iCadeJoystickRight))) {
			AxisInput axis;
			switch (button) {
				case iCadeJoystickUp :
					axis.axisId = JOYSTICK_AXIS_Y;
					axis.value = -1.0f;
					break;
					
				case iCadeJoystickDown :
					axis.axisId = JOYSTICK_AXIS_Y;
					axis.value = 1.0f;
					break;
					
				case iCadeJoystickLeft :
					axis.axisId = JOYSTICK_AXIS_X;
					axis.value = -1.0f;
					break;
					
				case iCadeJoystickRight :
					axis.axisId = JOYSTICK_AXIS_X;
					axis.value = 1.0f;
					break;
					
				default:
					break;
			}
			axis.deviceId = DEVICE_ID_PAD_0;
			axis.flags = 0;
			NativeAxis(axis);
		} else {
			KeyInput key;
			key.flags = KEY_DOWN;
			key.keyCode = iCadeToKeyMap[button];
			key.deviceId = DEVICE_ID_PAD_0;
			NativeKey(key);
		}
}

- (void)buttonUp:(iCadeState)button
{
	if (button == iCadeButtonA) {
		// Pressing Select twice within 1 second toggles the DPad between
		//     normal operation and simulating the Analog stick.
		if ((lastSelectPress + 1.0f) > time_now_d())
			simulateAnalog = !simulateAnalog;
		lastSelectPress = time_now_d();
	}
	
	if (button == iCadeButtonC) {
		// Pressing Start twice within 1 second will take to the Emu menu
		if ((lastStartPress + 1.0f) > time_now_d()) {
			KeyInput key;
			key.flags = KEY_DOWN;
			key.keyCode = NKCODE_ESCAPE;
			key.deviceId = DEVICE_ID_KEYBOARD;
			NativeKey(key);
			return;
		}
		lastStartPress = time_now_d();
	}
	
	if (simulateAnalog &&
		((button == iCadeJoystickUp) ||
		 (button == iCadeJoystickDown) ||
		 (button == iCadeJoystickLeft) ||
		 (button == iCadeJoystickRight))) {
		AxisInput axis;
		switch (button) {
			case iCadeJoystickUp :
				axis.axisId = JOYSTICK_AXIS_Y;
				axis.value = 0.0f;
				break;
				
			case iCadeJoystickDown :
				axis.axisId = JOYSTICK_AXIS_Y;
				axis.value = 0.0f;
				break;
				
			case iCadeJoystickLeft :
				axis.axisId = JOYSTICK_AXIS_X;
				axis.value = 0.0f;
				break;
				
			case iCadeJoystickRight :
				axis.axisId = JOYSTICK_AXIS_X;
				axis.value = 0.0f;
				break;
				
			default:
				break;
		}
		axis.deviceId = DEVICE_ID_PAD_0;
		axis.flags = 0;
		NativeAxis(axis);
	} else {
		KeyInput key;
		key.flags = KEY_UP;
		key.keyCode = iCadeToKeyMap[button];
		key.deviceId = DEVICE_ID_PAD_0;
		NativeKey(key);
	}
	
}

- (void)controllerDidConnect:(NSNotification *)note
{
    if (self.gameController != nil) return; // already have a connected controller
    
    [self setupController:(GCController *)note.object];
    
    [[UIApplication sharedApplication] setIdleTimerDisabled:YES];   // prevent auto-lock
}

- (void)controllerDidDisconnect:(NSNotification *)note
{
    if (self.gameController == note.object) {
        self.gameController = nil;
        
        if ([[GCController controllers] count] > 0) {
            [self setupController:[[GCController controllers] firstObject]];
        } else {
            [[UIApplication sharedApplication] setIdleTimerDisabled:NO];
        }
    }
}

- (void)controllerButtonPressed:(BOOL)pressed keyCode:(keycode_t)keyCode
{
    KeyInput key;
    key.deviceId = DEVICE_ID_PAD_0;
    key.flags = pressed ? KEY_DOWN : KEY_UP;
    key.keyCode = keyCode;
    NativeKey(key);
}

- (void)setupController:(GCController *)controller
{
    self.gameController = controller;
    
    GCGamepad *baseProfile = self.gameController.gamepad;
    if (baseProfile == nil) {
        self.gameController = nil;
        return;
    }
    
    self.gameController.controllerPausedHandler = ^(GCController *controller) {
        KeyInput key;
        key.flags = KEY_DOWN;
        key.keyCode = NKCODE_ESCAPE;
        key.deviceId = DEVICE_ID_KEYBOARD;
        NativeKey(key);
    };
    
    baseProfile.buttonA.valueChangedHandler = ^(GCControllerButtonInput *button, float value, BOOL pressed) {
        [self controllerButtonPressed:pressed keyCode:NKCODE_BUTTON_2]; // Cross
    };
    
    baseProfile.buttonB.valueChangedHandler = ^(GCControllerButtonInput *button, float value, BOOL pressed) {
        [self controllerButtonPressed:pressed keyCode:NKCODE_BUTTON_3]; // Circle
    };
    
    baseProfile.buttonX.valueChangedHandler = ^(GCControllerButtonInput *button, float value, BOOL pressed) {
        [self controllerButtonPressed:pressed keyCode:NKCODE_BUTTON_4]; // Square
    };
    
    baseProfile.buttonY.valueChangedHandler = ^(GCControllerButtonInput *button, float value, BOOL pressed) {
        [self controllerButtonPressed:pressed keyCode:NKCODE_BUTTON_1]; // Triangle
    };
    
    baseProfile.leftShoulder.valueChangedHandler = ^(GCControllerButtonInput *button, float value, BOOL pressed) {
        [self controllerButtonPressed:pressed keyCode:NKCODE_BUTTON_7]; // LTrigger
    };
    
    baseProfile.rightShoulder.valueChangedHandler = ^(GCControllerButtonInput *button, float value, BOOL pressed) {
        [self controllerButtonPressed:pressed keyCode:NKCODE_BUTTON_8]; // RTrigger
    };
    
    baseProfile.dpad.up.valueChangedHandler = ^(GCControllerButtonInput *button, float value, BOOL pressed) {
        [self controllerButtonPressed:pressed keyCode:NKCODE_DPAD_UP];
    };
    
    baseProfile.dpad.down.valueChangedHandler = ^(GCControllerButtonInput *button, float value, BOOL pressed) {
        [self controllerButtonPressed:pressed keyCode:NKCODE_DPAD_DOWN];
    };
    
    baseProfile.dpad.left.valueChangedHandler = ^(GCControllerButtonInput *button, float value, BOOL pressed) {
        [self controllerButtonPressed:pressed keyCode:NKCODE_DPAD_LEFT];
    };
    
    baseProfile.dpad.right.valueChangedHandler = ^(GCControllerButtonInput *button, float value, BOOL pressed) {
        [self controllerButtonPressed:pressed keyCode:NKCODE_DPAD_RIGHT];
    };
    
    GCExtendedGamepad *extendedProfile = self.gameController.extendedGamepad;
    if (extendedProfile == nil)
        return; // controller doesn't support extendedGamepad profile
    
    extendedProfile.leftTrigger.valueChangedHandler = ^(GCControllerButtonInput *button, float value, BOOL pressed) {
        [self controllerButtonPressed:pressed keyCode:NKCODE_BUTTON_9]; // Select
    };
    
    extendedProfile.rightTrigger.valueChangedHandler = ^(GCControllerButtonInput *button, float value, BOOL pressed) {
        [self controllerButtonPressed:pressed keyCode:NKCODE_BUTTON_10]; // Start
    };
    
    extendedProfile.leftThumbstick.xAxis.valueChangedHandler = ^(GCControllerAxisInput *axis, float value) {
        AxisInput axisInput;
        axisInput.deviceId = DEVICE_ID_PAD_0;
        axisInput.flags = 0;
        axisInput.axisId = JOYSTICK_AXIS_X;
        axisInput.value = value;
        NativeAxis(axisInput);
    };
    
    extendedProfile.leftThumbstick.yAxis.valueChangedHandler = ^(GCControllerAxisInput *axis, float value) {
        AxisInput axisInput;
        axisInput.deviceId = DEVICE_ID_PAD_0;
        axisInput.flags = 0;
        axisInput.axisId = JOYSTICK_AXIS_Y;
        axisInput.value = -value;
        NativeAxis(axisInput);
    };
    
    // Map right thumbstick as 4 extra buttons
    extendedProfile.rightThumbstick.up.valueChangedHandler = ^(GCControllerButtonInput *button, float value, BOOL pressed) {
        [self controllerButtonPressed:(value > 0.5) keyCode:NKCODE_BUTTON_5];
    };
    
    extendedProfile.rightThumbstick.down.valueChangedHandler = ^(GCControllerButtonInput *button, float value, BOOL pressed) {
        [self controllerButtonPressed:(value < -0.5) keyCode:NKCODE_BUTTON_6];
    };
    
    extendedProfile.rightThumbstick.left.valueChangedHandler = ^(GCControllerButtonInput *button, float value, BOOL pressed) {
        [self controllerButtonPressed:(value < -0.5) keyCode:NKCODE_BUTTON_11];
    };
    
    extendedProfile.rightThumbstick.right.valueChangedHandler = ^(GCControllerButtonInput *button, float value, BOOL pressed) {
        [self controllerButtonPressed:(value > 0.5) keyCode:NKCODE_BUTTON_12];
    };
}

@end
