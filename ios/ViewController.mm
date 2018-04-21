//
// ViewController.m
//
// Created by rock88
// Modified by xSacha
//

#import "ViewController.h"
#import "SubtleVolume.h"
#import <GLKit/GLKit.h>
#include <cassert>

#include "base/display.h"
#include "base/timeutil.h"
#include "file/zip_read.h"
#include "input/input_state.h"
#include "net/resolve.h"
#include "ui/screen.h"
#include "thin3d/thin3d.h"
#include "thin3d/thin3d_create.h"
#include "thin3d/GLRenderManager.h"
#include "input/keycodes.h"
#include "gfx_es2/gpu_features.h"

#include "Core/Config.h"
#include "Core/System.h"
#include "Common/GraphicsContext.h"

#include <sys/types.h>
#include <sys/sysctl.h>
#include <mach/machine.h>

#define IS_IPAD() ([UIDevice currentDevice].userInterfaceIdiom == UIUserInterfaceIdiomPad)
#define IS_IPHONE() ([UIDevice currentDevice].userInterfaceIdiom == UIUserInterfaceIdiomPhone)

class IOSGraphicsContext : public DummyGraphicsContext {
public:
	IOSGraphicsContext() {
		CheckGLExtensions();
		draw_ = Draw::T3DCreateGLContext();
		renderManager_ = (GLRenderManager *)draw_->GetNativeObject(Draw::NativeObject::RENDER_MANAGER);
		SetGPUBackend(GPUBackend::OPENGL);
		bool success = draw_->CreatePresets();
		assert(success);
	}
	~IOSGraphicsContext() {
		delete draw_;
	}
	Draw::DrawContext *GetDrawContext() override {
		return draw_;
	}
	void ThreadStart() override {
		renderManager_->ThreadStart();
	}

	bool ThreadFrame() override {
		return renderManager_->ThreadFrame();
	}

	void ThreadEnd() override {
		renderManager_->ThreadEnd();
	}

	void StopThread() override {
		renderManager_->WaitUntilQueueIdle();
		renderManager_->StopThread();
	}
private:
	Draw::DrawContext *draw_;
	GLRenderManager *renderManager_;
};

static float dp_xscale = 1.0f;
static float dp_yscale = 1.0f;

static double lastSelectPress = 0.0f;
static double lastStartPress = 0.0f;
static bool simulateAnalog = false;
static bool threadEnabled = true;
static bool threadStopped = false;

__unsafe_unretained ViewController* sharedViewController;
static GraphicsContext *graphicsContext;

@interface ViewController () {
	std::map<uint16_t, uint16_t> iCadeToKeyMap;
}

@property (nonatomic, strong) EAGLContext* context;
@property (nonatomic, strong) NSMutableArray<NSDictionary *>* touches;
//@property (nonatomic) iCadeReaderView* iCadeView;
#if __IPHONE_OS_VERSION_MAX_ALLOWED > __IPHONE_6_1
@property (nonatomic) GCController *gameController __attribute__((weak_import));
#endif

@end

@interface ViewController () <SubtleVolumeDelegate> {
	SubtleVolume *volume;
}
@end


@implementation ViewController

