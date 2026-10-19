#!/usr/bin/env python
"""Framedump rendering test runner for PPSSPPHeadless.

Runs GE frame dumps (".ppdmp", possibly zipped) through PPSSPPHeadless with a
set of rendering variants (command line option sets), generating reference
images when missing and comparing against existing ones when present. Produces
a self-contained HTML report and returns a nonzero exit code on failure.

This script is test-set agnostic: it reads its configuration from a JSON file
(similar to frametests/frametests.json) that points at the test data tree,
so it can be used against different test sets (the CI one, or bigger private
ones on custom machines).

Usage:
    frametests.py [OPTIONS] [CONFIG.json]

Example:
    python3 frametests.py frametests/frametests.json
"""

import argparse
import base64
import glob
import html
import json
import os
import re
import shlex
import shutil
import subprocess
import platform
import sys
import time
from pathlib import Path

# test.py-style candidate paths for the headless binary, relative to the
# current working directory, in preference order.
# The machine's own architecture comes first, the same way test.py picks: an x64 build runs on
# Windows-on-ARM too, under emulation, so looking for it first quietly tests the emulated build.
HEADLESS_CANDIDATES = ([
    "Windows/ARM64/Debug/PPSSPPHeadless.exe",
    "Windows/ARM64/Release/PPSSPPHeadless.exe",
] if platform.machine().lower() in ("arm64", "aarch64") else []) + [
    "Windows/x64/Debug/PPSSPPHeadless.exe",
    "Windows/Debug/PPSSPPHeadless.exe",
    "Windows/x64/Release/PPSSPPHeadless.exe",
    "Windows/Release/PPSSPPHeadless.exe",
    "build/PPSSPPHeadless",
    "build-headless/PPSSPPHeadless",
    "build*/PPSSPPHeadless",
    "PPSSPPHeadless",
    "ppsspp/PPSSPPHeadless",
]

MSE_RE = re.compile(r"Screenshot MSE: ([0-9.eE+-]+)")
LOG_EMBED_LIMIT = 64 * 1024

STATUS_PASS = "PASS"
STATUS_FAIL = "FAIL"
STATUS_NEW = "NEW"
STATUS_ERROR = "ERROR"


def find_headless(config_dir, config_path):
    """Locate the PPSSPPHeadless binary. The config headlessPath wins, then
    the PPSSPP_HEADLESS env var, then candidate paths (newest by mtime)."""
    candidates = []
    if config_path:
        p = Path(config_path)
        candidates.append(p if p.is_absolute() else config_dir / p)
    env_path = os.environ.get("PPSSPP_HEADLESS")
    if env_path:
        candidates.append(Path(env_path))
    for pattern in HEADLESS_CANDIDATES:
        candidates.extend(Path(m) for m in glob.glob(pattern))
    found = [c.resolve() for c in candidates if c.is_file()]
    if not found:
        return None
    return max(found, key=lambda p: p.stat().st_mtime)


def strip_dump_extensions(name):
    """Derive the base name for a dump, stripping .zip and .ppdmp extensions."""
    base = name
    lower = base.lower()
    if lower.endswith(".zip"):
        base = base[:-4]
        lower = base.lower()
    if lower.endswith(".ppdmp"):
        base = base[:-6]
    return base


def collect_dumps(test_root):
    """Recursively collect frame dumps (.ppdmp files and .zip wrappers)."""
    dumps = []
    for dirpath, dirnames, filenames in os.walk(test_root):
        dirnames[:] = [d for d in sorted(dirnames) if not d.startswith(".")]
        for filename in sorted(filenames):
            lower = filename.lower()
            if lower.endswith(".ppdmp") or lower.endswith(".zip"):
                dumps.append(Path(dirpath) / filename)
    return sorted(dumps)


