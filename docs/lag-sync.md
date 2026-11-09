# Lag sync: how it works, and benchmarking it

Lag sync ("Force real clock sync", `bForceLagSync`, `--lag-sync` on the command line) keeps emulated
time in step with the wall clock every emulated millisecond, instead of running a frame flat out and
then waiting at the end of it. Spread-out emulation means lower input, network and audio latency: with
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
Lag sync: 5092 sleeps, 12.63s slept, 1us late on average, 0 resets, 1 fallbacks to bursting (bursting now)
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

## Still to do

- With lag sync pacing, audio arrives about every 1.5 ms rather than once per frame, so the
  resampler's target buffer (`TARGET_BUFSIZE_DEFAULT`, 40 ms, in `Core/HW/StereoResampler.cpp`) could be
  much smaller, going back up on a fallback to bursting.
- Measure on Windows and on Android (where CPU frequency scaling may react to spread-out work), then
  decide whether lag sync should be on by default.
