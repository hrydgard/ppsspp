#import <Cocoa/Cocoa.h>
#import <QuartzCore/CAMetalLayer.h>

#include "base/logging.h"
#include "SDLCocoaMetalLayer.h"

void *makeWindowMetalCompatible(void *window) {
	NSView *view = ((NSWindow *)window).contentView;
	assert([view isKindOfClass:[NSView class]]);

	if (![view.layer isKindOfClass:[CAMetalLayer class]])
	{
		[view setLayer:[CAMetalLayer layer]];
	}
	return view.layer;
}