def run_test(headless, dump_path, variant_args, ref_path, actual_path, diff_path, max_mse, timeout, output_dir):
    """Run one dump with one variant's args. Returns (status, mse, output, timed_out)."""
    generate = not ref_path.exists()
    args = [str(headless)] + list(variant_args)
    # A memory stick of the tests' own, so the replays don't share shader caches with the user's games.
    if not any(a.startswith("--memstick") for a in args):
        args.append("--memstick=" + str(Path(headless).resolve().parent / "memstick"))
    if generate:
        args.append("--screenshot-save=" + str(ref_path))
    else:
        args.extend([
            "--screenshot-save=" + str(actual_path),
            "--screenshot=" + str(ref_path),
            "--max-mse=" + str(max_mse),
            "--screenshot-diff=" + str(diff_path),
        ])
    args.append(str(dump_path))

    timed_out = False
    try:
        proc = subprocess.Popen(
            args, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
            cwd=str(output_dir), text=True, encoding="utf-8", errors="replace")
        output, _ = proc.communicate(timeout=timeout)
    except subprocess.TimeoutExpired:
        proc.kill()
        output, _ = proc.communicate()
        timed_out = True
    except OSError as e:
        # e.g. the binary couldn't be launched at all.
        return STATUS_ERROR, None, "Failed to launch headless binary: %s\n" % e, False
    returncode = proc.returncode

    # Headless may drop these in the cwd on failure (outside GITHUB_ACTIONS).
    for stray in ("__testfailure.bmp", "__testcompare.png"):
        try:
            (output_dir / stray).unlink()
        except OSError:
            pass

    match = MSE_RE.search(output)
    mse = float(match.group(1)) if match else None

    if timed_out:
        return STATUS_FAIL, mse, output, True
    if generate:
        if returncode != 0 or not ref_path.exists():
            return STATUS_ERROR, mse, output, False
        return STATUS_NEW, mse, output, False
    if returncode != 0:
        return STATUS_FAIL, mse, output, False
    return STATUS_PASS, mse, output, False


class AdbDevice:
    """An Android device running ppsspp_headless over adb, from a directory under /data/local/tmp."""

    def __init__(self, serial, device_dir):
        self.serial = serial
        self.dir = device_dir.rstrip("/")

    def adb(self, *args, timeout=None, check=True):
        cmd = ["adb"] + (["-s", self.serial] if self.serial else []) + list(args)
        proc = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=timeout,
                              text=True, encoding="utf-8", errors="replace")
        if check and proc.returncode != 0:
            raise RuntimeError("%s failed:\n%s" % (" ".join(cmd), proc.stdout))
        return proc

    def shell(self, command, timeout=None, check=True):
        return self.adb("shell", command, timeout=timeout, check=check)

    def setup(self, binary, assets_dir, test_root):
        """Push the binary, the assets and the dumps. --sync skips files that are already there."""
        self.shell("mkdir -p %s" % shlex.quote(self.dir))
        self.adb("push", str(binary), self.dir + "/ppsspp_headless")
        self.shell("chmod 755 %s/ppsspp_headless" % shlex.quote(self.dir))
        self.adb("push", "--sync", str(assets_dir) + "/.", self.dir + "/assets")
        self.adb("push", "--sync", str(test_root) + "/.", self.dir + "/dumps")
        self.shell("rm -rf %s/shots && mkdir -p %s/shots" % (shlex.quote(self.dir), shlex.quote(self.dir)))
        model = self.shell("getprop ro.product.model").stdout.strip()
        soc = self.shell("getprop ro.soc.model", check=False).stdout.strip()
        # SurfaceFlinger names the GPU, except where it reports its own render engine instead (Samsung's Skia).
        gles = self.shell("dumpsys SurfaceFlinger | grep GLES", check=False).stdout.strip()
        gpu = next((l.strip() for l in gles.splitlines() if re.search(r"Adreno|Mali|Immortalis|PowerVR|Xclipse", l)), "")
        return ", ".join(s for s in (model, soc, gpu) if s)


