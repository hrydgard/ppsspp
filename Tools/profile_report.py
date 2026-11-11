#!/usr/bin/env python3
"""Summarizes a frame profiler log, as written with --profile-log=FILE.

The log has a JSON line per frame, with each thread's time and count per category, and for each slow
frame (--profile-hitch-ms) a line per scope of at least --profile-span-ms that ended in it, with the
scope's own time (minus the logged scopes inside it) and its label (which syscall, event, module...).

  python Tools/profile_report.py profile.jsonl            # summary, causes and the worst frames
  python Tools/profile_report.py profile.jsonl --frame 1234   # one frame's scopes
"""

import argparse
import json
import sys
from collections import defaultdict

# Waiting, not working. Left out when naming what made a frame slow, but shown in the details.
WAIT_CATEGORIES = ("sleep",)


def load(path):
    frames = {}
    spans = defaultdict(list)
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            try:
                rec = json.loads(line)
            except json.JSONDecodeError:
                continue  # A line cut short at the end of a run.
            if "span" in rec:
                spans[rec["span"]].append(rec)
            elif "f" in rec:
                frames[rec["f"]] = rec
    return frames, spans


def percentile(values, p):
    if not values:
        return 0.0
    values = sorted(values)
    return values[min(len(values) - 1, int(len(values) * p / 100))]


def span_name(s):
    return s["c"] + (":" + s["n"] if s.get("n") else "")


def is_wait(s):
    return s["c"] in WAIT_CATEGORIES


def emu_thread(frame):
    """The thread running the game: the one with JIT (or interpreter) time."""
    for thread, cats in frame.get("th", {}).items():
        if "jit" in cats or "advance" in cats:
            return thread
    return None


VBLANK_MS = 1000.0 / 59.94
# A frame this far behind the emulated time it covered is a stall: half a vblank.
STALL_MS = 8.0
# Frame details show the scopes with this much work of their own.
DETAIL_MS = 1.0
# The threads that a frame waits for, other than the emulator thread. Vulkan's is "VulkanRenderMan".
RENDER_THREAD_PREFIX = "VulkanRender"


def behind_ms(frame):
    """How much longer the frame took than the emulated time it covered: the PSP takes a vblank
    (16.7 ms) per vblank, so a game that doesn't flip for ten of them (loading, say) shows a 167 ms
    frame on the PSP too. A frame without vblanks covered no emulated time (a menu, say)."""
    thread = emu_thread(frame)
    vblanks = frame["th"][thread].get("vblank", [0.0, 0])[1] if thread else 0
    return frame["ms"] - vblanks * VBLANK_MS


def culprits(frame_spans):
    """The frame's working scopes, by their own time, longest first."""
    return sorted((s for s in frame_spans if not is_wait(s)), key=lambda s: -s["self"])


