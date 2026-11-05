# Testing a game's ad hoc multiplayer

Several headless instances on one machine can play a game's multiplayer against each other: one
of them runs the built-in ad hoc server, and the others log in to it at 127.0.0.1. For the API itself,
the pspautotests pair tests are the tool (`Tools/adhoc_multi.py`, see [pspautotests.md](pspautotests.md));
this is for commercial games.

## The quick way: Tools/adhoc_session.py

```bash
python3 Tools/adhoc_session.py start "$HOME/PSP/Tekken 6 (Europe).iso" -n 2
python3 Tools/adhoc_session.py run tekken6_lobby.txt     # a script for this game, see below
python3 Tools/adhoc_session.py run tekken6_fight.txt
python3 Tools/adhoc_session.py shot now      # NAME_1.png, NAME_2.png and all side by side in NAME.png
python3 Tools/adhoc_session.py stats         # emulated time and frame rate of each instance
python3 Tools/adhoc_session.py stop
```

`start` launches N instances of the newest `build*/PPSSPPHeadless`, running freely at PSP speed, each
with a WebSocket debugger port. Instance 1 also runs the ad hoc server. The other commands talk to the
instances over the debugger: `press BUTTON [-i 1,2] [--frames N] [--times N --every SECONDS]`, `stick X Y [--seconds S]` (the left analog stick), `type TEXT` (the system on-screen keyboard's first page, just after it opens), `wait SECONDS`, `shot NAME`,
`stats`, and `run SCRIPT`, which runs a file of those commands, one per line (`#` starts a comment).
The session (ports, logs, screenshots, memory sticks) lives in `$TMPDIR/ppsspp-adhoc-session`, or
wherever `--dir` says.

What `start` sets up, and why:

- **Each instance gets its own memory stick**, with a copy of the real one's savedata
  (`~/.config/ppsspp/PSP/SAVEDATA`, or `--memstick`). Games autosave after a match, and instances
  sharing one would write over each other's saves, and yours. The firmware still comes from the real
  memory stick (`--nand`), so firmware modules run as LLE, like in any game run.
- **`--realtime --lag-sync`.** Without lag sync, emulation runs each frame in a burst and then waits,
  and data that arrives in between waits for the next frame: a round trip between two instances took
  15-31 ms instead of about 3 ms. `--no-lag-sync` turns it off, to see what the app's default does.
- **The instances keep in step in emulated time** (`--sync-file`, `--sync-count`): at every emulated
  millisecond each one waits until all the others have got there too, through a small file they all
  map. Without it, an instance the machine runs slower than the others falls behind, and anything
  timed across instances (a timeout, a peer leaving) comes out differently with the load. Any number
  of instances can share the file. One that stops moving (paused in a debugger, or crashed) is waited
  for 2 seconds, then left out until it moves again. `--no-sync` turns it off; `Tools/adhoc_multi.py`,
  which runs the pair tests, syncs the same way.
- **Relay mode** by default, since most players use it. `--p2p` makes the instances talk directly
  instead (see below).
- **Each instance has its own nickname** (`--nickname`), "PPSSPP 1", "PPSSPP 2" and so on, or what
  `--names A,B,...` says, so games that show it in their lobbies tell the instances apart.
- `--state FILE` loads a savestate in every instance at boot. A state only loads into a build with the
  same savestate sections, so a state made on a branch with ad hoc changes may not load on master.

Screenshots pause each instance for a moment, which is harmless. A long pause isn't: a game whose peer
stops sending for a few seconds drops it, as it would drop a paused PSP.

### Writing a script for a new game

Find the way by hand first: `start`, then `press` and `shot` one step at a time, and look at each
screenshot. Then write the steps down in a file, `<game>_<stage>.txt`, with a comment saying where it
starts and ends. The scripts for particular games aren't kept in this repo.

Fixed `wait`s (wall-clock seconds, which with lag sync are emulated seconds too) are fine for menus
that react at once. Loading screens and intro movies take a different time on every run, though, and a
press that comes before a menu is ready is simply lost, so wait for the screen instead:

```
until title.png --box 130,170 --press start --every 3   # press START every 3 s until the region matches
until --pixel 20,30 --color ff0000 -i 2                  # or until one pixel has that color
```

`until` takes screenshots until each selected instance shows the reference image at `--box` (a mean
difference of up to `--tolerance` per channel, 12 by default, so moving backgrounds still match). When
it gives up (`--timeout`, 60 s), it saves the screens it saw as `until_failed_N.png`. Make references
with `crop SHOT.png X,Y,W,H OUT.png` from a screenshot of the screen you want, and keep them next to the
script: paths in a script are relative to it. A title screen left alone too long may start an attract
demo, so don't wait for one without pressing.

To see what a game does, `log sceNet debug` turns a log channel up, and `watch SECONDS NAME` collects
each instance's log messages meanwhile into `NAME_N.log` (the debugger only sends log messages to
clients that are connected). That works without `--log`, which slows the instances down enough to
throw a script's timing off.