class IosDevice:
    """An iPhone or iPad running PPSSPPHeadless as an app (ios/HeadlessMain.mm), driven with devicectl.
    Dumps go in the app's Documents folder. One launch renders a batch of dumps (--screenshot-dir),
    since each devicectl call costs a few seconds."""

    BUNDLE_ID = "org.ppsspp.headless"
    BATCH = 100

    def __init__(self, device):
        self.device = device

    def devicectl(self, *args, timeout=None, check=True):
        cmd = ["xcrun", "devicectl"] + list(args)
        proc = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=timeout,
                              text=True, encoding="utf-8", errors="replace")
        if check and proc.returncode != 0:
            raise RuntimeError("%s failed:\n%s" % (" ".join(cmd), proc.stdout))
        return proc

    def container_args(self):
        return ["--device", self.device, "--domain-type", "appDataContainer", "--domain-identifier", self.BUNDLE_ID]

    def find_device(self):
        """The device to use: the one asked for, or the only connected iPhone/iPad. Returns a description."""
        import tempfile
        with tempfile.TemporaryDirectory() as tmp:
            path = os.path.join(tmp, "devices.json")
            self.devicectl("list", "devices", "--json-output", path, "--quiet")
            with open(path, "r", encoding="utf-8") as f:
                devices = json.load(f)["result"]["devices"]
        candidates = []
        for d in devices:
            hw = d.get("hardwareProperties", {})
            props = d.get("deviceProperties", {})
            conn = d.get("connectionProperties", {})
            if hw.get("reality") != "physical" or hw.get("platform") != "iOS":
                continue
            names = (hw.get("udid"), d.get("identifier"), props.get("name"))
            if self.device and self.device not in names:
                continue
            if conn.get("tunnelState") == "unavailable":
                continue
            desc = "%s, iOS %s" % (hw.get("marketingName") or hw.get("productType"), props.get("osVersionNumber", "?"))
            candidates.append((hw.get("udid"), desc))
        if len(candidates) != 1:
            raise RuntimeError("expected one connected iOS device%s, found %d: %s" % (
                " matching '%s'" % self.device if self.device else "", len(candidates), candidates))
        self.device = candidates[0][0]
        return candidates[0][1]

    def setup(self, app, test_root, sync_dumps):
        desc = self.find_device()
        self.devicectl("device", "install", "app", "--device", self.device, str(app), timeout=600)
        if sync_dumps:
            self.sync_dumps(test_root)
        return desc

    def remote_files(self, subdirectory):
        """{relative path: size} of the files under subdirectory of the app's container, or {} if it's missing."""
        import tempfile
        with tempfile.TemporaryDirectory() as tmp:
            path = os.path.join(tmp, "files.json")
            proc = self.devicectl("device", "info", "files", *self.container_args(), "--subdirectory", subdirectory,
                                  "--json-output", path, "--quiet", check=False, timeout=600)
            if proc.returncode != 0 or not os.path.exists(path):
                return {}
            with open(path, "r", encoding="utf-8") as f:
                files = json.load(f)["result"]["files"]
        return {f["relativePath"]: f["metadata"]["size"] for f in files if not f["resources"]["isDirectory"]}

    def sync_dumps(self, test_root):
        """Copy the dumps that aren't on the device already (by name and size). Nothing is ever deleted:
        devicectl can't, and its --remove-existing-content empties all of Documents. Dumps left over from
        another test set do no harm, since only the ones asked for run."""
        remote = self.remote_files("Documents/dumps")
        local = {p.relative_to(test_root).as_posix(): p.stat().st_size for p in test_root.rglob("*") if p.is_file()}
        changed = sorted(rel for rel, size in local.items() if remote.get(rel) != size)
        if not changed:
            print("The dumps are already on the device.")
        elif len(changed) > 20:
            # Each copy costs a second or so, so many files go faster as one folder.
            print("Copying all %d files under %s (%d new or changed)..." % (len(local), test_root, len(changed)))
            self.devicectl("device", "copy", "to", *self.container_args(), "--source", str(test_root),
                           "--destination", "Documents/dumps", timeout=7200)
        else:
            print("Copying %d new or changed dumps..." % len(changed))
            for rel in changed:
                self.devicectl("device", "copy", "to", *self.container_args(), "--source", str(test_root / rel),
                               "--destination", "Documents/dumps/" + rel, timeout=600)

    def run_batch(self, variant, variant_args, rels, timeout, local_dir, log):
        """Render rels (dump paths relative to the test root) with variant_args. Screenshots end up in
        local_dir/dumps/<rel>.png. A dump that crashes the app ends its batch, so the rest are retried
        in a new launch. log receives the console output."""
        import tempfile
        remote_shots = "shots/" + variant
        # The first launch empties it, so screenshots from an earlier run can't stand in for missing ones.
        clear = ["--ios-clear=" + remote_shots]
        pending = list(rels)
        while pending:
            batch = pending[:self.BATCH]
            print("  %s: %d dumps left, launching a batch of %d..." % (variant, len(pending), len(batch)))
            command = ["device", "process", "launch", "--device", self.device, "--terminate-existing", "--console",
                       self.BUNDLE_ID] + clear + list(variant_args) + [
                       "--timeout-wall=%g" % timeout, "--screenshot-dir=" + remote_shots] + ["dumps/" + r.as_posix() for r in batch]
            clear = []
            try:
                proc = self.devicectl(*command, timeout=timeout * len(batch) + 60, check=False)
                log.append(proc.stdout)
            except subprocess.TimeoutExpired:
                log.append("devicectl launch timed out\n")
            with tempfile.TemporaryDirectory() as tmp:
                # To a folder that doesn't exist yet, which it becomes.
                dest = os.path.join(tmp, "shots")
                got = self.devicectl("device", "copy", "from", *self.container_args(), "--source", "Documents/" + remote_shots,
                                     "--destination", dest, check=False, timeout=600)
                if got.returncode == 0 and os.path.isdir(dest):
                    shutil.copytree(dest, str(local_dir), dirs_exist_ok=True)
            done = [r for r in batch if (local_dir / "dumps" / (r.as_posix() + ".png")).exists()]
            if not done:
                # The first one took the app down (or every one of them failed); skip it.
                pending = pending[1:]
                continue
            last = batch.index(done[-1])
            # Everything up to the last one that rendered had its turn. Of the rest, the first one is
            # what ended the batch.
            pending = batch[last + 2:] + pending[len(batch):] if last + 1 < len(batch) else pending[len(batch):]


