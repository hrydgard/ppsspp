# PPSSPP Emulator API

`ppsspp_emu_api.h` is a small, header-only C wrapper around the emulator-only tricks PPSSPP exposes
to homebrew through the `"emulator:"` device of `sceIoDevctl` (implemented in `Core/HLE/sceIo.cpp`,
search for `"emulator:"`).

None of this works on real PSP hardware - always check `ppsspp_is_emulator()` before relying on
anything else in the header, and keep a working fallback path for real hardware.

C API documentation: https://www.ppsspp.org/docs/development/ppsspp-internals/emu-api/

Raw `sceIoDevctl` protocol (commands, layouts, quirks): see [protocol.md](protocol.md).

## Usage

Just copy `ppsspp_emu_api.h` into a PSPSDK-based homebrew project and `#include` it. It's entirely
`static inline` functions over `sceIoDevctl`, so there's no library to build or link.

```c
#include "ppsspp_emu_api.h"

if (ppsspp_is_emulator()) {
    ppsspp_send_output_str("Hello from homebrew, running under PPSSPP!\n");
}
```