-(id) init {
	self = [super init];
	if (self) {
		sharedViewController = self;
		self.touches = [NSMutableArray array];

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

		[[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(appWillTerminate:) name:UIApplicationWillTerminateNotification object:nil];

#if __IPHONE_OS_VERSION_MAX_ALLOWED > __IPHONE_6_1
		if ([GCController class]) // Checking the availability of a GameController framework
		{
			[[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(controllerDidConnect:) name:GCControllerDidConnectNotification object:nil];
			[[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(controllerDidDisconnect:) name:GCControllerDidDisconnectNotification object:nil];
		}
#endif
	}
	return self;
}

- (void)subtleVolume:(SubtleVolume *)volumeView willChange:(CGFloat)value {
}
- (void)subtleVolume:(SubtleVolume *)volumeView didChange:(CGFloat)value {
}

- (void)viewDidLoad {
	[super viewDidLoad];

	self.view.frame = [[UIScreen mainScreen] bounds];
	self.view.multipleTouchEnabled = YES;
	self.context = [[EAGLContext alloc] initWithAPI:kEAGLRenderingAPIOpenGLES3];
	
	if (!self.context) {
		self.context = [[EAGLContext alloc] initWithAPI:kEAGLRenderingAPIOpenGLES2];
	}

	GLKView* view = (GLKView *)self.view;
	view.context = self.context;
	view.drawableDepthFormat = GLKViewDrawableDepthFormat24;
	[EAGLContext setCurrentContext:self.context];
	self.preferredFramesPerSecond = 60;

	// Might be useful for a speed boot, sacrificing resolution:
	// view.contentScaleFactor = 1.0;

	float scale = [UIScreen mainScreen].scale;
	
	if ([[UIScreen mainScreen] respondsToSelector:@selector(nativeScale)]) {
		scale = [UIScreen mainScreen].nativeScale;
	}

	CGSize size = [[UIApplication sharedApplication].delegate window].frame.size;

	if (size.height > size.width) {
		float h = size.height;
		size.height = size.width;
		size.width = h;
	}

	g_dpi = (IS_IPAD() ? 200.0f : 150.0f) * scale;
	g_dpi_scale_x = 240.0f / g_dpi;
	g_dpi_scale_y = 240.0f / g_dpi;
	g_dpi_scale_real_x = g_dpi_scale_x;
	g_dpi_scale_real_y = g_dpi_scale_y;
	pixel_xres = size.width * scale;
	pixel_yres = size.height * scale;

	dp_xres = pixel_xres * g_dpi_scale_x;
	dp_yres = pixel_yres * g_dpi_scale_y;

	pixel_in_dps_x = (float)pixel_xres / (float)dp_xres;
	pixel_in_dps_y = (float)pixel_yres / (float)dp_yres;

	graphicsContext = new IOSGraphicsContext();
	
	graphicsContext->ThreadStart();

	dp_xscale = (float)dp_xres / (float)pixel_xres;
	dp_yscale = (float)dp_yres / (float)pixel_yres;
	
	/*self.iCadeView = [[iCadeReaderView alloc] init];
	[self.view addSubview:self.iCadeView];
	self.iCadeView.delegate = self;
	self.iCadeView.active = YES;*/
	
#if __IPHONE_OS_VERSION_MAX_ALLOWED > __IPHONE_6_1
	if ([GCController class]) {
		if ([[GCController controllers] count] > 0) {
			[self setupController:[[GCController controllers] firstObject]];
		}
	}
#endif
	
	CGFloat margin = 0;
	CGFloat height = 16;
	volume = [[SubtleVolume alloc]
			  initWithStyle:SubtleVolumeStylePlain
			  frame:CGRectMake(
							   margin,   // X
							   0,        // Y
							   self.view.frame.size.width-(margin*2), // width
							   height    // height
							)];
	
	volume.padding = 7;
	volume.barTintColor = [UIColor blackColor];
	volume.barBackgroundColor = [UIColor whiteColor];
	volume.animation = SubtleVolumeAnimationSlideDown;
	volume.delegate = self;
	[self.view addSubview:volume];
	[self.view bringSubviewToFront:volume];
	
	dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_HIGH, 0), ^{
		NativeInitGraphics(graphicsContext);

		ILOG("Emulation thread starting\n");
		while (threadEnabled) {
			NativeUpdate();
			NativeRender(graphicsContext);
			time_update();
		}

		threadStopped = true;

		ILOG("Emulation thread shutting down\n");
		NativeShutdownGraphics();

		// Also ask the main thread to stop, so it doesn't hang waiting for a new frame.
		ILOG("Emulation thread stopping\n");
		graphicsContext->StopThread();
	});
}

- (void)didReceiveMemoryWarning
{
	[super didReceiveMemoryWarning];
}

- (void)appWillTerminate:(NSNotification *)notification
{
	[self shutdown];
}

- (void)shutdown
{
	if (sharedViewController == nil) {
		return;
	}
	
	if(volume) {
		[volume removeFromSuperview];
		volume = nil;
	}

	Audio_Shutdown();

	if (threadEnabled) {
		threadEnabled = false;
		while (graphicsContext->ThreadFrame()) {
			continue;
		}
		while (!threadStopped) {}
		graphicsContext->ThreadEnd();
	}

	sharedViewController = nil;

	if (self.context) {
		if ([EAGLContext currentContext] == self.context) {
			[EAGLContext setCurrentContext:nil];
		}
		self.context = nil;
	}

	[[NSNotificationCenter defaultCenter] removeObserver:self];

#if __IPHONE_OS_VERSION_MAX_ALLOWED > __IPHONE_6_1
	if ([GCController class]) {
		self.gameController = nil;
	}
#endif

	if (graphicsContext) {
		graphicsContext->Shutdown();
		delete graphicsContext;
		graphicsContext = NULL;
	}

	NativeShutdown();
}

- (void)dealloc
{
	[self shutdown];
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
	if (sharedViewController)
		graphicsContext->ThreadFrame();
}

- (void)touchX:(float)x y:(float)y code:(int)code pointerId:(int)pointerId
{
	float scale = [UIScreen mainScreen].scale;
	
	if ([[UIScreen mainScreen] respondsToSelector:@selector(nativeScale)]) {
		scale = [UIScreen mainScreen].nativeScale;
	}

	float scaledX = (int)(x * dp_xscale) * scale;
	float scaledY = (int)(y * dp_yscale) * scale;

	TouchInput input;
	input.x = scaledX;
	input.y = scaledY;
	switch (code) {
		case 1 :
			input.flags = TOUCH_DOWN;
			break;

		case 2 :
			input.flags = TOUCH_UP;
			break;

		default :
			input.flags = TOUCH_MOVE;
			break;
	}
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

- (void)touchesBegan:(NSSet *)touches withEvent:(UIEvent *)event
{
	for(UITouch* touch in touches)
	{
		NSDictionary* dict = @{@"touch":touch,@"index":@([self freeTouchIndex])};
		[self.touches addObject:dict];
		CGPoint point = [touch locationInView:self.view];
		[self touchX:point.x y:point.y code:1 pointerId:[[dict objectForKey:@"index"] intValue]];
	}
}

- (void)touchesMoved:(NSSet *)touches withEvent:(UIEvent *)event
{
	for(UITouch* touch in touches)
	{
		CGPoint point = [touch locationInView:self.view];
		NSDictionary* dict = [self touchDictBy:touch];
		[self touchX:point.x y:point.y code:0 pointerId:[[dict objectForKey:@"index"] intValue]];
	}
}

- (void)touchesEnded:(NSSet *)touches withEvent:(UIEvent *)event
{
	for(UITouch* touch in touches)
	{
		CGPoint point = [touch locationInView:self.view];
		NSDictionary* dict = [self touchDictBy:touch];
		[self touchX:point.x y:point.y code:2 pointerId:[[dict objectForKey:@"index"] intValue]];
		[self.touches removeObject:dict];
	}
}

- (void)touchesCancelled:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event
{
	for(UITouch* touch in touches)
	{
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

#if __IPHONE_OS_VERSION_MAX_ALLOWED > __IPHONE_6_1
- (void)controllerDidConnect:(NSNotification *)note
{
	if (![[GCController controllers] containsObject:self.gameController]) self.gameController = nil;
	
	if (self.gameController != nil) return; // already have a connected controller
	
	[self setupController:(GCController *)note.object];
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
	
	[[UIApplication sharedApplication] setIdleTimerDisabled:YES];   // prevent auto-lock
	
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
		axisInput.value = value * g_Config.fXInputAnalogSensitivity;
		NativeAxis(axisInput);
	};
	
	extendedProfile.leftThumbstick.yAxis.valueChangedHandler = ^(GCControllerAxisInput *axis, float value) {
		AxisInput axisInput;
		axisInput.deviceId = DEVICE_ID_PAD_0;
		axisInput.flags = 0;
		axisInput.axisId = JOYSTICK_AXIS_Y;
		axisInput.value = -value * g_Config.fXInputAnalogSensitivity;
		NativeAxis(axisInput);
	};
	
	// Map right thumbstick as another analog stick, particularly useful for controllers like the DualShock 3/4 when connected to an iOS device
	extendedProfile.rightThumbstick.xAxis.valueChangedHandler = ^(GCControllerAxisInput *axis, float value) {
		AxisInput axisInput;
		axisInput.deviceId = DEVICE_ID_PAD_0;
		axisInput.flags = 0;
		axisInput.axisId = JOYSTICK_AXIS_Z;
		axisInput.value = value * g_Config.fXInputAnalogSensitivity;
		NativeAxis(axisInput);
	};
	
	extendedProfile.rightThumbstick.yAxis.valueChangedHandler = ^(GCControllerAxisInput *axis, float value) {
		AxisInput axisInput;
		axisInput.deviceId = DEVICE_ID_PAD_0;
		axisInput.flags = 0;
		axisInput.axisId = JOYSTICK_AXIS_RZ;
		axisInput.value = -value * g_Config.fXInputAnalogSensitivity;
		NativeAxis(axisInput);
	};
}
#endif

@end

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