def render_host(headless, host_args, dump_path, host_path, timeout, output_dir):
    """Render dump_path on the host with host_args, unless an earlier variant already did.
    Returns (output, error) where error is None or (status, timed_out, detail)."""
    if host_path.exists():
        return "", None
    args = [str(headless)] + list(host_args) + ["--screenshot-save=" + str(host_path), str(dump_path)]
    try:
        proc = subprocess.run(args, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, cwd=str(output_dir),
                              timeout=timeout, text=True, encoding="utf-8", errors="replace")
    except subprocess.TimeoutExpired:
        return "", (STATUS_FAIL, True, "timed out on the host")
    output = "=== host ===\n" + proc.stdout
    if not host_path.exists():
        return output, (STATUS_ERROR, False, "the host run produced no screenshot")
    return output, None


def compare_with_host(host_path, actual_path, diff_path, compare_args):
    """Compare a device screenshot approximately (Tools/image_compare.py) with the host's. Returns (status, detail)."""
    import image_compare
    a, b, area, blocks, bias = image_compare.compare(image_compare.load(str(host_path)), image_compare.load(str(actual_path)), compare_args)
    if not area.any() and not blocks.any():
        return STATUS_PASS, ""
    image_compare.heatmap(str(diff_path), a, b, area, blocks, compare_args.block)
    return STATUS_FAIL, "%d px, %d/%d blocks differ from the host (bias %+.1f %+.1f %+.1f)" % (
        area.sum(), blocks.sum(), blocks.size, bias[0], bias[1], bias[2])


def run_side_by_side(headless, host_args, device, dump_path, rel, variant_args, host_path, actual_path, diff_path,
                     timeout, output_dir, compare_args):
    """Run one dump on the device, and compare its screenshot approximately (Tools/image_compare.py) with
    the host's, rendered with host_args. The host render is shared by all variants of a dump.
    Returns (status, mse, output, timed_out, detail)."""
    output, error = render_host(headless, host_args, dump_path, host_path, timeout, output_dir)
    if error:
        return error[0], None, output, error[1], error[2]

    shot = "%s/shots/shot.png" % device.dir
    device.shell("rm -f %s" % shlex.quote(shot), check=False)
    command = "cd %s && ./ppsspp_headless %s --screenshot-save=%s %s" % (
        shlex.quote(device.dir), " ".join(shlex.quote(a) for a in variant_args), shlex.quote(shot),
        shlex.quote("dumps/" + rel.as_posix()))
    try:
        proc = device.shell(command, timeout=timeout, check=False)
        output += "\n=== device ===\n" + proc.stdout
    except subprocess.TimeoutExpired:
        device.shell("pkill -f ppsspp_headless", check=False)
        return STATUS_FAIL, None, output, True, "timed out on the device"
    if device.adb("pull", shot, str(actual_path), check=False).returncode != 0 or not actual_path.exists():
        return STATUS_ERROR, None, output, False, "the device run produced no screenshot"

    status, detail = compare_with_host(host_path, actual_path, diff_path, compare_args)
    return status, None, output, False, detail


def ios_side_by_side(headless, host_args, dump_path, shot_path, device_log, host_path, actual_path, diff_path,
                     timeout, output_dir, compare_args):
    """run_side_by_side for a dump the iOS device already rendered, in a batch, to shot_path."""
    output, error = render_host(headless, host_args, dump_path, host_path, timeout, output_dir)
    if error:
        return error[0], None, output, error[1], error[2]
    output += "\n=== device (whole batch) ===\n" + device_log
    if not shot_path.exists():
        return STATUS_ERROR, None, output, False, "the device run produced no screenshot (crashed or timed out?)"
    shutil.copyfile(shot_path, actual_path)
    status, detail = compare_with_host(host_path, actual_path, diff_path, compare_args)
    return status, None, output, False, detail


