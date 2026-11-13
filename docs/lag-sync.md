# Lag sync: how it works, and benchmarking it

Lag sync ("Low-latency CPU timing" in the settings, formerly "Force real clock sync"; `bForceLagSync`,
`--lag-sync` on the command line) keeps emulated time in step with the wall clock every emulated
millisecond, instead of running a frame flat out and then waiting at the end of it. Spread-out emulation means lower input, network and audio latency: with
bursts, a whole frame of audio arrives at once, so the audio buffer has to hold a frame plus slack.

The code is `hleLagSync` and `UpdateLagSyncMode` in `Core/HLE/sceDisplay.cpp`.

- Each wait is measured from a fixed base (`lagSyncRealBase`/`lagSyncEmuBase`), so waking up late
  doesn't add up. That's why the sleep doesn't need to spin: it uses `sleep_approx`
  (`Common/TimeUtil.cpp`), which never spins, rather than `sleep_precise`, which on Windows spins the
  last 1.02 ms.
- It only sleeps when at least `LAG_SYNC_MIN_SLEEP` (2 ms) ahead, and asks for `lagSyncWakeLate` less:
  a running average of how late the OS wakes it.
- More than 100 ms behind or ahead, it starts over from now.
- **Fallback to bursting.** Every second of wall time it measures the share of time spent waiting, in
  lag sync or in the frame throttle. Under 10% idle it stops pacing and lets the frame throttle pace
  per frame, exactly as with lag sync off. It goes back after two windows over 25% idle.

## Audio buffer

`StereoResampler` (`Core/HW/StereoResampler.cpp`) keeps its buffer around a target. Bursting delivers
a frame of audio at once (33 ms in a 30 fps game), so the default target is 40 ms. While lag sync
paces (`__DisplayGetLagSyncMode()`), audio arrives every millisecond or two, and the target is the
output's period (`SYSPROP_AUDIO_FRAMES_PER_BUFFER`) plus 192 samples (4.4 ms), or 20 ms where the
platform doesn't report a period, plus room for stalls (below). It never goes over the default. On a
fallback to bursting it goes back to the default, with some underruns while the buffer fills up again.

**Stalls.** Pacing doesn't make every millisecond cost the same: a slow display list can hold up
emulation for several milliseconds on a slow device. Lag sync catches up afterwards (its waits are
measured from a fixed base), but meanwhile the output keeps pulling, so the buffer needs that much
room. Each sync that finds emulation behind the wall clock records by how much, and
`__DisplayGetLagSyncBehindPeak()` is the most over the last five seconds: stalls that recur keep the
room, and the one from loading a state is gone five seconds later. With a fixed 4.4 ms, 6 ms stalls
underran a few times in 15 seconds with the classic resampler and 46 times with granular (3 ms
period); with the room added, neither underran.

The resampler's rate adjustment only drains a full buffer slowly, around ten seconds from a startup
fill down to a small target. So when the lowest level over a quarter second stays more than 5 ms
above the target, it drops the excess in one go. Bursting's sawtooth never does that: its troughs are
below the target.

The granular mixer has no target as such: its queue is capped, and cut back to half the cap when it
overflows, so the level settles somewhere under the cap. The cap makes room for a frame's burst of
audio, which pacing leaves out; it keeps the room for two output reads and adds the room for stalls
there, up to a frame.

The audio debug overlay (and the ImGui debugger's audio window) shows the target in milliseconds, the
lag sync mode with the room for stalls, and how much excess was dropped.

## Benchmarking

What to find out on a platform:

1. **Speed.** Near the host's limit, lag sync must not run slower than bursting (lag sync off).
2. **Cost.** At 1x, how much more CPU lag sync uses than bursting.
3. **Lateness.** How late its sleeps wake up on average (`late` below).
4. **Fallback.** That it falls back when the host is nearly flat out, and stays pacing otherwise.

Use headless with a game and a savestate in real gameplay, Vulkan, a Release build, and the firmware
memstick (see the AGENTS.md section on game runs). Ask the user which game and state to use. Compare the
branch against a build of `master` from the same commit the branch starts from, run one at a time.
`--frame-stats` prints, at the end of the run:

```
Frame stats: 422 flips in 14 emulated seconds, wall 15.1s, per second: 32 30 30 ...
Lag sync: 5092 sleeps, 12.63s slept, 1us late on average, 4 syncs over 3ms behind (54.0ms at most), 0 resets, 1 fallbacks to bursting (bursting now)
```

The second line only appears with lag sync on, and only from the branch build.

### Speed near the host's limit

`--speed=N` (percent, with `--realtime`) runs the game N% faster than a PSP, which is the easy way to
turn a fast host into a slow one. First find the game's ceiling by running it unthrottled (leave out
`--realtime`) for a fixed wall time and dividing emulated seconds by wall seconds. Then run speeds
around it, for example 60%, 80%, 95% and 105% of the ceiling, each with and without `--lag-sync`:

```
PPSSPPHeadless --memstick=<memstick> --graphics=vulkan --realtime --speed=2800 --lag-sync --frame-stats
    --timeout-emulated=1000 --timeout-wall=12 --log <game.iso> --state=<state.ppst>
```

Compare emulated seconds reached in the 12 wall seconds. `--log` shows the mode switches (`Lag sync: 4%
idle, bursting instead`, `... pacing again`). Expected: the same emulated seconds as without lag sync, or
slightly more; staying in pacing mode below about 80% load; falling back above about 90%.

Reference, M1 Mac, GTA VCS (ceiling about 30x): at 15x and 18x both modes reached the same emulated
time and lag sync stayed pacing; at 24x, 28x and 32x it fell back and reached 284/320/330 emulated
seconds against 284/317/319 for bursting.

