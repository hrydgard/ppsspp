# Experimental PSP ad hoc over RetroArch Netplay

Enable **RetroArch Netplay (Experimental, Restart)** in the core's Network
options, close content, and restart it. Every participant must use this core
build and the same game revision. Connect the RetroArch Netplay session **before
entering the game's wireless/ad hoc menus**. Each player controls a separate
emulated PSP using local controller port 1.

The option defaults to disabled. It requires a frontend implementing
`RETRO_ENVIRONMENT_SET_NETPACKET_INTERFACE` (environment command 78); an
unsupported frontend rejects content loading when the option is enabled.
WLAN, MAC addresses, PRO server, relay mode, UPnP and port offsets are managed
by the transport while enabled. Normal in-game memory-stick saves still work.
Restart content after ending a session, or before switching the option off.

## Savestates and synchronized starts

A savestate taken while a session runs keeps the game's PDP sockets, its
GameMode areas and its group. Loading it restores them, rejoins the group once
the transport has logged in, and holds emulation until every GameMode member of
the state is connected again, so that the PSPs resume together. RetroArch
refuses to load a state into a connected Netpacket session, so each player
brings their own state (every PSP has its own memory) through the core:

- enable **RetroArch Netplay Start State (Experimental)**;
- put the player's state, taken by RetroArch in the same session (uncompressed),
  at `PPSSPP/netplay/<game ID>.state` in the system directory;
- the core loads it once the game has booted and waits for the other players
  (with a "Waiting for the other players..." message; it resumes anyway after
  five minutes).

MAC addresses derive from frontend client IDs, so each player must load the
state taken by the participant with the same ID (the host first, then the
clients in connection order). Only the session layer comes back: an
established PTP stream does not, so take states in GameMode or between rounds.
States taken outside such a session keep the previous layout, which older
builds still load.

This is a prototype for testing. Passing transport and emulator regression
tests does not establish compatibility with a particular commercial game,
latency, graphics driver, or RetroBat configuration. Infrastructure/Internet
PSP games and interoperability with standalone PPSSPP or real PSPs are outside
this transport. Start with two players and a direct LAN session; test a
RetroArch relay separately after the direct connection works.

## Architecture

`Core/HLE/NetAdhocTransport.h` defaults to the existing aemu relay functions.
The libretro frontend substitutes an in-memory implementation before PSP boot,
stops it before shutdown, and restores the defaults after networking threads
have joined. The disabled path retains the existing transport and HLE state
layout. No HLE syscall table entry is added; the `sceNetAdhoc` savestate section
gains version 11 (the session), written only when a frontend transport runs.

`NetpacketAdhoc` owns the thread-safe discovery service, PDP datagrams and PTP
streams. The RetroArch host maintains game/group membership; clients receive
peer events only from the host. MACs derive from frontend-assigned client IDs.
Socket endpoints use monotonically increasing IDs. All wire integers are
explicitly little-endian; packets have a versioned header, size validation,
sender validation, and bounded queues. PTP uses ordered reliable delivery and
a 256 KiB receive window with credit-based backpressure. Datagram queues can
drop excess datagrams, as an unreliable packet service may do.

Emulation/network workers only enqueue messages. `retro_run` and the Netpacket
poll callback drain them; no worker calls frontend functions. Stop wakes
pending PTP connects without waiting for a frontend callback. The host's local
messages use the same wire decoder as remote messages. Protocol compatibility
is advertised as `ppsspp-adhoc-experimental-1`.

The main regression risks are relay-handle ownership during teardown, PSP
network timing, games caching MAC addresses before session start, and differences
between the frontend's delivery timing and socket-backed aemu. These need real
multiplayer game coverage before enabling the feature by default.

## Tests

Standalone transport tests (no ROM required):

```sh
cmake -S libretro/tests -B build-netpacket-tests
cmake --build build-netpacket-tests
ctest --test-dir build-netpacket-tests --output-on-failure
```

They cover three-player discovery and scans, broadcasts, small receive buffers,
group isolation, rejected senders, client-to-client PTP, partial stream reads,
receive-window backpressure, disconnect, stop/reconnect, malformed packets and
resource limits. They can also be compiled with AddressSanitizer and UBSan.

The optional integration smoke test loads the actual core in a minimal frontend
and boots the repository's `pspautotests/tests/net/adhoc/self.prx`. A synthetic
peer joins its group and must receive its PSP-generated broadcast. It checks
frontend callback thread affinity, boot/video output, a savestate round trip during the session and
shutdown. Point the system directory's `PPSSPP` subdirectory at `assets`:

```sh
build-netpacket-tests/NetpacketCoreSmoke /absolute/path/ppsspp_libretro.so \
  /absolute/path/pspautotests/tests/net/adhoc/self.prx \
  /absolute/path/system /absolute/path/saves
```

Append `disabled` or `unsupported` to test those frontend paths. The harness
also builds on Windows and accepts a DLL instead of a `.so`.

Run the normal repository regression suites as well:

```sh
build/PPSSPPUnitTest all
python test.py -g --graphics=software
```

## RetroBat test installation

Close RetroBat and RetroArch first. Back up the installed
`emulators/retroarch/cores/ppsspp_libretro.dll` and the game's memory-stick saves.
Install the experimental DLL under the same filename and ensure RetroBat's PSP
emulator selection is **LIBRETRO / PPSSPP**. The standalone **PPSSPP** selection
does not load this core. The RetroArch and DLL architectures must both be x64.

Keep the core's required PPSSPP assets in the system directory configured by
RetroArch (usually a `PPSSPP` directory beneath it). Enable the experimental
option on both machines, save core options, fully close content, and relaunch.
Host/join through RetroArch's Netplay menu, then use the game's own ad hoc
host/join menu. Save verbose RetroArch logs from both machines if it fails,
along with the game ID/revision, core version, frontend version, video driver,
and whether the failure was connecting the salon, discovering the PSP player,
or starting the match.

To revert, close RetroArch, restore the original DLL, and disable/remove
`ppsspp_retroarch_netplay` in the core options. Avoid updating the core through
the online updater while testing: it would replace the experimental DLL.