def image_data_uri(path):
    """Read an image file as a PNG data URI, or None if missing."""
    try:
        data = path.read_bytes()
    except OSError:
        return None
    return "data:image/png;base64," + base64.b64encode(data).decode("ascii")


def write_report(report_path, results, variants):
    rows = []
    for res in results:
        cls = "failed" if res["status"] in (STATUS_FAIL, STATUS_ERROR) else ("new" if res["status"] == STATUS_NEW else "")
        mse = ('<span class="mse">MSE: %.6f</span>' % res["mse"]) if res["mse"] is not None else ""
        detail = '<span class="detail">%s</span>' % html.escape(res["detail"]) if res["detail"] else ""
        ref_img = '<img class="img" src="%s" alt="reference" title="reference">' % res["ref_uri"] if res["ref_uri"] else ""
        actual_img = '<img class="img" src="%s" alt="actual" title="actual">' % res["actual_uri"] if res["actual_uri"] else ""
        diff_img = '<img class="img diff" src="%s" alt="diff" title="diff">' % res["diff_uri"] if res["diff_uri"] else ""
        log = ('<details class="log"><summary>log</summary><pre>%s</pre></details>' % html.escape(res["log"])) if res["log"] else ""
        rows.append("""        <div class="test {cls}">
          <div class="head">
            <span class="status">{status}</span>
            <span class="variant">{variant}</span>
            <span class="name">{name}</span>
            {mse}{detail}
          </div>
          <div class="images">
            {ref_img}{actual_img}{diff_img}
          </div>
          {log}
        </div>""".format(
            cls=cls,
            status=html.escape(res["status"]),
            variant=html.escape(res["variant"]),
            name=html.escape(res["dump_name"]),
            mse=mse,
            detail=detail,
            ref_img=ref_img,
            actual_img=actual_img,
            diff_img=diff_img,
            log=log,
        ))

    counts = {}
    for res in results:
        counts[res["status"]] = counts.get(res["status"], 0) + 1
    summary = ", ".join("%s: %d" % (k, v) for k, v in sorted(counts.items()))

    variant_rows = ""
    for variant in variants:
        var_counts = {}
        for res in results:
            if res["variant"] == variant:
                var_counts[res["status"]] = var_counts.get(res["status"], 0) + 1
        variant_rows += "<tr><td>%s</td><td>%s</td></tr>" % (
            html.escape(variant), html.escape(", ".join("%s: %d" % (k, v) for k, v in sorted(var_counts.items()))))

    report = """<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<title>PPSSPP frametests report</title>
<style>
  body {{ font-family: sans-serif; margin: 1em; }}
  .test {{ border: 1px solid #ccc; border-radius: 4px; margin: 0.5em 0; padding: 0.5em; }}
  .test.failed {{ border-color: #c33; background: #fee; }}
  .test.new {{ border-color: #cc3; background: #ffe; }}
  .head {{ font-weight: bold; }}
  .status {{ color: #393; }}
  .failed .status {{ color: #c33; }}
  .new .status {{ color: #c93; }}
  .variant {{ color: #666; margin: 0 0.5em; }}
  .mse {{ color: #c33; margin-left: 0.5em; }}
  .detail {{ color: #c33; margin-left: 0.5em; }}
  .images {{ margin-top: 0.5em; }}
  .img {{ width: 256px; border: 1px solid #999; margin-right: 0.5em; vertical-align: top; }}
  .img.diff {{ width: 512px; }}
  .log pre {{ white-space: pre-wrap; overflow-x: auto; max-height: 20em; }}
  .failed .img {{ border-color: #c33; }}
  table {{ border-collapse: collapse; }}
  td, th {{ border: 1px solid #ccc; padding: 0.2em 0.6em; }}
</style>
</head>
<body>
<h1>PPSSPP frametests report</h1>
<p>{summary}</p>
<h2>Variants</h2>
<table>
<tr><th>variant</th><th>results</th></tr>
{variant_rows}
</table>
<h2>Results</h2>
{rows}
</body>
</html>""".format(summary=html.escape(summary), variant_rows=variant_rows, rows="\n".join(rows))

    report_path.write_text(report, encoding="utf-8")
    return report_path


