#import "ios/AccessibilityBridge.h"

#include <vector>

#include "Common/Input/InputState.h"
#include "Common/System/Display.h"
#include "Common/System/NativeApp.h"
#include "Common/UI/Accessibility.h"
#include "Core/System.h"

@class PPSSPPAccessibilityBridge;

typedef NS_ENUM(NSInteger, PPSSPPAccessibilityAction) {
	PPSSPPAccessibilityActionActivateUI,
	PPSSPPAccessibilityActionDPad,
	PPSSPPAccessibilityActionLeftStick,
	PPSSPPAccessibilityActionRightStick,
	PPSSPPAccessibilityActionFaceButtons,
	PPSSPPAccessibilityActionShoulders,
	PPSSPPAccessibilityActionSelect,
	PPSSPPAccessibilityActionEmulatorMenu,
};

@interface PPSSPPAccessibilityElement : UIAccessibilityElement
@property(nonatomic, weak) PPSSPPAccessibilityBridge *bridge;
@property(nonatomic) PPSSPPAccessibilityAction action;
@property(nonatomic) CGRect dpFrame;
@end

@interface PPSSPPAccessibilityBridge () {
	__weak UIView *_view;
	NSMutableArray *_elements;
	NSTimer *_refreshTimer;
	NSString *_lastSignature;
	InputKeyCode _lastShoulderKey;
	InputKeyCode _heldShoulderKey;
	BOOL _refreshQueued;
}
- (BOOL)activateElement:(PPSSPPAccessibilityElement *)element;
- (BOOL)scrollElement:(PPSSPPAccessibilityElement *)element direction:(UIAccessibilityScrollDirection)direction;
- (void)adjustElement:(PPSSPPAccessibilityElement *)element increment:(BOOL)increment;
@end

@implementation PPSSPPAccessibilityElement

- (BOOL)accessibilityActivate {
	return [self.bridge activateElement:self];
}

- (BOOL)accessibilityScroll:(UIAccessibilityScrollDirection)direction {
	return [self.bridge scrollElement:self direction:direction];
}

- (void)accessibilityIncrement {
	[self.bridge adjustElement:self increment:YES];
}

- (void)accessibilityDecrement {
	[self.bridge adjustElement:self increment:NO];
}

@end

static void SendKey(InputKeyCode keyCode, bool down, InputDeviceID deviceId = DEVICE_ID_TOUCH) {
	KeyInput key{};
	key.deviceId = deviceId;
	key.keyCode = keyCode;
	key.flags = down ? KeyInputFlags::DOWN : KeyInputFlags::UP;
	NativeKey(key);
}

static void TapKey(InputKeyCode keyCode, InputDeviceID deviceId = DEVICE_ID_TOUCH) {
	SendKey(keyCode, true, deviceId);
	dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(80 * NSEC_PER_MSEC)), dispatch_get_main_queue(), ^{
		SendKey(keyCode, false, deviceId);
	});
}

static void SendAxis(InputAxis axisId, float value) {
	AxisInput axis{};
	axis.deviceId = DEVICE_ID_PAD_0;
	axis.axisId = axisId;
	axis.value = value;
	NativeAxis(&axis, 1);
}

static void TapAxis(InputAxis axisId, float value) {
	SendAxis(axisId, value);
	dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(80 * NSEC_PER_MSEC)), dispatch_get_main_queue(), ^{
		SendAxis(axisId, 0.0f);
	});
}

static UIAccessibilityTraits TraitsForRole(UI::AccessibilityRole role) {
	switch (role) {
	case UI::AccessibilityRole::Button:
	case UI::AccessibilityRole::Choice:
	case UI::AccessibilityRole::GamepadControl:
		return UIAccessibilityTraitButton;
	case UI::AccessibilityRole::Checkbox:
		return UIAccessibilityTraitButton;
	case UI::AccessibilityRole::Slider:
		return UIAccessibilityTraitAdjustable;
	case UI::AccessibilityRole::TextField:
		return UIAccessibilityTraitNone;
	case UI::AccessibilityRole::Progress:
		return UIAccessibilityTraitUpdatesFrequently;
	case UI::AccessibilityRole::Heading:
		return UIAccessibilityTraitHeader;
	case UI::AccessibilityRole::StaticText:
	default:
		return UIAccessibilityTraitStaticText;
	}
}

@implementation PPSSPPAccessibilityBridge

