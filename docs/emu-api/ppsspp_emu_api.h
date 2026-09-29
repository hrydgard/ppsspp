// PPSSPP emulator API - header-only C wrapper.
//
// PPSSPP exposes a handful of emulator-only tricks to homebrew through the
// "emulator:"/"kemulator:" pseudo-devices of sceIoDevctl. None of this exists
// on real hardware - always check ppsspp_is_emulator() before depending on
// anything else here, and keep a working fallback path for real PSPs.
//
// C API docs: https://www.ppsspp.org/docs/development/ppsspp-internals/emu-api/
//
// Raw sceIoDevctl protocol this wraps (cmd numbers, layouts, quirks): see protocol.md
// in this same directory, or Core/HLE/sceIo.cpp (search for "emulator:") in the main
// PPSSPP source tree, which is the source of truth.
//
// Usage: just #include this file in a PSPSDK-based homebrew project. It's
// entirely static inline functions over sceIoDevctl, so there's nothing to
// link.

#pragma once

#include <pspiofilemgr.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

enum PPSSPPEmulatorDevctlCmd {
	PPSSPP_DEVCTL__GET_HAS_DISPLAY     = 1,
	PPSSPP_DEVCTL__SEND_OUTPUT         = 2,
	PPSSPP_DEVCTL__IS_EMULATOR         = 3,
	PPSSPP_DEVCTL__VERIFY_STATE        = 4,

	PPSSPP_DEVCTL__EMIT_SCREENSHOT     = 0x20,

	PPSSPP_DEVCTL__TOGGLE_FASTFORWARD  = 0x30,
	PPSSPP_DEVCTL__GET_ASPECT_RATIO    = 0x31,
	PPSSPP_DEVCTL__GET_SCALE           = 0x32,
	PPSSPP_DEVCTL__GET_AXIS            = 0x33,
	PPSSPP_DEVCTL__GET_VKEY            = 0x34,

	PPSSPP_DEVCTL__BEFORE_UI_DRAW      = 0x35,
};

// Either name works - PPSSPP currently treats them identically.
#define PPSSPP_EMULATOR_DEVICE "emulator:"

// Returns non-zero if running under PPSSPP. Call this before trusting any
// of the other functions below - on real hardware, or on an emulator that
// doesn't implement this API, sceIoDevctl will simply fail here.
static inline int ppsspp_is_emulator(void) {
	return sceIoDevctl(PPSSPP_EMULATOR_DEVICE, PPSSPP_DEVCTL__IS_EMULATOR, NULL, 0, NULL, 0) == 0;
}

// Returns non-zero if there's an actual display to render to (i.e. not
// running under PPSSPPHeadless).
static inline int ppsspp_has_display(void) {
	unsigned int hasDisplay = 0;
	sceIoDevctl(PPSSPP_EMULATOR_DEVICE, PPSSPP_DEVCTL__GET_HAS_DISPLAY, NULL, 0, &hasDisplay, sizeof(hasDisplay));
	return (int)hasDisplay;
}

// Sends a block of text straight to PPSSPP's debug/log output.
static inline void ppsspp_send_output(const char *data, int len) {
	sceIoDevctl(PPSSPP_EMULATOR_DEVICE, PPSSPP_DEVCTL__SEND_OUTPUT, (void *)data, len, NULL, 0);
}

// Convenience wrapper for ppsspp_send_output() over a NUL-terminated string.
static inline void ppsspp_send_output_str(const char *str) {
	ppsspp_send_output(str, (int)strlen(str));
}

// Asks PPSSPP to do an internal savestate round-trip as a consistency check.
// Asynchronous - the pass/fail result is only logged on the PPSSPP side, not
// reported back here. Mainly useful when testing the emulator itself.
static inline void ppsspp_verify_state(void) {
	sceIoDevctl(PPSSPP_EMULATOR_DEVICE, PPSSPP_DEVCTL__VERIFY_STATE, NULL, 0, NULL, 0);
}