## Setting it up by hand

What `start` does for each instance N (instance 1 also gets `--adhoc-server`):

```bash
build-release/PPSSPPHeadless --graphics=vulkan --memstick=MEMSTICK_N --nand=$HOME/.config/ppsspp/PSP/NAND \
    --realtime --lag-sync --instance=N --nickname="PPSSPP N" --appendconfig=network.ini --debugger-run=0 \
    --sync-file=SYNC --sync-count=2 --timeout-wall=3600 GAME.iso
```

`--instance=N` gives the instance its own MAC address and its own address, 127.0.0.N, for ad hoc data.
`SYNC` is a file of 4096 zero bytes, made before the instances start and shared by all of them.
`network.ini` points it at the local server:

```ini
[Network]
proAdhocServer = 127.0.0.1
AdhocServerRelayMode = 1
```

### With the relay

`AdhocServerRelayMode = 1`. The built-in server also runs the relay (aemu_postoffice, on TCP port
27313 next to the ad hoc server's 27312), and all game data goes through it over TCP. Nothing else is
needed; this works on any machine.

### Without the relay (P2P)

`AdhocServerRelayMode = 0`. The instances send game data straight to each other, from and to their
127.0.0.N addresses. Linux routes all of 127.0.0.0/8 to the loopback interface already. macOS only has
127.0.0.1, so each further instance needs an alias first:

```bash
sudo ifconfig lo0 alias 127.0.0.2 up
sudo ifconfig lo0 alias 127.0.0.3 up
```

`adhoc_session.py start --p2p` checks for them and prints these commands if any are missing.

## Simulating a slow network

Three options delay or drop the ad hoc datagrams an instance receives, to see how a game copes with a
distant player or a bad connection. They work the same in relay and P2P mode, and are also in the ini,
under `[Network]`, for the app.

| Option | ini | What it does |
|---|---|---|
| `--adhoc-delay=MS` | `AdhocSimDelayMs` | Each datagram reaches the game MS milliseconds after it arrived. |
| `--adhoc-jitter=MS` | `AdhocSimJitterMs` | Up to MS more, at random. Datagrams stay in order. |
| `--adhoc-loss=PERCENT` | `AdhocSimLossPercent` | Datagrams dropped. |

`adhoc_session.py start` takes them as `--delay`, `--jitter` and `--loss`, and passes them to every
instance; `Tools/adhoc_multi.py` passes them through to the instances it starts. Things to know:

- **Times are emulated**, so a delay is the same at any `--speed`.
- **The delay is one way, on the receiving side.** With it on every instance, a round trip takes twice
  the delay more: `--adhoc-delay=50` turns a 2 ms P2P round trip into 102 ms (pspautotests
  `net/adhoc/pair/timing`).
- **Runs repeat.** Jitter and loss come from random numbers seeded per instance when the game starts ad
  hoc, so with the instances in step (`--sync-file`) the same datagrams are lost each run.
- **PDP** (including matching and game mode): a datagram arrives at the next emulated millisecond after
  it's due, and goes into the socket's buffer then, where it's dropped if it doesn't fit, as on a PSP.
- **PTP** is held on the sending side instead: a send is accepted at once and goes out when due, in
  order, which with the same options on every instance gives the same round trip. Nothing is lost on a
  connection: a "lost" send goes out one retransmit interval (the one given to PtpOpen) later. A flush
  waits for what's held, and GetPtpStat counts it as not yet sent.
- The ad hoc server's own messages (logins, groups, peer lists) aren't delayed.

## Measuring

- `stats` reads each instance's frame rate (`gpu.stats.get`). If vblanks/s is below 60, emulation itself
  is behind: the instances share one GPU, and a GPU-heavy game (Star Wars: The Force Unleashed) can
  hold both back while each is mostly waiting on a fence. Pause the other instance (`kill -STOP`) to
  check whether one alone keeps up. A game that waits for its peer often
  keeps drawing, so this doesn't always show it.
- Game time shows in the game itself: a fighting game's round timer, a race's lap time. Two
  screenshots ten seconds apart that show less than ten seconds of game time passing mean the game
  has been waiting for the network.
- Identical screenshots on both instances at the same moment show that the game stays in sync.