- (instancetype)initWithView:(UIView *)view {
	self = [super init];
	if (self) {
		_view = view;
		_elements = [[NSMutableArray alloc] init];
		_lastShoulderKey = NKCODE_UNKNOWN;
		_heldShoulderKey = NKCODE_UNKNOWN;
		view.isAccessibilityElement = NO;
		view.accessibilityElements = _elements;
		[[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(voiceOverStatusChanged:) name:UIAccessibilityVoiceOverStatusDidChangeNotification object:nil];
		_refreshTimer = [NSTimer scheduledTimerWithTimeInterval:1.0 target:self selector:@selector(periodicRefresh:) userInfo:nil repeats:YES];
	}
	return self;
}

- (void)dealloc {
	[_refreshTimer invalidate];
	[[NSNotificationCenter defaultCenter] removeObserver:self];
	[self releaseHeldShoulder];
}

- (void)voiceOverStatusChanged:(NSNotification *)notification {
	[self scheduleRefresh];
}

- (void)periodicRefresh:(NSTimer *)timer {
	if (UIAccessibilityIsVoiceOverRunning()) {
		[self scheduleRefresh];
	}
}

- (void)scheduleRefresh {
	if (_refreshQueued) {
		return;
	}
	_refreshQueued = YES;
	dispatch_async(dispatch_get_main_queue(), ^{
		self->_refreshQueued = NO;
		[self refresh];
	});
}

- (CGRect)uiFrameFromDPBounds:(const Bounds &)bounds {
	UIView *view = _view;
	if (!view || g_display.dp_xres <= 0 || g_display.dp_yres <= 0) {
		return CGRectZero;
	}
	const CGFloat xScale = view.bounds.size.width / (CGFloat)g_display.dp_xres;
	const CGFloat yScale = view.bounds.size.height / (CGFloat)g_display.dp_yres;
	CGRect local = CGRectMake(bounds.x * xScale, bounds.y * yScale, bounds.w * xScale, bounds.h * yScale);
	return [view.window convertRect:local fromView:view];
}

- (PPSSPPAccessibilityElement *)makeElementWithLabel:(NSString *)label
											 frame:(CGRect)frame
										  dpFrame:(CGRect)dpFrame
										   action:(PPSSPPAccessibilityAction)action
										   traits:(UIAccessibilityTraits)traits {
	PPSSPPAccessibilityElement *element = [[PPSSPPAccessibilityElement alloc] initWithAccessibilityContainer:_view];
	element.bridge = self;
	element.action = action;
	element.accessibilityLabel = label;
	element.accessibilityFrame = frame;
	element.dpFrame = dpFrame;
	element.accessibilityTraits = traits;
	return element;
}

- (void)addInGameControls {
	UIView *view = _view;
	if (!view || g_display.dp_xres <= 0 || g_display.dp_yres <= 0) {
		return;
	}

	struct ControlArea {
		__unsafe_unretained NSString *label;
		PPSSPPAccessibilityAction action;
		CGRect dpFrame;
	};
	const CGFloat w = (CGFloat)g_display.dp_xres;
	const CGFloat h = (CGFloat)g_display.dp_yres;
	const CGFloat thirdW = w / 3.0f;
	const CGFloat halfH = h / 2.0f;
	const ControlArea controls[] = {
		{ @"D-pad", PPSSPPAccessibilityActionDPad, CGRectMake(0, halfH, thirdW, halfH) },
		{ @"Left stick", PPSSPPAccessibilityActionLeftStick, CGRectMake(0, 0, thirdW, halfH) },
		{ @"Right stick", PPSSPPAccessibilityActionRightStick, CGRectMake(thirdW * 2.0f, 0, thirdW, halfH) },
		{ @"Face buttons", PPSSPPAccessibilityActionFaceButtons, CGRectMake(thirdW * 2.0f, halfH, thirdW, halfH) },
		{ @"Shoulder buttons", PPSSPPAccessibilityActionShoulders, CGRectMake(thirdW, 0, thirdW, h * 0.25f) },
		{ @"Select", PPSSPPAccessibilityActionSelect, CGRectMake(thirdW, h * 0.72f, thirdW * 0.5f, h * 0.16f) },
		{ @"Emulator menu", PPSSPPAccessibilityActionEmulatorMenu, CGRectMake(thirdW * 1.5f, h * 0.72f, thirdW * 0.5f, h * 0.16f) },
	};

	for (const ControlArea &control : controls) {
		Bounds bounds(control.dpFrame.origin.x, control.dpFrame.origin.y, control.dpFrame.size.width, control.dpFrame.size.height);
		PPSSPPAccessibilityElement *element = [self makeElementWithLabel:control.label
																	frame:[self uiFrameFromDPBounds:bounds]
																  dpFrame:control.dpFrame
																   action:control.action
																   traits:UIAccessibilityTraitButton];
		if (control.action == PPSSPPAccessibilityActionDPad ||
			control.action == PPSSPPAccessibilityActionLeftStick ||
			control.action == PPSSPPAccessibilityActionRightStick ||
			control.action == PPSSPPAccessibilityActionFaceButtons ||
			control.action == PPSSPPAccessibilityActionShoulders) {
			element.accessibilityHint = @"Swipe up, down, left, or right.";
		}
		[_elements addObject:element];
	}
}

- (void)refresh {
	UIView *view = _view;
	if (!view) {
		return;
	}
	if (!UIAccessibilityIsVoiceOverRunning()) {
		[_elements removeAllObjects];
		_lastSignature = nil;
		view.accessibilityElements = _elements;
		return;
	}
	NSMutableArray *newElements = [[NSMutableArray alloc] init];
	NSMutableString *signature = [[NSMutableString alloc] init];
	NSMutableArray *oldElements = _elements;
	_elements = newElements;

	if (GetUIState() == UISTATE_INGAME) {
		[self addInGameControls];
		[signature appendString:@"ingame"];
		for (PPSSPPAccessibilityElement *element in _elements) {
			[signature appendFormat:@"|%@:%@", element.accessibilityLabel, NSStringFromCGRect(element.accessibilityFrame)];
		}
		if (![_lastSignature isEqualToString:signature]) {
			_lastSignature = [signature copy];
			view.accessibilityElements = _elements;
			UIAccessibilityPostNotification(UIAccessibilityLayoutChangedNotification, nil);
		} else {
			_elements = oldElements;
		}
		return;
	}

	[self releaseHeldShoulder];
	if (g_display.dp_xres > 0 && g_display.dp_yres > 0) {
		std::vector<UI::AccessibilityElementInfo> snapshot = UI::GetCachedAccessibilitySnapshot();
		for (const UI::AccessibilityElementInfo &info : snapshot) {
			NSString *label = [NSString stringWithUTF8String:info.label.c_str()];
			CGRect frame = [self uiFrameFromDPBounds:info.bounds];
			CGRect dpFrame = CGRectMake(info.bounds.x, info.bounds.y, info.bounds.w, info.bounds.h);
			UIAccessibilityTraits traits = TraitsForRole(info.role);
			if (!info.enabled) {
				traits |= UIAccessibilityTraitNotEnabled;
			}
			PPSSPPAccessibilityElement *element = [self makeElementWithLabel:label
																		frame:frame
																	  dpFrame:dpFrame
																	   action:PPSSPPAccessibilityActionActivateUI
																	   traits:traits];
			[_elements addObject:element];
			[signature appendFormat:@"|%@:%@:%llu", label, NSStringFromCGRect(frame), (unsigned long long)traits];
		}
	}
	if (![_lastSignature isEqualToString:signature]) {
		_lastSignature = [signature copy];
		view.accessibilityElements = _elements;
		UIAccessibilityPostNotification(UIAccessibilityLayoutChangedNotification, nil);
	} else {
		_elements = oldElements;
	}
}

- (void)releaseHeldShoulder {
	if (_heldShoulderKey != NKCODE_UNKNOWN) {
		SendKey(_heldShoulderKey, false, DEVICE_ID_PAD_0);
		_heldShoulderKey = NKCODE_UNKNOWN;
	}
}

- (void)reset {
	[self releaseHeldShoulder];
	_lastShoulderKey = NKCODE_UNKNOWN;
	[_elements removeAllObjects];
	_lastSignature = nil;
	if (_view) {
		_view.accessibilityElements = _elements;
	}
}

- (void)uiStateChanged {
	if (GetUIState() != UISTATE_INGAME) {
		[self releaseHeldShoulder];
	}
	[self scheduleRefresh];
}

- (void)willResignActive {
	[self releaseHeldShoulder];
}

- (BOOL)activateElement:(PPSSPPAccessibilityElement *)element {
	switch (element.action) {
	case PPSSPPAccessibilityActionActivateUI: {
		const CGFloat x = CGRectGetMidX(element.dpFrame);
		const CGFloat y = CGRectGetMidY(element.dpFrame);
		TouchInput down{};
		down.x = x;
		down.y = y;
		down.id = 9;
		down.flags = TouchInputFlags::DOWN;
		NativeTouch(down);
		TouchInput up = down;
		up.flags = TouchInputFlags::UP;
		NativeTouch(up);
		return YES;
	}
	case PPSSPPAccessibilityActionShoulders:
		if (_lastShoulderKey == NKCODE_UNKNOWN) {
			return NO;
		}
		if (_heldShoulderKey == _lastShoulderKey) {
			[self releaseHeldShoulder];
		} else {
			[self releaseHeldShoulder];
			SendKey(_lastShoulderKey, true, DEVICE_ID_PAD_0);
			_heldShoulderKey = _lastShoulderKey;
		}
		return YES;
	case PPSSPPAccessibilityActionSelect:
		TapKey(NKCODE_BUTTON_SELECT, DEVICE_ID_PAD_0);
		return YES;
	case PPSSPPAccessibilityActionEmulatorMenu:
		TapKey(NKCODE_BACK);
		return YES;
	default:
		return NO;
	}
}

- (void)adjustElement:(PPSSPPAccessibilityElement *)element increment:(BOOL)increment {
	if (element.action != PPSSPPAccessibilityActionActivateUI) {
		return;
	}
	[self activateElement:element];
	TapKey(increment ? NKCODE_DPAD_RIGHT : NKCODE_DPAD_LEFT);
}

- (BOOL)scrollElement:(PPSSPPAccessibilityElement *)element direction:(UIAccessibilityScrollDirection)direction {
	switch (element.action) {
	case PPSSPPAccessibilityActionDPad:
		switch (direction) {
		case UIAccessibilityScrollDirectionLeft: TapKey(NKCODE_DPAD_LEFT, DEVICE_ID_PAD_0); return YES;
		case UIAccessibilityScrollDirectionRight: TapKey(NKCODE_DPAD_RIGHT, DEVICE_ID_PAD_0); return YES;
		case UIAccessibilityScrollDirectionUp: TapKey(NKCODE_DPAD_UP, DEVICE_ID_PAD_0); return YES;
		case UIAccessibilityScrollDirectionDown: TapKey(NKCODE_DPAD_DOWN, DEVICE_ID_PAD_0); return YES;
		default: return NO;
		}
	case PPSSPPAccessibilityActionLeftStick:
		switch (direction) {
		case UIAccessibilityScrollDirectionLeft: TapAxis(JOYSTICK_AXIS_X, -1.0f); return YES;
		case UIAccessibilityScrollDirectionRight: TapAxis(JOYSTICK_AXIS_X, 1.0f); return YES;
		case UIAccessibilityScrollDirectionUp: TapAxis(JOYSTICK_AXIS_Y, -1.0f); return YES;
		case UIAccessibilityScrollDirectionDown: TapAxis(JOYSTICK_AXIS_Y, 1.0f); return YES;
		default: return NO;
		}
	case PPSSPPAccessibilityActionRightStick:
		switch (direction) {
		case UIAccessibilityScrollDirectionLeft: TapAxis(JOYSTICK_AXIS_Z, -1.0f); return YES;
		case UIAccessibilityScrollDirectionRight: TapAxis(JOYSTICK_AXIS_Z, 1.0f); return YES;
		case UIAccessibilityScrollDirectionUp: TapAxis(JOYSTICK_AXIS_RZ, -1.0f); return YES;
		case UIAccessibilityScrollDirectionDown: TapAxis(JOYSTICK_AXIS_RZ, 1.0f); return YES;
		default: return NO;
		}
	case PPSSPPAccessibilityActionFaceButtons:
		switch (direction) {
		case UIAccessibilityScrollDirectionLeft: TapKey(NKCODE_BUTTON_4, DEVICE_ID_PAD_0); return YES;
		case UIAccessibilityScrollDirectionRight: TapKey(NKCODE_BUTTON_3, DEVICE_ID_PAD_0); return YES;
		case UIAccessibilityScrollDirectionUp: TapKey(NKCODE_BUTTON_1, DEVICE_ID_PAD_0); return YES;
		case UIAccessibilityScrollDirectionDown: TapKey(NKCODE_BUTTON_2, DEVICE_ID_PAD_0); return YES;
		default: return NO;
		}
	case PPSSPPAccessibilityActionShoulders:
		switch (direction) {
		case UIAccessibilityScrollDirectionLeft:
			_lastShoulderKey = NKCODE_BUTTON_L1;
			if (_heldShoulderKey != NKCODE_BUTTON_L1) {
				TapKey(NKCODE_BUTTON_L1, DEVICE_ID_PAD_0);
			}
			return YES;
		case UIAccessibilityScrollDirectionRight:
			_lastShoulderKey = NKCODE_BUTTON_R1;
			if (_heldShoulderKey != NKCODE_BUTTON_R1) {
				TapKey(NKCODE_BUTTON_R1, DEVICE_ID_PAD_0);
			}
			return YES;
		default:
			return NO;
		}
	default:
		return NO;
	}
}

- (BOOL)accessibilityPerformEscape {
	TapKey(NKCODE_BACK);
	return YES;
}

- (BOOL)accessibilityPerformMagicTap {
	if (GetUIState() != UISTATE_INGAME) {
		return NO;
	}
	TapKey(NKCODE_BUTTON_START, DEVICE_ID_PAD_0);
	return YES;
}

@end