### Cost at 1x

Run 15 emulated seconds at 1x (`--realtime --timeout-emulated=15`) with and without `--lag-sync`, two or
three times each, alternating, and compare the process's CPU time. On the Mac, `/usr/bin/time -l` gives
user/sys time, instructions and cycles. On Windows, from PowerShell:

```powershell
$p = Start-Process -FilePath $exe -ArgumentList $argList -PassThru -NoNewWindow -RedirectStandardError err.txt
$p.WaitForExit()
"{0:N2}s CPU" -f $p.TotalProcessorTime.TotalSeconds
Select-String -Path err.txt -Pattern 'Frame stats|Lag sync:'
```

Read `TotalProcessorTime` only after `WaitForExit()`. Reference, M1 Mac: bursting about 2.8 s of CPU,
lag sync about 3.2 s (about 1% of a core more), the same for the old and new lag sync.

On Windows, also run `master` with `--lag-sync`: its waits spun (see above), so it should cost clearly
more CPU than the branch. That's the main thing this change is expected to fix there.

### Lateness

`late` is the average of how much later than its goal each sleep woke up, after the `lagSyncWakeLate`
correction, so it should be near 0. The M1 Mac gives 1-10 us. On Windows it depends on whether
`CreateWaitableTimerExW(CREATE_WAITABLE_TIMER_HIGH_RESOLUTION)` worked (Windows 10 1803 and later); with
the fallback `Sleep()`, expect lateness around a millisecond. Lateness much over 1 ms, or many resets,
is worth reporting.

### Things that skew the numbers

- **Background QoS on macOS** (`taskpolicy -c background`) gives timers huge slack: it is not a slow-CPU
  simulation. Bursting lost most of its speed under it (12 emulated seconds in 40), and lag sync woke
  48 ms late on average yet kept up (14 in 15.6), because its waits don't add up.
- The first second includes loading the state and compiling JIT blocks and shaders, and can make the
  first window fall back to bursting. That's expected, and it goes back to pacing two seconds later if
  there's idle time.
- Laptops on battery throttle unpredictably; plug in. Don't run two builds at once.
- **Games diverge.** Pacing for the first second shifts the emulated timeline, and a game can take
  another path from there: from a God of War: Ghost of Sparta state at a quick-time prompt, the paced
  run was in a fight at 60 seconds while the bursting run still waited at the prompt, and it reached
  11% fewer emulated seconds near the ceiling. Starting that run already bursting gave the same speed
  as lag sync off, and a state that doesn't diverge showed no difference. Pick states where nothing
  is about to happen, and compare screenshots when a result looks odd.

### Audio buffer

`--audio-sink=FRAMES` (headless, with `--realtime`) pulls audio through a `StereoResampler` on a steady
clock the way an output device does, FRAMES at 48 kHz at a time, and prints at the end:

```
Audio sink: 1506 pulls of 480 frames, 0 short (0 frames missing), buffer before a pull 11.3/15.9/19.3 ms min/avg/max, target 15.2 ms, 42.3 ms of excess dropped
```

It reports its period as `SYSPROP_AUDIO_FRAMES_PER_BUFFER`, so the target follows it as in the app,
and turns sound on, which changes how the frame throttle waits as it does in the app. Counting starts
two emulated seconds in. Try a 10 ms period (480, a common WASAPI period) and a short one (144),
with and without `--lag-sync`, for 15-20 emulated seconds. A single short pull with a hundred or more
frames missing shows up now and then at any target; it's the host stalling, not the buffer.
`--audio-granular` mixes with the granular mixer instead, and the line then gives the queue's level
and cap, and counts underruns.

The averages include the first five seconds after loading the state, when the room for stalls is
still that of the load, which raises them by a few milliseconds. To simulate a slow device's stalls,
put a temporary `sleep_precise` of a few milliseconds every few hundred calls of `hleLagSync`.

### Reference, Windows

Windows 11, x64 desktop, Vulkan, Release, against `master` from where the branch starts.

- **Speed.** GTA VCS (ceiling about 16x): lag sync reached the same emulated time as bursting at 60%,
  80%, 95% and 105% of the ceiling, paced at 60% and 80% and fell back at 95% and 105%. God of War:
  Ghost of Sparta (a cave state, ceiling about 8.6x): the same at 95% and 105%.
- **Lateness.** 0-2 us at 1x, up to about 100 us while it falls back.
- **Cost at 1x**, CPU seconds for 15 emulated seconds: GTA bursting 1.8, lag sync 1.8, `master`'s lag
  sync 15.6 (spinning a core); God of War bursting 3.7-4.0, lag sync 4.0-4.6, `master`'s 17.
- **Audio.** Average buffer bursting at the default target against lag sync: with a 10 ms period
  38 against 16 ms (GTA) and 35 against 17 ms (God of War), with a 3 ms period 37 against 9 ms and
  34 against 10 ms, all without underruns. Bursting with the small target forced on underran on a
  pull in twelve. These were measured before the room for stalls, which adds a few milliseconds to
  the averages (see above). Granular, GTA: about 40 ms bursting; with lag sync 25.5 and 21.7 ms
  (10 and 3 ms period) while the cap still held a frame, 21.8 and 8.6 ms without it.

## Still to do

- Measure on Android (where CPU frequency scaling may react to spread-out work), then decide whether
  lag sync should be on by default.
- On a fallback to bursting the target goes back up at once, and the buffer underruns for a moment
  while it fills. Raising the target ahead of the switch would avoid that.
- Check the room for stalls on a slow device: how often it gets behind (`--frame-stats` counts the
  syncs more than 3 ms behind; its maximum is usually from loading the state), and whether five
  seconds is long enough to remember stalls between.
