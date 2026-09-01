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