def main():
    parser = argparse.ArgumentParser(description="Run PPSSPP framedump rendering tests.")
    parser.add_argument("config", nargs="?", default="frametests/frametests.json",
                        help="Path to the JSON configuration file (default: frametests/frametests.json)")
    parser.add_argument("--filter", default="", help="Only run dumps whose path contains this substring (case-insensitive)")
    parser.add_argument("--strict", action="store_true",
                        help="Treat missing reference images as failures (configuration error)")
    parser.add_argument("--out-mode", choices=["all", "failures"], default="all",
                        help="What to keep in the output directory: everything, or only failures (default: all)")
    parser.add_argument("--adb", nargs="?", const="", default=None, metavar="SERIAL",
                        help="Run each dump on an Android device over adb, and compare its screenshot approximately "
                             "(Tools/image_compare.py) with the host's Vulkan render instead of with the references. "
                             "SERIAL picks a device when several are connected")
    parser.add_argument("--device-headless", default="android/libs/arm64-v8a/ppsspp_headless",
                        help="With --adb: the Android headless binary to push (default: android/libs/arm64-v8a/ppsspp_headless)")
    parser.add_argument("--device-dir", default="/data/local/tmp/ppsspp-frametests",
                        help="With --adb: where on the device the binary, assets and dumps go")
    parser.add_argument("--ios", nargs="?", const="", default=None, metavar="DEVICE",
                        help="Like --adb, on an iPhone or iPad over devicectl, with PPSSPPHeadless built as an app "
                             "(see docs/frametest.md). DEVICE (UDID or name) picks one when several are connected")
    parser.add_argument("--ios-app", default="build-ios-headless/Release-iphoneos/PPSSPPHeadless.app",
                        help="With --ios: the app to install (default: build-ios-headless/Release-iphoneos/PPSSPPHeadless.app)")
    parser.add_argument("--no-sync", action="store_true",
                        help="With --ios: don't copy the dumps over, they're already on the device")
    # Macs have Metal instead of Vulkan.
    default_host_graphics = "metal" if sys.platform == "darwin" else "vulkan"
    parser.add_argument("--host-args", default="--graphics=%s --dump-compat" % default_host_graphics,
                        help="With --adb or --ios: how the host renders the reference each device run is compared with "
                             "(default: --graphics=%s --dump-compat)" % default_host_graphics)
    parser.add_argument("--variants", default="",
                        help="Variants to run, as name=args pairs separated by commas, replacing the config's "
                             "(default with --adb: vulkan=--graphics=vulkan --dump-compat,gl=--graphics=gles --dump-compat)")
    args = parser.parse_args()

    strict = args.strict or bool(os.environ.get("GITHUB_ACTIONS"))

    config_path = Path(args.config)
    if not config_path.is_file():
        print("ERROR: config file not found: %s" % config_path, file=sys.stderr)
        return 2
    config_dir = config_path.parent
    with open(config_path, "r", encoding="utf-8") as f:
        config = json.load(f)

    test_root = (config_dir / config["testRoot"]).resolve()
    ref_root = (config_dir / config["refRoot"]).resolve()
    output_root = (config_dir / config.get("outputRoot", "out")).resolve()
    timeout = float(config.get("timeout", 60))
    max_mse = float(config.get("maxMse", 0.0))
    raw_variants = config.get("variants", {})
    if not args.variants and args.adb is not None:
        # With the game's compat settings on both sides, which is what the game would look like.
        args.variants = "vulkan=--graphics=vulkan --dump-compat,gl=--graphics=gles --dump-compat"
    if not args.variants and args.ios is not None:
        # iOS is Metal only.
        args.variants = "metal=--graphics=metal --dump-compat"
    if args.variants:
        raw_variants = dict(v.split("=", 1) for v in args.variants.split(","))
    if not raw_variants:
        print("ERROR: no variants defined in %s" % config_path, file=sys.stderr)
        return 2

    # Each variant has a suffix (used for its output images) and a compare-suffix
    # (the reference image it compares against).  Reference images are only
    # generated for variants whose suffix matches their compare-suffix, so that
    # other variants can share a reference (e.g. gl comparing against the soft
    # reference).  A plain string entry means args only, with both suffixes
    # defaulting to the variant name.
    variants = {}
    for key, entry in raw_variants.items():
        if isinstance(entry, str):
            args_str = entry
            suffix = key
        else:
            args_str = entry.get("args", "")
            suffix = entry.get("suffix", key)
        compare_suffix = entry.get("compare-suffix", suffix) if not isinstance(entry, str) else suffix
        variants[key] = {"args": shlex.split(args_str), "suffix": suffix, "compare_suffix": compare_suffix}

    headless = find_headless(config_dir, config.get("headlessPath", ""))
    if headless is None:
        print("ERROR: PPSSPPHeadless binary not found. Set 'headlessPath' in %s or PPSSPP_HEADLESS, or run from the repo root." % config_path, file=sys.stderr)
        return 2

    if not test_root.is_dir():
        print("ERROR: test root not found: %s (check 'testRoot' in %s)" % (test_root, config_path), file=sys.stderr)
        return 2

    dumps = collect_dumps(test_root)
    if not dumps:
        print("ERROR: no frame dumps found under %s" % test_root, file=sys.stderr)
        return 2

    if args.filter:
        filter_lower = args.filter.lower()
        dumps = [d for d in dumps if filter_lower in str(d.relative_to(test_root)).lower()]

    device = None
    ios = None
    compare_args = None
    if args.adb is not None or args.ios is not None:
        sys.path.insert(0, str(Path(__file__).resolve().parent / "Tools"))
        import image_compare
        compare_args = argparse.Namespace(tolerance=12, radius=1, min_neighbors=5, block=8, block_tolerance=8.0, crop=(480, 272))
        shutil.rmtree(output_root / "host", ignore_errors=True)
    if args.ios is not None:
        ios_app = Path(args.ios_app)
        if not ios_app.is_dir():
            print("ERROR: iOS headless app not found: %s" % ios_app, file=sys.stderr)
            return 2
        ios = IosDevice(args.ios)
        print("Installing %s%s..." % (ios_app, "" if args.no_sync else " and copying the dumps"))
        try:
            print("Device: %s" % ios.setup(ios_app, test_root, not args.no_sync))
        except (RuntimeError, OSError, subprocess.TimeoutExpired) as e:
            print("ERROR: %s" % e, file=sys.stderr)
            return 2
    if args.adb is not None:
        device_headless = Path(args.device_headless)
        if not device_headless.is_file():
            print("ERROR: Android headless binary not found: %s" % device_headless, file=sys.stderr)
            return 2
        device = AdbDevice(args.adb, args.device_dir)
        print("Pushing %s, assets and dumps to %s..." % (device_headless, device.dir))
        try:
            print("Device: %s" % device.setup(device_headless, Path(__file__).resolve().parent / "assets", test_root))
        except (RuntimeError, OSError) as e:
            print("ERROR: %s" % e, file=sys.stderr)
            return 2

    log_dir = output_root / "logs"
    diff_dir = output_root / "diffs"
    actual_dir = output_root / "actuals"
    generated_dir = output_root / "generated"
    for d in (log_dir, diff_dir, actual_dir, generated_dir):
        d.mkdir(parents=True, exist_ok=True)

    print("Headless: %s" % headless)
    print("Test root: %s" % test_root)
    variant_desc = []
    for key, v in variants.items():
        if v["compare_suffix"] == v["suffix"]:
            variant_desc.append(key)
        else:
            variant_desc.append("%s (ref: %s)" % (key, v["compare_suffix"]))
    print("Variants: %s" % ", ".join(variant_desc))
    print("Running %d dumps..." % len(dumps))

    results = []
    failures = 0
    errors = 0
    new_refs = 0
    start_time = time.time()

    # The device renders every dump of a variant up front, in batches.
    ios_shots = output_root / "ios-shots"
    ios_logs = {}
    if ios:
        shutil.rmtree(ios_shots, ignore_errors=True)
        rels = [d.relative_to(test_root) for d in dumps]
        for variant, vinfo in variants.items():
            log = []
            ios.run_batch(variant, vinfo["args"], rels, timeout, ios_shots / variant, log)
            ios_logs[variant] = "".join(log)

    for dump in dumps:
        rel = dump.relative_to(test_root)
        base = strip_dump_extensions(dump.name)
        rel_dir = rel.parent
        for variant, vinfo in variants.items():
            variant_args = vinfo["args"]
            generate_refs = vinfo["suffix"] == vinfo["compare_suffix"]
            ref_path = ref_root / rel_dir / ("%s-%s.png" % (base, vinfo["compare_suffix"]))
            actual_path = actual_dir / rel_dir / ("%s-%s.png" % (base, variant))
            diff_path = diff_dir / rel_dir / ("%s-%s.png" % (base, variant))
            log_path = log_dir / rel_dir / ("%s-%s.log" % (base, variant))
            for p in (ref_path, actual_path, diff_path, log_path):
                p.parent.mkdir(parents=True, exist_ok=True)

            remote_detail = None
            if ios:
                ref_path = output_root / "host" / rel_dir / ("%s.png" % base)
                ref_path.parent.mkdir(parents=True, exist_ok=True)
                shot_path = ios_shots / variant / "dumps" / (rel.as_posix() + ".png")
                status, mse, output, timed_out, remote_detail = ios_side_by_side(
                    headless, shlex.split(args.host_args), dump, shot_path, ios_logs[variant], ref_path, actual_path,
                    diff_path, timeout, output_root, compare_args)
            elif device:
                # The host's screenshot is the reference, so the exact references aren't involved.
                ref_path = output_root / "host" / rel_dir / ("%s.png" % base)
                ref_path.parent.mkdir(parents=True, exist_ok=True)
                status, mse, output, timed_out, remote_detail = run_side_by_side(
                    headless, shlex.split(args.host_args), device, dump, rel, variant_args, ref_path, actual_path, diff_path,
                    timeout, output_root, compare_args)
            elif not ref_path.exists() and not generate_refs:
                status = STATUS_ERROR
                mse = None
                output = ""
                timed_out = False
            else:
                status, mse, output, timed_out = run_test(
                    headless, dump, variant_args, ref_path, actual_path, diff_path,
                    max_mse, timeout, output_root)

            detail = ""
            if remote_detail is not None:
                detail = remote_detail
            elif status == STATUS_ERROR and not ref_path.exists() and not generate_refs:
                detail = "no reference image '%s' available for this variant (references are only generated for variants whose suffix matches their compare-suffix)" % vinfo["compare_suffix"]
            elif status == STATUS_FAIL:
                if timed_out:
                    detail = "timed out"
                elif mse is None:
                    if "Unable to read screenshot" in output:
                        status = STATUS_ERROR
                        detail = "reference image could not be loaded (corrupt or unreadable)"
                    else:
                        detail = "no screenshot MSE reported"
                else:
                    detail = "MSE %.6f exceeds maximum %.6f" % (mse, max_mse)
            if status == STATUS_ERROR and not detail:
                detail = "headless failed or produced no reference image"
            if status == STATUS_NEW:
                detail = "reference image generated"

            # Track counters and emit GitHub Actions annotations.
            if status == STATUS_FAIL:
                failures += 1
                if os.environ.get("GITHUB_ACTIONS"):
                    print("::error file=%s::%s failed (%s)" % (rel, variant, detail))
            elif status == STATUS_ERROR:
                errors += 1
                if os.environ.get("GITHUB_ACTIONS"):
                    print("::error file=%s::%s errored (%s)" % (rel, variant, detail))
            elif status == STATUS_NEW:
                new_refs += 1
                gen_path = generated_dir / rel_dir / ("%s-%s.png" % (base, vinfo["compare_suffix"]))
                gen_path.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(ref_path, gen_path)
                if strict:
                    failures += 1
                    if os.environ.get("GITHUB_ACTIONS"):
                        print("::error file=%s::%s missing reference image (config error; generated one saved to %s)" % (rel, variant, gen_path))

            # Keep artifacts (log, actual, diff) for everything in "all" mode,
            # or only for failures/errors otherwise.
            keep_artifacts = args.out_mode == "all" or status in (STATUS_FAIL, STATUS_ERROR)
            if keep_artifacts:
                log_path.write_text(output, encoding="utf-8", errors="replace")
            else:
                for p in (log_path, actual_path, diff_path):
                    try:
                        p.unlink()
                    except OSError:
                        pass

            if status != STATUS_PASS:
                print("[%s] %s (%s)%s" % (status, rel, variant, " - " + detail if detail else ""))

            # In "failures" mode, only embed images for non-passing tests to
            # keep the report (and thus the CI artifact) small.
            ref_uri = None
            if args.out_mode == "all" or status != STATUS_PASS:
                if "could not be loaded" not in detail:
                    ref_uri = image_data_uri(ref_path)
            results.append({
                "dump_name": str(rel),
                "variant": variant,
                "status": status,
                "mse": mse,
                "detail": detail,
                "log": output if keep_artifacts and len(output) < LOG_EMBED_LIMIT else "",
                "ref_uri": ref_uri,
                "actual_uri": image_data_uri(actual_path) if actual_path.exists() else None,
                "diff_uri": image_data_uri(diff_path) if diff_path.exists() else None,
            })

    elapsed = time.time() - start_time
    print("Done in %.1fs: %d tests, %d failures, %d errors, %d new references." % (
        elapsed, len(results), failures, errors, new_refs))

    report_path = output_root / "report.html"
    write_report(report_path, results, list(variants.keys()))
    print("Report written to: %s" % report_path)

    if failures or errors:
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