// Reports that the frame of the game is between its world and its UI.
//
// PPSSPP makes everything drawn so far real, and then counts the counter this
// is handed - the caller's own, in its own memory, which has to stay there for
// as long as the game runs. Call this from the point of the game's own code
// where the world is done and the UI is not drawn yet; the emulator cannot work
// that point out by itself.
//
// A caller that cannot say where the world of the frame ends this time passes 0
// for the position, and its counter is counted right away. The point of the last
// call that could say stays the point of the frame until another one says
// otherwise, so a frame the point is missed for does not make what is drawn
// there - the drawing of a plugin of the host, and the post shaders of the
// emulator - come and go.
//
// This exists for a plugin of the host application that draws into the frame of
// the game: such a plugin watches the counter, and fills the frame in when it
// changes, which is what puts its drawing under the UI the game sends next
// instead of on top of it. PPSSPP runs its own post-processing shaders at that
// same point when they are enabled, so they end up under that UI too instead of
// over the whole frame. An emulator that does not know the call - a real PSP,
// for one - does nothing with it, so a game built against it still runs
// anywhere.
static inline void ppsspp_before_ui_draw(volatile unsigned int *tick) {
	sceIoDevctl(PPSSPP_EMULATOR_DEVICE, PPSSPP_DEVCTL__BEFORE_UI_DRAW, (void *)tick, sizeof(*tick), NULL, 0);
}

// Delivers the current framebuffer through PPSSPP's internal debug-screenshot
// hook (used by the pspautotests/frametest infrastructure). This does not
// write a file to the memory stick - it's not a general screenshot feature.
static inline void ppsspp_emit_screenshot(void) {
	sceIoDevctl(PPSSPP_EMULATOR_DEVICE, PPSSPP_DEVCTL__EMIT_SCREENSHOT, NULL, 0, NULL, 0);
}

// Turns PPSSPP's fast-forward mode on or off.
static inline void ppsspp_toggle_fastforward(int enable) {
	sceIoDevctl(PPSSPP_EMULATOR_DEVICE, PPSSPP_DEVCTL__TOGGLE_FASTFORWARD, enable ? (void *)1 : NULL, 0, NULL, 0);
}

// Current display aspect ratio. Only correct in landscape orientation.
static inline float ppsspp_get_aspect_ratio(void) {
	float ar = 0.0f;
	sceIoDevctl(PPSSPP_EMULATOR_DEVICE, PPSSPP_DEVCTL__GET_ASPECT_RATIO, NULL, 0, &ar, sizeof(ar));
	return ar;
}

// Current display scale factor. Only correct in landscape orientation.
static inline float ppsspp_get_scale(void) {
	float scale = 0.0f;
	sceIoDevctl(PPSSPP_EMULATOR_DEVICE, PPSSPP_DEVCTL__GET_SCALE, NULL, 0, &scale, sizeof(scale));
	return scale;
}

// Reads an analog axis value injected by a PPSSPP-side input plugin, by
// JOYSTICK_AXIS_* index (see PPSSPP's Common/Input/KeyCodes.h). This is NOT
// normal controller input - use sceCtrl* for that. Returns 0 if no plugin
// has set this axis.
static inline float ppsspp_get_axis(int axisIndex) {
	float value = 0.0f;
	sceIoDevctl(PPSSPP_EMULATOR_DEVICE, PPSSPP_DEVCTL__GET_AXIS, (void *)(long)axisIndex, 0, &value, sizeof(value));
	return value;
}

// Reads whether a virtual key injected by a PPSSPP-side input plugin is
// currently pressed, by NKCODE_* value (see PPSSPP's Common/Input/KeyCodes.h,
// codes mostly mirror Android's). This is NOT normal controller input - use
// sceCtrl* for that. Returns 0 if no plugin has set this key.
static inline unsigned char ppsspp_get_vkey(int keyCode) {
	unsigned char value = 0;
	sceIoDevctl(PPSSPP_EMULATOR_DEVICE, PPSSPP_DEVCTL__GET_VKEY, (void *)(long)keyCode, 0, &value, sizeof(value));
	return value;
}

#ifdef __cplusplus
}  // extern "C"
#endif