def print_frame(frame, frame_spans, out):
    out.write("Frame %d at %.2f s: %.1f ms (typically %.1f), %.1f ms behind emulated time\n" % (frame["f"], frame["t"], frame["ms"], frame.get("typ", 0.0), behind_ms(frame)))
    for thread, cats in frame.get("th", {}).items():
        busy = sorted(cats.items(), key=lambda kv: -kv[1][0])
        parts = ["%s %.1f%s" % (c, v[0], " x%d" % v[1] if v[1] > 1 else "") for c, v in busy if v[0] >= 0.1]
        if parts:
            out.write("  %-16s %s\n" % (thread, ", ".join(parts)))
    by_thread = defaultdict(list)
    for s in frame_spans:
        by_thread[s["th"]].append(s)
    # Times from the frame's start. A scope that began in an earlier frame starts below zero.
    start = frame["t"] - frame["ms"] / 1000.0
    for thread, ss in by_thread.items():
        ss = sorted(ss, key=lambda s: (s["t"], s["l"]))
        # The scopes that did work themselves, and the ones around them.
        keep = set(i for i, s in enumerate(ss) if s["self"] >= DETAIL_MS and not is_wait(s))
        for i in list(keep):
            inner = ss[i]
            for j, outer in enumerate(ss):
                if outer["l"] < inner["l"] and outer["t"] <= inner["t"] and outer["t"] + outer["ms"] / 1000.0 >= inner["t"] + inner["ms"] / 1000.0 - 1e-6:
                    keep.add(j)
        if not keep:
            continue
        out.write("  %s scopes with %.0f ms or more of their own work (start, then length and own time):\n" % (thread, DETAIL_MS))
        for i, s in enumerate(ss):
            if i in keep:
                out.write("    %+8.1f ms %s%s %.1f ms (own %.1f)\n" % ((s["t"] - start) * 1000.0, "  " * s["l"], span_name(s), s["ms"], s["self"]))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("log")
    ap.add_argument("--hitch-ms", type=float, default=None, help="Count frames of at least this long as slow (default: the logged threshold)")
    ap.add_argument("--since", type=float, default=0.0, help="Leave out frames before this many seconds")
    ap.add_argument("--worst", type=int, default=5, help="Show the scopes of this many of the slowest frames")
    ap.add_argument("--causes", type=int, default=25, help="Show this many causes")
    ap.add_argument("--frame", type=int, action="append", help="Show this frame's scopes (repeatable)")
    args = ap.parse_args()

    frames, spans = load(args.log)
    out = sys.stdout
    if args.frame:
        for n in args.frame:
            if n in frames:
                print_frame(frames[n], spans.get(n, []), out)
            else:
                out.write("No frame %d\n" % n)
        return

    chosen = [f for f in frames.values() if f["t"] >= args.since]
    if not chosen:
        out.write("No frames.\n")
        return
    ms = [f["ms"] for f in chosen]
    if args.hitch_ms is not None:
        slow = [f for f in chosen if f["ms"] >= args.hitch_ms]
        rule = "of %.1f ms or more" % args.hitch_ms
    else:
        # As logged: slow in absolute terms, and at least 1.5x the frames around it ("typ").
        slow = [f for f in chosen if f.get("slow")]
        rule = "logged as slow"
    # A slow frame that covered as much emulated time is the game not flipping, as slow on a PSP
    # (a loading screen, say). The rest are stalls: emulation fell behind real time.
    paced = [f for f in slow if behind_ms(f) < STALL_MS]
    slow = [f for f in slow if behind_ms(f) >= STALL_MS]
    slow.sort(key=lambda f: -f["ms"])
    duration = chosen[-1]["t"] - chosen[0]["t"]
    out.write("%d frames over %.1f s. Frame time median %.1f ms, 95%% %.1f, 99%% %.1f, max %.1f.\n" % (
        len(chosen), duration, percentile(ms, 50), percentile(ms, 95), percentile(ms, 99), max(ms)))
    scopes = [sum(v[1] for cats in f.get("th", {}).values() for v in cats.values()) for f in chosen]
    out.write("Scopes per frame (all threads): median %d, max %d.\n" % (percentile(scopes, 50), max(scopes)))
    stalled = sum(behind_ms(f) for f in slow)
    out.write("%d stalls (frames %s that fell %.0f ms or more behind emulated time), %.2f s behind in all.\n" % (len(slow), rule, STALL_MS, stalled / 1000.0))
    out.write("%d more slow frames took as long as the vblanks they covered (the game not flipping, as on a PSP).\n" % len(paced))

    # What the stalls' time went to on the threads a frame waits for: each category's own time, from
    # the exact per-frame totals. Worker threads run alongside, and show up here as wait_* when waited
    # for. A stall's "cause" is its biggest category.
    cat_ms = defaultdict(float)
    cat_frames = defaultdict(int)
    first_cause = defaultdict(int)
    for f in slow:
        here = defaultdict(float)
        for thread, cats in f.get("th", {}).items():
            if thread != emu_thread(f) and not thread.startswith(RENDER_THREAD_PREFIX):
                continue
            for c, v in cats.items():
                if c not in WAIT_CATEGORIES:
                    here["%s %s" % (thread, c)] += v[0]
        for k, v in here.items():
            cat_ms[k] += v
            cat_frames[k] += 1
        if here:
            first_cause[max(here.items(), key=lambda kv: kv[1])[0]] += 1
    if cat_ms:
        out.write("\nTime in stalls by thread and category (own time):\n")
        out.write("  %9s %6s %6s  %s\n" % ("ms", "frames", "worst", "category"))
        for name, total in sorted(cat_ms.items(), key=lambda kv: -kv[1])[:args.causes]:
            out.write("  %9.1f %6d %6d  %s\n" % (total, cat_frames[name], first_cause.get(name, 0), name))
        out.write("  (worst: how many stalls it was the biggest category of)\n")

    # Which syscall, event, module or pipeline: the labelled scopes, on any thread.
    own = defaultdict(float)
    count = defaultdict(int)
    for f in slow:
        for s in spans.get(f["f"], []):
            if s.get("n") and not is_wait(s):
                own[span_name(s)] += s["self"]
                count[span_name(s)] += 1
    if own:
        out.write("\nLabelled scopes in stalls (own time, scopes of at least the logged length, any thread):\n")
        out.write("  %9s %6s  %s\n" % ("ms", "times", "scope"))
        for name, total in sorted(own.items(), key=lambda kv: -kv[1])[:args.causes]:
            out.write("  %9.1f %6d  %s\n" % (total, count[name], name))

    for f in slow[:args.worst]:
        out.write("\n")
        print_frame(f, spans.get(f["f"], []), out)


if __name__ == "__main__":
    main()
