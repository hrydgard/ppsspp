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
| `EMULATOR_DEVCTL__BEFORE_UI_DRAW` | 0x35 | in: pointer to a `u32` counter of the caller's own; `outPtr` (as a value, not written to): where the commands the caller has written into the display list of its game end at this moment, or 0 if it cannot say | Says that the frame of the game is between its world and its UI. PPSSPP runs the display list of the game up to the given point, makes what that leaves in the draw engine real, and then counts the counter that was handed over. Only meaningful together with a plugin of the host application that draws into the frame of the game: it watches the counter and fills the frame in when it changes, which puts its drawing under the UI the game sends next. See the note below. |

A couple of notes on quirks that are easy to trip over:

* For `GET_AXIS` and `GET_VKEY`, the "input" isn't the `indata` buffer contents - it's the `indata` pointer value itself, used directly as an integer index. This matches how the current PPSSPP implementation reads it (`argAddr` is compared against the axis/key range and used directly), so pass the index as if it were a pointer, e.g. `sceIoDevctl("emulator:", EMULATOR_DEVCTL__GET_AXIS, (void *)JOYSTICK_AXIS_X, 0, &value, sizeof(value))`.
* `GET_AXIS` and `GET_VKEY` don't read normal controller input (`sceCtrl*` already does that) - they read state from PPSSPP's HLE plugin system, i.e. values that a native PPSSPP-side plugin PRX has explicitly set for your homebrew to pick up. If no plugin is active, expect these to just come back as 0/unpressed.
* The axis index matches PPSSPP's internal `JOYSTICK_AXIS_*` enum (`Common/Input/KeyCodes.h`), and the key code matches PPSSPP's internal `NKCODE_*` enum (same file), not any PSP SDK enum. These mostly mirror Android's key/axis codes. A handful of the more useful ones: `NKCODE_DPAD_UP/DOWN/LEFT/RIGHT`, `NKCODE_BUTTON_CROSS/CIRCLE/SQUARE/TRIANGLE`, `JOYSTICK_AXIS_X/Y`. See the header for the full list if you need something more obscure.
* You'll sometimes see an `EMULATOR_DEVCTL__SEND_CTRLDATA` (0x10) constant referenced in older test code. It is not currently implemented by PPSSPP - calling it just gets you the generic "unknown parameters" error. Don't rely on it.
* `BEFORE_UI_DRAW` is the one command here that exists for the *host* side rather than for the game: it is how a plugin of the application (an ASI/DLL injected into PPSSPP) can draw into the frame of the game *under* its HUD. The game calls it at the point of its own code where the world is done and the UI is not drawn yet. A game hands the commands of a whole frame over to the GE in one go, at its next vblank, so at that call the world of the frame is usually written into the display list but not handed over to the GE yet - which is why the caller passes where in that list its commands end at that moment (a game keeps that position itself; see the plugin of the game for examples). PPSSPP stops the list at that point, so that the counter is counted when the world of the frame is really drawn and nothing of the UI is; it also finishes the pending vertex data and sends whatever the draw engine has collected but not yet handed to the backend, so nothing of the world is left in there either. The counter is what the injected plugin polls (it reads that counter straight out of the game's memory), so everything the game draws after the call lands on top of what the plugin drew. A caller that passes 0 for that position has the counter counted right away, which is what a plugin that cannot find out where the world of the frame ends can do. The counter must live in the game's own memory and stay put for as long as the game runs - PPSSPP only writes the incremented value to the address it was given, it has no idea what else is there. On an emulator that doesn't implement it, the call does nothing at all, so a game built against it still runs anywhere.

## Drawing into the frame of a game

`BEFORE_UI_DRAW` marks the point of a frame of a game where its world is drawn and its UI is
not, and counts a counter for whoever watches it. Everything the game draws after that call
lands on top of the counter, so anything a plugin of the host application wants to end up
*under* the UI of the game has to be drawn at that point - and that point is inside the frame
of the game, where a plugin cannot draw by itself: the frame is over by the time the backend
presents it, and on OpenGL it is not even recorded on the calling thread.

PPSSPP therefore hands the drawing of its own backend over there. A plugin of the host
application - an ASI/DLL injected into the process, not a PRX in the game - asks for it once,
at the moment it is loaded:

| | |
|---|---|
| `PPSSPP_RegisterBeforeUIDrawDraw(fn)` | Exported by `PPSSPPWindows.exe` / `PPSSPPWindows64.exe`. It is not there in an older build, and looking it up with `GetProcAddress` is how a plugin tells whether this emulator has the frame point at all. |
| `fn(const PPSSPPBeforeUIDrawTarget *)` | Called from the thread that runs the display list of the game, once per frame, at the point `BEFORE_UI_DRAW` marks. |

The target it is called with is a plain struct, so a plugin needs no headers of the emulator to
be handed it:

| field | meaning |
|---|---|
| `draw` | The drawing of the backend of the emulator, a `Draw::DrawContext` of `Common/GPU/thin3d.h`. Everything a plugin creates with it is reference counted, see `release`. |
| `frame` | The framebuffer the frame of the game is being drawn into at that moment. A plugin draws into it, and the UI the game sends afterwards lands on top of that. Only valid for the duration of the call. |
| `width`, `height` | The size of that framebuffer, in pixels. |
| `shownWidth`, `shownHeight` | The size the frame ends up being shown at. A frame that is not shown in its own proportion is stretched, and a drawing that keeps its shapes round has to compensate for that; 0 when there is no window to show anything in. |
| `memory`, `reportAddress` | The memory of the game, and the address the game reported from (`BEFORE_UI_DRAW`). A plugin that keeps something of its own in the memory of the game - the state of an effect, for one - reads it from here, which is the only way it can: asking the window of the emulator instead means asking its UI thread, and that thread is waiting for this very frame and cannot answer before it is over. |
| `release` | How to give an object back that was created with `draw`. It must not be used afterwards. |

A plugin that draws here is compiled against the headers of the emulator version it targets
(`Common/GPU/thin3d.h` and whatever that pulls in), and its drawing code is written against the
`Draw::DrawContext` interface, which fits every backend of the emulator. A plugin that is loaded
into a build without `PPSSPP_RegisterBeforeUIDrawDraw` cannot use any of this and has to fall
back to what the APIs of the backends offer from a present call, which draws over the UI of the
game rather than under it.

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
