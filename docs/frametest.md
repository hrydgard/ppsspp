# Framedump rendering tests (frametests)

PPSSPP has a test system that replays GE frame dumps (`.ppdmp`) through
`PPSSPPHeadless` with various rendering settings, comparing the rendered
output against stored reference images. It is designed to catch rendering
regressions across GPU backends and configurations.

The system has three parts:

- **The test runner**: `frametests.py` (repo root). Test-set agnostic - it
  works against any test data tree pointed to by its configuration.
- **The configuration**: a JSON file describing where the test data lives and
  which rendering variants to run (e.g. `frametests/frametests.json`).
- **The test set**: the frame dumps and reference images (e.g.
  `frametests/dumps/` and `frametests/ref/`). Different environments can use
  different test sets - GitHub CI uses the one in the repo, custom machines
  can use their own (possibly much larger) ones.

## Quick start

```bash
# Build PPSSPPHeadless (see headless/README.md), then:
python3 frametests.py frametests/frametests.json
```

The first run generates reference images for any dumps that don't have them
yet (status `NEW`) and compares the rest (status `PASS`/`FAIL`). A summary is
printed and a self-contained HTML report is written to
`frametests/frametest-out/report.html` (default output dir; gitignored).

Exit code is `0` if all tests passed, `1` if anything failed, `2` on
configuration/usage errors.

## Configuration

The configuration is a single JSON file. All relative paths are resolved
against the config file's directory, so the same script works with any test
set by just pointing `--config` at a different file.

```json
{
	"testRoot": "dumps",
	"refRoot": "ref",
	"outputRoot": "frametest-out",
	"headlessPath": "",
	"timeout": 60,
	"maxMse": 0.0,
	"variants": {
		"soft": "--graphics=software"
	}
}
```

| Key             | Description                                                                 |
|-----------------|-----------------------------------------------------------------------------|
| `testRoot`      | Directory tree containing frame dumps (`.ppdmp` files, possibly wrapped in `.zip`). |
| `refRoot`       | Where reference images live; mirrors the `testRoot` tree.                   |
| `outputRoot`    | Where logs, diff images, the report, and generated references go.           |
| `headlessPath`  | Path to the `PPSSPPHeadless` binary. Empty = auto-detect (see below).        |
| `timeout`       | Per-test timeout in seconds.                                                 |
| `maxMse`        | Maximum allowed MSE for screenshot comparison (0 = exact match).            |
| `variants`      | Map of variant name to command line arguments for the headless binary.       |

### Variants

Each test runs once per variant. The variant name is appended to the
reference image filename, so each variant gets its own reference set:

```
dumps/Depth/11578 Virtua Tennis pause menu ULES00126_0002.zip
  → ref/Depth/11578 Virtua Tennis pause menu ULES00126_0002-soft.png
```

The variant value is an arbitrary command line argument string passed to
`PPSSPPHeadless`, so new rendering configurations are just new entries (or a
different config file on a machine with the right GPU):

```json
"variants": {
	"soft":  "--graphics=software",
	"gl":    "--graphics=opengl",
	"gl-4x": "--graphics=opengl --resolution-scale=4"
}
```

## How a test runs

For each dump (recursively under `testRoot`) and each variant:

- If the reference image `<name>-<variant>.png` is **missing**: the dump is
  rendered and the output saved as the new reference. Status: `NEW`. This is
  how references are created - run locally, then commit the generated images
  (also copied to `<outputRoot>/generated/` for convenience).
- If the reference **exists**: the dump is rendered, the output saved to
  `<outputRoot>/actuals/`, and compared against the reference using MSE
  (mean squared error over R, G, B per pixel, alpha ignored). A visual
  comparison image (actual / reference+diff) is saved to
  `<outputRoot>/diffs/` whenever a comparison runs. Status: `PASS` or `FAIL`
  (mismatch, crash, or timeout).

A `FAIL` with no MSE reported usually means the headless binary crashed
before producing a screenshot, and a reference image that can't be loaded
(corrupt) is reported as `ERROR`. The full emulator log of every non-passing
test is kept in `<outputRoot>/logs/`.

### Reference images

- PNG, 512×272 (480×272 display in a 512-wide framebuffer), stored top-down
  (row 0 = top of screen). The BMP output format is bottom-up per the BMP
  spec; the flip is applied only when writing BMPs.
- Generated with `--graphics=software` they are fully deterministic: a
  subsequent run produces byte-identical output, so `maxMse` can be 0.
- If rendering code changes the output, existing references may need
  regenerating: delete the affected reference images and re-run to regenerate
  them.

### Headless flags used

The runner uses `--screenshot=<ref>` (compare), `--screenshot-save=<file>`
(save output; PNG if the path ends in `.png`, else BMP) and
`--screenshot-diff=<file>` (always write a visual comparison when comparing).
See `headless/README.md` for details.

## Command line options

```
usage: frametests.py [OPTIONS] [CONFIG.json]
```

| Option             | Description                                                           |
|--------------------|-----------------------------------------------------------------------|
| `CONFIG.json`      | Path to the configuration file (default: `frametests/frametests.json`). |
| `--filter=SUBSTR`  | Only run dumps whose relative path contains SUBSTR (case-insensitive). |
| `--strict`         | Treat missing reference images as failures (a configuration error).    |
| `--out-mode=all\|failures` | `all` keeps everything in the output dir; `failures` keeps only artifacts of non-passing tests (smaller CI artifacts). Default: `all`. |

The headless binary is located, in order of preference:

1. `headlessPath` from the config file.
2. The `PPSSPP_HEADLESS` environment variable.
3. Well-known paths relative to the current directory (e.g.
   `Windows/x64/Debug/PPSSPPHeadless.exe`, `build/PPSSPPHeadless`), newest
   by modification time.

## CI integration

In CI (`GITHUB_ACTIONS` is set) the runner behaves as if `--strict` was
passed and prints `::error` annotations for each failing test, so failures
show up inline in the GitHub Actions log. A typical CI job:

```yaml
- name: Build headless
  run: ./b.sh --headless
- name: Run frametests
  run: python3 frametests.py frametests/frametests.json --out-mode=failures
- name: Upload report
  uses: actions/upload-artifact@v4
  with:
    name: frametest-report
    path: frametests/frametest-out/
```

A missing reference image in CI means the test set is incomplete - the run
fails and the generated reference is available in
`frametests/frametest-out/generated/` (part of the uploaded artifact) so it
can be committed.

## Custom machines

The script is designed to run on machines with different GPUs and bigger test
sets than CI. Copy the repo, point at a custom config:

```bash
python3 frametests.py /path/to/my-config.json
```

The config can live anywhere and point at any test data tree; only the
script itself is shared. Add hardware-specific variants (`--graphics=vulkan`,
`--msaa=...`, etc.) to the machine's own config - no code changes needed for
new flag combinations, as long as the headless binary supports them.

## Adding a new framedump

1. Drop the dump into `frametests/dumps/` (either as a `.ppdmp` or zipped).
2. Run `python3 frametests.py frametests/frametests.json --filter=<name>` -
   the reference image(s) are generated and copied to `<outputRoot>/generated/`.
3. Commit the dump and the reference images under `frametests/ref/`.

The CI test set (dumps and references) is maintained separately from the
runner; the `frametests/` directory is just one of several possible test sets
and may become a git submodule at some point.
