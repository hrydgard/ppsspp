# Emulator API - raw protocol

This documents the raw `sceIoDevctl`-based protocol behind PPSSPP's emulator API. Most homebrew
should just use the C wrapper in [`ppsspp_emu_api.h`](ppsspp_emu_api.h) instead (see the
[README](README.md) and the [website docs](https://www.ppsspp.org/docs/development/ppsspp-internals/emu-api/)
for that) - read this page if you're extending the wrapper, writing a binding for another language,
or just curious how it works under the hood.

Important: None of this will work on the real PSP! Always check `IS_EMULATOR` (see below) before
relying on any of it, and keep a working fallback path for real hardware.

## How it works

The whole API is exposed through a single, existing PSP syscall: `sceIoDevctl`. PPSSPP recognizes
two special, fake device names that don't correspond to any real device:

* `"emulator:"`
* `"kemulator:"`

Both names are currently handled identically (there's no user/kernel distinction enforced), so
either works from a user-mode homebrew app. The implementation lives in `sceIoDevctl()` in
[`Core/HLE/sceIo.cpp`](https://github.com/hrydgard/ppsspp/blob/master/Core/HLE/sceIo.cpp) - search
for `"emulator:"` if you want to see exactly what each command does, or if you're adding a new one.

The standard `sceIoDevctl` signature is used, just with PPSSPP-specific `cmd` numbers and
argument/output block layouts:

```c
int sceIoDevctl(const char *devicename, unsigned int cmd, void *indata, int inlen, void *outdata, int outlen);
```

* `devicename` - `"emulator:"` or `"kemulator:"`.
* `cmd` - one of the `EMULATOR_DEVCTL__*` values below.
* `indata`/`inlen` - input block, meaning depends on `cmd`. Some commands instead just check whether `indata` is NULL/non-NULL as a boolean flag.
* `outdata`/`outlen` - output block, meaning depends on `cmd`. Most commands write a single `u32` or `float` here.

If `cmd` doesn't match any known command, `sceIoDevctl` returns an error (`UNKNOWN PARAMETERS`)
rather than crashing, so probing for support is safe.

## Commands

| Command | Value | Direction | Purpose |
|---|---|---|---|
| `EMULATOR_DEVCTL__GET_HAS_DISPLAY` | 1 | out: `u32` | Writes 1 if there's a real display (normal PPSSPP), 0 if running headless (`PPSSPPHeadless`). Useful to skip presentation/vblank-dependent work when there's nothing to show. |
| `EMULATOR_DEVCTL__SEND_OUTPUT` | 2 | in: bytes | Sends a raw block of text straight to PPSSPP's debug/log output (and to headless's collected output buffer, if any). Handy for logging from homebrew without going through `sceIoWrite` to a real file. |
| `EMULATOR_DEVCTL__IS_EMULATOR` | 3 | out: `u32` | Writes 1. This is the one to call first: if the `sceIoDevctl` call itself fails, you're not running under PPSSPP (or a build that doesn't implement this API), and none of the rest of this page applies. |
| `EMULATOR_DEVCTL__VERIFY_STATE` | 4 | none | Asks PPSSPP to do an internal savestate round-trip (save to memory, then verify it reads back correctly) as a consistency check. Runs asynchronously - it doesn't report the pass/fail result back to your code, it just gets logged on the PPSSPP side. Mainly useful for automated testing of the emulator itself. |
| `EMULATOR_DEVCTL__EMIT_SCREENSHOT` | 0x20 | none | Grabs the current framebuffer and delivers it through PPSSPP's internal debug-screenshot hook, which is used by things like the pspautotests/frametest infrastructure to collect result images. Not useful as a general "save a screenshot to memstick" feature - it doesn't write a file. |
| `EMULATOR_DEVCTL__TOGGLE_FASTFORWARD` | 0x30 | in: bool (NULL/non-NULL) | Turns PPSSPP's fast-forward mode on (non-NULL `indata`) or off (NULL). |
| `EMULATOR_DEVCTL__GET_ASPECT_RATIO` | 0x31 | out: `float` | Writes the display's current aspect ratio. Only correct in landscape orientation right now. |
| `EMULATOR_DEVCTL__GET_SCALE` | 0x32 | out: `float` | Writes the current display scale factor. Only correct in landscape orientation right now. |
| `EMULATOR_DEVCTL__GET_AXIS` | 0x33 | in: axis index (as `indata` value, not a pointer), out: `float` | Reads an analog axis value that a PPSSPP-side input plugin has injected (see below), by `JOYSTICK_AXIS_*` index. |
| `EMULATOR_DEVCTL__GET_VKEY` | 0x34 | in: key code (as `indata` value, not a pointer), out: `u8` | Reads whether a virtual key that a PPSSPP-side input plugin has injected is currently pressed, by PPSSPP's internal key code (see below). |

A couple of notes on quirks that are easy to trip over:

* For `GET_AXIS` and `GET_VKEY`, the "input" isn't the `indata` buffer contents - it's the `indata` pointer value itself, used directly as an integer index. This matches how the current PPSSPP implementation reads it (`argAddr` is compared against the axis/key range and used directly), so pass the index as if it were a pointer, e.g. `sceIoDevctl("emulator:", EMULATOR_DEVCTL__GET_AXIS, (void *)JOYSTICK_AXIS_X, 0, &value, sizeof(value))`.
* `GET_AXIS` and `GET_VKEY` don't read normal controller input (`sceCtrl*` already does that) - they read state from PPSSPP's HLE plugin system, i.e. values that a native PPSSPP-side plugin PRX has explicitly set for your homebrew to pick up. If no plugin is active, expect these to just come back as 0/unpressed.
* The axis index matches PPSSPP's internal `JOYSTICK_AXIS_*` enum (`Common/Input/KeyCodes.h`), and the key code matches PPSSPP's internal `NKCODE_*` enum (same file), not any PSP SDK enum. These mostly mirror Android's key/axis codes. A handful of the more useful ones: `NKCODE_DPAD_UP/DOWN/LEFT/RIGHT`, `NKCODE_BUTTON_CROSS/CIRCLE/SQUARE/TRIANGLE`, `JOYSTICK_AXIS_X/Y`. See the header for the full list if you need something more obscure.
* You'll sometimes see an `EMULATOR_DEVCTL__SEND_CTRLDATA` (0x10) constant referenced in older test code. It is not currently implemented by PPSSPP - calling it just gets you the generic "unknown parameters" error. Don't rely on it.

## Raw example

```c
#include <pspiofilemgr.h>

#define EMULATOR_DEVCTL__IS_EMULATOR     3
#define EMULATOR_DEVCTL__SEND_OUTPUT     2

int runningOnPPSSPP = sceIoDevctl("emulator:", EMULATOR_DEVCTL__IS_EMULATOR, NULL, 0, NULL, 0) == 0;

if (runningOnPPSSPP) {
    const char *msg = "Hello from homebrew, running under PPSSPP!\n";
    sceIoDevctl("emulator:", EMULATOR_DEVCTL__SEND_OUTPUT, (void *)msg, strlen(msg), NULL, 0);
}
```
