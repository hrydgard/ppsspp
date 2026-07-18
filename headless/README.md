# PPSSPPHeadless

Non-interactive, headless build of PPSSPP. It boots a PSP executable, PRX, or GE frame dump (`.ppdmp`) without a GUI, outputs emulated debug text to the console, optionally captures and compares screenshots, and exits.

Primarily intended for:
- Automated regression testing (via [pspautotests](https://github.com/hrydgard/pspautotests/))
- Replaying GE frame dumps (`.ppdmp`) to compare rendering output across GPU backends or settings
- CI pipelines that need to verify emulation correctness

## Building

### Windows (Visual Studio)

The `PPSSPPHeadless` project is part of the `Windows/PPSSPP.sln` solution. Build the `PPSSPPHeadless` target in your preferred configuration.

Example:
```
msbuild Windows\PPSSPP.sln /p:Configuration=Debug /p:Platform=x64 /t:PPSSPPHeadless
```

### Linux / macOS (CMake)

```
cmake -DHEADLESS=ON -B build-headless
cmake --build build-headless
```

## Usage

```
PPSSPPHeadless file.elf|file.prx|file.ppdmp [...] [options]
```

### Options

| Argument                        | Description                                                |
|---------------------------------|------------------------------------------------------------|
| `file.elf` / `file.prx` / `file.ppdmp` | Executable or frame dump to run. Directories followed by `...` recurse for `.prx` files. |
| `@file`                         | Read list of test filenames from a text file (`@-` for stdin). |
| `-m`, `--mount <file.cso>`      | Mount an ISO/CSO on `umd1:`.                               |
| `-r`, `--root <path>`           | Mount a path on `host0:` (ELF/PRX files must be under this). |
| `-l`, `--log`                   | Full emulator log output (not just emulated `printf`).     |
| `-o`, `--odslog`                | Write log to `OutputDebugString` (Windows only).           |
| `--graphics=<backend>`          | GPU backend: `software`, `gles`, `directx11`, `vulkan`.    |
| `--screenshot=<file>`           | Compare the rendered output against a reference screenshot. |
| `--screenshot-save=<file>`      | Save the rendered output to a BMP file (no comparison).    |
| `--max-mse=<number>`            | Maximum allowed Mean Squared Error for screenshot comparison (default: 0 = exact). |
| `--compare` / `-c`              | Compare test output with `.expected` text file and/or screenshot (see below). |
| `--timeout=<seconds>`           | Abort test if it takes longer than this.                   |
| `-v`, `--verbose`               | Print full pass/fail details.                              |
| `--bench`                       | Run multiple times and report average speed.               |
| `-i`                            | Use interpreter CPU core.                                  |
| `--ir`                          | Use IR interpreter CPU core.                               |
| `-j`                            | Use JIT CPU core (default).                                |
| `--debugger=<port>`             | Enable WebSocket debugger and break at start.              |
| `--state=<file>`                | Load a save state before running.                          |
| `--old-atrac`                   | Use the old Atrac3+ audio decoder.                         |
| `--ignore <file>`               | Skip the specified test file.                              |
| `--help` / `-h`                 | Show usage information.                                    |

## GPU Backends

The `--graphics` option selects the rendering backend:

| Backend        | Description                                                     |
|----------------|-----------------------------------------------------------------|
| `software`     | Software rasterizer (most deterministic, recommended for tests) |
| `gles`         | OpenGL ES (desktop OpenGL on non-Windows)                       |
| `directx11`    | Direct3D 11 (Windows only)                                      |
| `vulkan`       | Vulkan                                                          |

The software backend produces deterministic output across runs and is the default. Hardware backends may produce slightly different pixels due to precision differences in shaders and rasterization.

## Frame Dump Replay (`.ppdmp`)

GE frame dumps are recordings of a single frame's GE graphics commands. When a `.ppdmp` file is passed:

1. The file is identified by the `PPSSPPGE` magic header.
2. The GE commands are replayed through the selected GPU backend.
3. The framebuffer is captured automatically (512×272 stride, 480×272 visible).
4. A screenshot is sent for comparison/saving via `--compare`, `--screenshot`, or `--screenshot-save`.

### Example: Generate a reference screenshot

```bash
PPSSPPHeadless.exe --graphics=software --screenshot-save=reference.bmp frame.ppdmp
```

This outputs a 512×272 BMP file with 32-bit BGRA pixel data. The file size is always 557,110 bytes (54-byte header + 512 × 272 × 4 bytes).

### Example: Compare against a reference

```bash
# With --compare, auto-derives the screenshot path:
#   frame.ppdmp  →  looks for  frame.png  (next to the .ppdmp)
PPSSPPHeadless.exe --graphics=software --compare frame.ppdmp

# Explicit reference file:
PPSSPPHeadless.exe --graphics=software --screenshot=reference.bmp --max-mse=0.5 frame.ppdmp
```

When the MSE exceeds `--max-mse`, the following files are saved in the working directory:

| File                  | Contents                                                  |
|-----------------------|-----------------------------------------------------------|
| `__testfailure.bmp`   | The actual rendered output (512×272 BMP).                 |
| `__testcompare.png`   | Visual comparison: left column = actual, right column = reference on top, diff map on bottom. |

## Screenshot Comparison Details

- **Resolution**: Always 480×272 display captured from a 512-pixel-wide framebuffer.
- **Format**: MSE (Mean Squared Error) calculated per-pixel across R, G, B channels (alpha ignored).
- **MSE formula**: `MSE = sum((actual - reference)^2) / (width × height × 3)`
- **Reference formats**: BMP (32-bit BGRA) or PNG (auto-detected by header).
- **`--compare` auto-path**: For `.ppdmp` files, it replaces the extension with `.png`. For `.prx`/`.elf` files, it appends `.expected.bmp`.
- **Failure output**: Off by default in GitHub Actions CI; controllable via `SetWriteFailureScreenshot()`.

## Text Output Comparison

The `--compare` flag also compares emulated debug output (`printf` / `sceIoWrite` to the emulator channel) against a `.expected` text file:

- For `.prx` files: same path with `.expected` extension.
- The comparison is line-based with a diff algorithm that highlights insertions/deletions.

If only a screenshot reference exists and no `.expected` file, the test passes as long as there is no unexpected text output.

## Batch Testing

Tests can be listed in a text file or piped via stdin:

```bash
# List file
PPSSPPHeadless.exe --compare --timeout=5 @testlist.txt

# Stdin
echo "test1.prx test2.prx test3.ppdmp" | PPSSPPHeadless.exe --compare --timeout=5 @-

# Recursive directory scan (append /...)
PPSSPPHeadless.exe --compare tests/cpu/...
```

Exit code is 0 if all tests pass, 1 if any fail.

## CI Integration

GitHub Actions is auto-detected via the `GITHUB_ACTIONS` environment variable and will print error annotations inline.

Example workflow step:

```yaml
- name: Headless tests
  run: |
    PPSSPPHeadless.exe --graphics=software --compare --timeout=10 @testlist.txt
```

## Configuration

The following configuration is hardcoded for headless mode (config file is never saved):

| Setting              | Value      | Notes                                    |
|----------------------|------------|------------------------------------------|
| Internal resolution  | 1× (480×272) | Fixed.                                 |
| Hardware transform   | Enabled    |                                          |
| Vertex decoder JIT   | Enabled    |                                          |
| Software renderer JIT| Enabled    |                                          |
| Skip GPU readback    | No skip    | Full readback for accurate screenshots.  |
| Ignore bad mem access| Enabled    |                                          |
| Firmware version     | 6.60       |                                          |
| PSP model            | Slim       |                                          |

## Comparison with `headless.txt`

This README supersedes the older `headless.txt`, which is kept for reference but no longer updated.
