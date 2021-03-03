#include "ppsspp_config.h"
#if PPSSPP_PLATFORM(MAC)
#import <Cocoa/Cocoa.h>
#else
#import <UIKit/UIKit.h>
#endif
#import <QuartzCore/CAMetalLayer.h>

#include "SDLCocoaMetalLayer.h"

void *makeWindowMetalCompatible(void *window) {
	// https://github.com/KhronosGroup/MoltenVK/issues/78#issuecomment-371118536
#if PPSSPP_PLATFORM(MAC)
	NSView *view = ((NSWindow *)window).contentView;
	assert([view isKindOfClass:[NSView class]]);

	if (![view.layer isKindOfClass:[CAMetalLayer class]])
	{
		[view setLayer:[CAMetalLayer layer]];
	}
	return view.layer;
#else
	UIView *view = (UIView *)window;
	assert([view isKindOfClass:[UIView class]]);

	CAMetalLayer *metalLayer = [CAMetalLayer new];

	CGSize viewSize = view.frame.size;
	metalLayer.frame = view.frame;
	metalLayer.opaque = true;
	metalLayer.framebufferOnly = true;
	metalLayer.drawableSize = viewSize;
	metalLayer.pixelFormat = (MTLPixelFormat)80;//BGRA8Unorm==80
	[view.layer addSublayer:metalLayer];
	return metalLayer;
#endif
}
