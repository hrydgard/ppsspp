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
- `src/naett_win.c`: the header sizing call only reports a size when it fails with
  `ERROR_INSUFFICIENT_BUFFER`; on any other failure the size stayed zero and `unpackHeaders` ran
  `wcslen` over a `malloc(0)` block. Checked, and the second query's result is checked too.
- `src/naett_win.c`: `winToUTF8`/`winFromUTF8`/`wcsndup` could all return NULL and every caller
  used the result unchecked - `packHeaders` most visibly, whose result is indexed as
  `headers[0]`. All checked now.
- `src/naett_win.c`: `res->bytesLeft` is unsigned, so a read longer than the announced count
  wrapped it into an enormous value and kept the read loop running.
- `src/naett_linux.c`: the worker read the queued `CURL*` out of the pipe into the start of its
  buffer while tracking a fill position, so a short read would have resumed mid-pointer and
  handed curl a mangled handle. Only ever safe because a write that size to a pipe is atomic.
- `src/naett_linux.c`: the easy handle is removed from the multi before being cleaned up, which
  is what curl asks for. `curl_multi_remove_handle` and `curl_multi_cleanup` were added to the
  dlopen table in `naett_curl.h`/`naett_curl.c` for this.
- `src/naett_linux.c`: `workerRunning` is written by the worker and read by the request path, so
  it's an `atomic_int` now rather than a plain `int`.
- `src/naett_linux.c`: the `write` that hands a request to the worker was unchecked - a failed
  one meant a request that never ran and never completed, so the caller polled `naettComplete`
  forever. Also retries on `EINTR`, and `curl_easy_init` failure is handled.
- `src/naett_linux.c`: the read and write callbacks returned the body callbacks' `int` straight
  to curl, which reads it as a `size_t` - a negative arrived as an enormous count instead of an
  error.
- `src/naett_linux.c`: the multi handle and the pipe leaked when init failed partway.
- `src/naett_android.c`: `getEnv` called `AttachCurrentThread` and nothing ever detached.
  `processRequest` detaches its own thread, but `naettPlatformInitRequest`/`FreeRequest` run on
  the caller's, and a thread that exits while attached is fatal on Android. They attach only if
  the thread wasn't already, and detach when they're done.
- `src/naett_android.c`: `pthread_create`'s result was ignored - with no worker, nothing sets
  `complete` and the caller polls `naettComplete` forever.
- `src/naett_android.c`: `getOutputStream` can throw, and the calls after it ran with the
  exception still pending, which isn't allowed for most of JNI. Checked now.
- `src/naett_android.c`: `GetMethodID` returns NULL for a method it can't find, and calling with
  a NULL `jmethodID` aborts the VM; the header loop could also hand `GetStringUTFChars` a null
  value for a header with no entries.
- `src/naett_core.c`: `naettClose` cleared `res->request` before calling the backend's close,
  which is the one thing the backend needs - the WinHTTP handles hang off the request. The
  backend goes first now.
- `naett.h`: documented that a response should be complete before it's closed. Only the Android
  backend really cancels and waits.
- `src/naett_win.c`: `naettPlatformCloseResponse` was empty, so the status callback kept the
  freed response as its context. Unhooks the callback and closes the request handle. Not a full
  cancel - a callback already running isn't waited for, which would need the
  `WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING` handshake.
- `src/naett_osx.c`: `invalidateAndCancel` returns before the session lets go of its delegate, so
  the delegate's back pointer to the response is cleared first, and `didReceiveData` checks it
  (as `didCompleteWithError` already did).
