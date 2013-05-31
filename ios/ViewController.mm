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

#include "Core/Config.h"
#include "gfx_es2/fbo.h"

#define IS_IPAD() ([UIDevice currentDevice].userInterfaceIdiom == UIUserInterfaceIdiomPad)

extern void UIUpdateMouse(int i, float x, float y, bool down);

float dp_xscale = 1.0f;
float dp_yscale = 1.0f;

static uint32_t pad_buttons_async_set = 0;
static uint32_t pad_buttons_async_clear = 0;
static uint32_t pad_buttons_down = 0;

double lastSelectPress = 0.0f;
bool simulateAnalog = false;

extern ScreenManager *screenManager;
InputState input_state;

extern std::string ram_temp_file;
extern bool isJailed;

ViewController* sharedViewController;

@interface ViewController ()

@property (strong, nonatomic) EAGLContext *context;
@property (nonatomic,retain) NSString* documentsPath;
@property (nonatomic,retain) NSString* bundlePath;
@property (nonatomic,retain) NSMutableArray* touches;
@property (nonatomic,retain) AudioEngine* audioEngine;
@property (nonatomic,retain) iCadeReaderView *iCadeView;

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
		pad_buttons_async_clear = 0;
		pad_buttons_async_set = 0;
		pad_buttons_down = 0;

		net::Init();

		ram_temp_file = [[NSTemporaryDirectory() stringByAppendingPathComponent:@"ram_tmp.file"] fileSystemRepresentation];
		NativeInit(0, NULL, [self.documentsPath UTF8String], [self.bundlePath UTF8String], NULL);
		
		isJailed = true;
		
		NSArray *jailPath = [NSArray arrayWithObjects:
							@"/Applications/Cydia.app",
							@"/private/var/lib/apt" ,
							@"/private/var/stash" ,
							@"/usr/sbin/sshd" ,
							@"/usr/bin/sshd" , nil];
		
		for(NSString *string in jailPath)
		{
			if ([[NSFileManager defaultManager] fileExistsAtPath:string])
				isJailed = false;
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
    
    if (g_Config.bEnableSound)
		self.audioEngine = [[[AudioEngine alloc] init] autorelease];
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

//static BOOL menuDown = NO;

- (void)glkView:(GLKView *)view drawInRect:(CGRect)rect
{
	{
		lock_guard guard(input_state.lock);
		pad_buttons_down |= pad_buttons_async_set;
		pad_buttons_down &= ~pad_buttons_async_clear;
        
        float analogX = 0;
        float analogY = 0;
        
        if (simulateAnalog) {
            if (pad_buttons_down & PAD_BUTTON_UP) {
                analogY = 1.0f;
                pad_buttons_down &= ~PAD_BUTTON_UP;
            }
            if (pad_buttons_down & PAD_BUTTON_DOWN) {
                analogY = -1.0f;
                pad_buttons_down &= ~PAD_BUTTON_DOWN;
            }
            if (pad_buttons_down & PAD_BUTTON_LEFT) {
                analogX = -1.0f;
                pad_buttons_down &= ~PAD_BUTTON_LEFT;
            }
            if (pad_buttons_down & PAD_BUTTON_RIGHT) {
                analogX = 1.0f;
                pad_buttons_down &= ~PAD_BUTTON_RIGHT;
            }
        }
        
        input_state.pad_lstick_x = analogX;
        input_state.pad_lstick_y = analogY;
		input_state.pad_rstick_x = 0;
		input_state.pad_rstick_y = 0;
		input_state.pad_buttons = pad_buttons_down;
		UpdateInputState(&input_state);
	}

	{
		lock_guard guard(input_state.lock);
		UIUpdateMouse(0, input_state.pointer_x[0], input_state.pointer_y[0], input_state.pointer_down[0]);
		screenManager->update(input_state);
	}

	{
		lock_guard guard(input_state.lock);
		EndInputState(&input_state);
	}

	NativeRender();
	time_update();
}

- (void)swipeGesture:(id)sender
{
	// TODO: Use a swipe gesture to handle BACK
/*
	pad_buttons_async_set |= PAD_BUTTON_MENU;
	pad_buttons_async_clear &= PAD_BUTTON_MENU;

	int64_t delayInSeconds = 1.5;
	dispatch_time_t popTime = dispatch_time(DISPATCH_TIME_NOW, delayInSeconds * NSEC_PER_SEC);
	dispatch_after(popTime, dispatch_get_main_queue(), ^(void){
		pad_buttons_async_set &= PAD_BUTTON_MENU;
		pad_buttons_async_clear |= PAD_BUTTON_MENU;
	});

	if (g_Config.bBufferedRendering)
		fbo_unbind();

	screenManager->push(new InGameMenuScreen());
*/
}

- (void)touchX:(float)x y:(float)y code:(int)code pointerId:(int)pointerId
{
	lock_guard guard(input_state.lock);
	
	float scale = [UIScreen mainScreen].scale;
	
	float scaledX = (int)(x * dp_xscale) * scale;
	float scaledY = (int)(y * dp_yscale) * scale;
	
	input_state.pointer_x[pointerId] = scaledX;
	input_state.pointer_y[pointerId] = scaledY;
	if (code == 1) {
		input_state.pointer_down[pointerId] = true;
	} else if (code == 2) {
		input_state.pointer_down[pointerId] = false;
	}
	input_state.mouse_valid = true;
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
    switch (button) {
        case iCadeJoystickUp :
            pad_buttons_async_set |= PAD_BUTTON_UP;
            pad_buttons_async_clear &= ~PAD_BUTTON_UP;
            break;
            
        case iCadeJoystickRight :
            pad_buttons_async_set |= PAD_BUTTON_RIGHT;
            pad_buttons_async_clear &= ~PAD_BUTTON_RIGHT;
            break;
            
        case iCadeJoystickDown :
            pad_buttons_async_set |= PAD_BUTTON_DOWN;
            pad_buttons_async_clear &= ~PAD_BUTTON_DOWN;
            break;
            
        case iCadeJoystickLeft :
            pad_buttons_async_set |= PAD_BUTTON_LEFT;
            pad_buttons_async_clear &= ~PAD_BUTTON_LEFT;
            break;
            
        case iCadeButtonA :
            pad_buttons_async_set |= PAD_BUTTON_SELECT;
            pad_buttons_async_clear &= ~PAD_BUTTON_SELECT;
            break;
            
        case iCadeButtonB :
            pad_buttons_async_set |= PAD_BUTTON_LBUMPER;
            pad_buttons_async_clear &= ~PAD_BUTTON_LBUMPER;
            break;
            
        case iCadeButtonC :
            pad_buttons_async_set |= PAD_BUTTON_START;
            pad_buttons_async_clear &= ~PAD_BUTTON_START;
            break;
            
        case iCadeButtonD :
            pad_buttons_async_set |= PAD_BUTTON_RBUMPER;
            pad_buttons_async_clear &= ~PAD_BUTTON_RBUMPER;
            break;
            
        case iCadeButtonE :
            pad_buttons_async_set |= PAD_BUTTON_X;
            pad_buttons_async_clear &= ~PAD_BUTTON_X;
            break;
            
        case iCadeButtonF :
            pad_buttons_async_set |= PAD_BUTTON_A;
            pad_buttons_async_clear &= ~PAD_BUTTON_A;
            break;
            
        case iCadeButtonG :
            pad_buttons_async_set |= PAD_BUTTON_Y;
            pad_buttons_async_clear &= ~PAD_BUTTON_Y;
            break;
            
        case iCadeButtonH :
            pad_buttons_async_set |= PAD_BUTTON_B;
            pad_buttons_async_clear &= ~PAD_BUTTON_B;
            break;
            
        default :
            break;
    }
}

- (void)buttonUp:(iCadeState)button
{
    switch (button) {
        case iCadeJoystickUp :
            pad_buttons_async_set &= ~PAD_BUTTON_UP;
            pad_buttons_async_clear |= PAD_BUTTON_UP;
            break;
            
        case iCadeJoystickRight :
            pad_buttons_async_set &= ~PAD_BUTTON_RIGHT;
            pad_buttons_async_clear |= PAD_BUTTON_RIGHT;
            break;
            
        case iCadeJoystickDown :
            pad_buttons_async_set &= ~PAD_BUTTON_DOWN;
            pad_buttons_async_clear |= PAD_BUTTON_DOWN;
            break;
            
        case iCadeJoystickLeft :
            pad_buttons_async_set &= ~PAD_BUTTON_LEFT;
            pad_buttons_async_clear |= PAD_BUTTON_LEFT;
            break;
            
        case iCadeButtonA :
            pad_buttons_async_set &= ~PAD_BUTTON_SELECT;
            pad_buttons_async_clear |= PAD_BUTTON_SELECT;
            
            if ((lastSelectPress + 1.0f) > time_now_d())
                simulateAnalog = !simulateAnalog;
            lastSelectPress = time_now_d();
            break;
            
        case iCadeButtonB :
            pad_buttons_async_set &= ~PAD_BUTTON_LBUMPER;
            pad_buttons_async_clear |= PAD_BUTTON_LBUMPER;
            break;
            
        case iCadeButtonC :
            pad_buttons_async_set &= ~PAD_BUTTON_START;
            pad_buttons_async_clear |= PAD_BUTTON_START;
            break;
            
        case iCadeButtonD :
            pad_buttons_async_set &= ~PAD_BUTTON_RBUMPER;
            pad_buttons_async_clear |= PAD_BUTTON_RBUMPER;
            break;
            
        case iCadeButtonE :
            pad_buttons_async_set &= ~PAD_BUTTON_X;
            pad_buttons_async_clear |= PAD_BUTTON_X;
            break;
            
        case iCadeButtonF :
            pad_buttons_async_set &= ~PAD_BUTTON_A;
            pad_buttons_async_clear |= PAD_BUTTON_A;
            break;
            
        case iCadeButtonG :
            pad_buttons_async_set &= ~PAD_BUTTON_Y;
            pad_buttons_async_clear |= PAD_BUTTON_Y;
            break;
            
        case iCadeButtonH :
            pad_buttons_async_set &= ~PAD_BUTTON_B;
            pad_buttons_async_clear |= PAD_BUTTON_B;
            break;
            
        default :
            break;
    }
}

@end
