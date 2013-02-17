//
// ViewController.m
//
// Created by rock88
// Modified by xSacha
//

#import "ViewController.h"
#import <GLKit/GLKit.h>

#include "base/display.h"
#include "base/timeutil.h"
#include "file/zip_read.h"
#include "input/input_state.h"
#include "net/resolve.h"
#include "ui_atlas.h"
#include "ui/screen.h"

#include "Config.h"
#include "gfx_es2/fbo.h"

#define IS_IPAD() ([UIDevice currentDevice].userInterfaceIdiom == UIUserInterfaceIdiomPad)

extern void UIUpdateMouse(int i, float x, float y, bool down);

float dp_xscale = 1.0f;
float dp_yscale = 1.0f;

static uint32_t pad_buttons_async_set = 0;
static uint32_t pad_buttons_async_clear = 0;

extern ScreenManager *screenManager;
InputState input_state;

extern std::string ram_temp_file;

@interface ViewController ()

@property (strong, nonatomic) EAGLContext *context;
@property (nonatomic,retain) NSString* documentsPath;
@property (nonatomic,retain) NSString* bundlePath;
@property (nonatomic,retain) NSMutableArray* touches;

@end

@implementation ViewController
@synthesize documentsPath,bundlePath,touches;

- (id)init
{
	self = [super init];
	if (self) {
		self.touches = [[[NSMutableArray alloc] init] autorelease];

		self.documentsPath = [NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES) objectAtIndex:0];
		self.bundlePath = [[[NSBundle mainBundle] resourcePath] stringByAppendingString:@"/"];

		memset(&input_state, 0, sizeof(input_state));

		net::Init();

		ram_temp_file = [[NSTemporaryDirectory() stringByAppendingPathComponent:@"ram_tmp.file"] fileSystemRepresentation];
		NativeInit(0, NULL, [self.bundlePath UTF8String], [self.documentsPath UTF8String], NULL);
		
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

	self.touches = nil;
	self.documentsPath = nil;
	self.bundlePath = nil;

	NativeShutdown();
	[super dealloc];
}

- (BOOL)shouldAutorotateToInterfaceOrientation:(UIInterfaceOrientation)toInterfaceOrientation
{
	return YES;
}

//static BOOL menuDown = NO;

- (void)glkView:(GLKView *)view drawInRect:(CGRect)rect
{
	lock_guard guard(input_state.lock);
	input_state.pad_buttons |= pad_buttons_async_set;
	input_state.pad_buttons &= ~pad_buttons_async_clear;
	UpdateInputState(&input_state);

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

- (void)touchesBegan:(NSSet *)_touches withEvent:(UIEvent *)event
{
	for(UITouch* touch in _touches) {
		[self.touches addObject:touch];
		CGPoint point = [touch locationInView:self.view];
		[self touchX:point.x y:point.y code:1 pointerId:[self.touches indexOfObject:touch]];
	}
}

- (void)touchesMoved:(NSSet *)_touches withEvent:(UIEvent *)event
{
	for(UITouch* touch in _touches) {
		CGPoint point = [touch locationInView:self.view];
		[self touchX:point.x y:point.y code:0 pointerId:[self.touches indexOfObject:touch]];
	}
}

- (void)touchesEnded:(NSSet *)_touches withEvent:(UIEvent *)event
{
	for(UITouch* touch in _touches) {
		CGPoint point = [touch locationInView:self.view];
		[self touchX:point.x y:point.y code:2 pointerId:[self.touches indexOfObject:touch]];
		[self.touches removeObject:touch];
	}
}

void LaunchBrowser(char const* url)
{
	[[UIApplication sharedApplication] openURL:[NSURL URLWithString:[NSString stringWithCString:url encoding:NSStringEncodingConversionAllowLossy]]];
}

void EnableFZ(){};
void DisableFZ(){};

@end
