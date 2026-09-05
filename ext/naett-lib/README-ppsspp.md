# naett in PPSSPP

Vendored copy of [naett](https://github.com/erkkah/naett) by Erik Agsjö (MIT licensed, see `LICENSE`).

Upstream version: **v0.3.3** (`5f695cfa9fcbf30668a4d3ac4b4abf1cd89a1302`, 2024-04-13)

This used to be a git submodule. It's now in-tree because upstream has been dormant since
April 2024 and we need to carry local changes (see below). It's ~1500 lines of C, which is
smaller than several other things we already vendor.

## Local changes

Keep this list up to date - it's what makes it possible to move to a newer upstream later.

- Dropped the generated single-file amalgam (`naett.c`) and `src/amalgam.h`, along with
  `example/` and `testrig/`. We build `src/*.c` directly, so that the file you edit is the
  file that gets compiled.
- `src/naett_internal.h`: added `#include "../naett.h"`. The amalgam included `naett.h`
  ahead of everything else; building the sources directly, each one needs it.
- `src/naett_linux.c`: added `#include <stdio.h>` / `#include <stdlib.h>`. It uses
  `exit`/`calloc`/`realloc`/`free`/`fprintf` but never included either header - in the amalgam it
  got them from `naett_core.c` further up the concatenation. Building it on its own is an error
  with a modern compiler.
- `src/naett_curl.h` / `src/naett_curl.c`: new, ours. Loads libcurl with `dlopen` instead of
  linking against it, so libcurl stays a soft dependency at runtime. `naett_linux.c` includes
  that header in place of `<curl/curl.h>`.
- `src/naett_linux.c`: replaced the `panic()` that called `exit(1)` on a pipe or
  `curl_multi_perform` failure. Taking the whole emulator down because a download failed isn't
  acceptable, so the backend now disables itself and requests complete with `naettGenericError`.
- `src/naett_linux.c`: `CURLINFO_RESPONSE_CODE` writes a `long`, and `res->code` is an `int` -
  upstream passed `&res->code` straight to `curl_easy_getinfo`, writing 8 bytes into 4. Reads
  into a `long` local now.
- `src/naett_linux.c`: `curl_easy_setopt` is varargs and takes a `long` for these options;
  upstream passed `int` literals and `int` variables, which is UB on LP64 (and what curl's own
  typecheck macros warn about). They're `1L`/`(long)` now.
- `src/naett_core.c`: `naettFree` never freed `options.userAgent`, though it's `strdup`'d by
  the same setter as `method`. Leaked once per request for every caller that sets a user agent,
  which we do on all of them.
- `src/naett_core.c`: `defaultBodyWriter` doubled an `int` capacity until it fit, which is signed
  overflow on a large response, and used the `realloc` result without checking it - losing the
  old pointer and then `memcpy`ing through NULL. Grows in `int64_t` against `INT_MAX` and
  reports failure by returning short, which every caller already treats as an error.
- `src/naett_linux.c`: `headerCallback` only handed its `strndup` to the header list when the
  line had a colon, and leaked it otherwise. curl passes the status line and the blank line that
  ends the header block, so that leaked at least twice per response.
- `src/naett_osx.c`: the delegate class was built with `objc_allocateClassPair` and then used
  without ever calling `objc_registerClassPair`, which the runtime requires before the class can
  be instantiated. Registered now, after the methods and ivar are added.
- `src/naett_osx.c`: the response header arrays were VLAs sized from the server's header count -
  unbounded stack use from network data, and a zero-length VLA when a response had no headers.
  They're heap allocations now, skipped entirely when there are none.
- `src/naett_osx.c`: the `NSURLSession` was stored in the response without a `retain`, though the
  autorelease pool it came from is drained before returning. Retained, and released in
  `naettPlatformCloseResponse`.
- `src/naett_objc.h`: `addMethod`/`addIvar` reported failure with `assert` only, so in release a
  delegate could silently come up without its methods. They print as well now.
