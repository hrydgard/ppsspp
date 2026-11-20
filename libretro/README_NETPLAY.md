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
by the transport while enabled. Normal in-game memory-stick saves still work;
emulator savestates are disabled in this mode. Restart content after ending a
session, or before switching the option off.

This is a prototype for testing. Passing transport and emulator regression
tests does not establish compatibility with a particular commercial game,
latency, graphics driver, or RetroBat configuration. Infrastructure/Internet
PSP games and interoperability with standalone PPSSPP or real PSPs are outside
this transport. Start with two players and a direct LAN session; test a
RetroArch relay separately after the direct connection works.

## Architecture

`NetpacketTransport` implements the upstream `AdhocTransport` interface from
#22855. The shared PSP HLE continues to own socket validation, blocking waits,
PDP receive queues, GameMode and matching. The new transport owns its endpoint
handles, PTP listener backlogs and paced receive blocks. Connections are
nonblocking; no PSP-side operation waits for a frontend packet callback.

`AdhocControlTransport` supplies discovery, login and the session MAC address.
Its default implementation still uses aemu's PRO server client. The libretro
frontend installs its provider before PSP boot and removes it after networking
threads have joined. The disabled path retains the existing transport and HLE
state layout. No HLE syscall table or serialized field is added. The upstream
GameMode fixes replace the old branch's resend workaround.

`NetpacketAdhoc` owns the thread-safe discovery service, PDP datagrams and PTP
streams. The RetroArch host maintains game/group membership; clients receive
peer events only from the host. MACs derive from frontend-assigned client IDs.
Socket endpoints use monotonically increasing IDs. All wire integers are
explicitly little-endian; packets have a versioned header, size validation,
sender validation, and bounded queues. PTP uses ordered reliable delivery and
a 256 KiB receive window with credit-based backpressure. Datagram queues can
drop excess datagrams, as an unreliable packet service may do.

Emulation/network workers only enqueue messages. `retro_run` and the Netpacket
poll callback drain them; no worker calls frontend functions. PTP connects
return immediately and progress as packets arrive. Stop closes
pending connections without waiting for a frontend callback. The host's local
messages use the same wire decoder as remote messages. Protocol compatibility
is advertised as `ppsspp-adhoc-experimental-2`.

The main regression risks are endpoint ownership during teardown, PSP
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
frontend callback thread affinity, boot/video output, savestate rejection and
shutdown. Point the system directory's `PPSSPP` subdirectory at `assets`:

```sh
build-netpacket-tests/NetpacketCoreSmoke /absolute/path/ppsspp_libretro.so \
  /absolute/path/pspautotests/tests/net/adhoc/self.prx \
  /absolute/path/system /absolute/path/saves
```

Append `disabled` or `unsupported` to test those frontend paths. The harness
also builds on Windows and accepts a DLL instead of a `.so`.

On POSIX, run the public `basic` and `gamemode` pair programs in two separate
core processes, with frontend callbacks carrying packets between them:

```sh
python3 libretro/tests/core_pair.py /absolute/path/ppsspp_libretro.so
```

Each process has its own memory stick. Assertions inspect the PSP programs'
output: group discovery, PDP payloads, PTP connect/send/receive/close/refusal,
and GameMode replica data. This tests the real HLE and the Netpacket adapter;
it does not run RetroArch itself or compare every timing with hardware.
Logs are kept in `build-netpacket-tests/pair-results`. These are targeted payload
and lifecycle assertions, not full `.expected` comparisons: for example, the
PSP GameMode scan mode flag is not yet represented by this discovery protocol.

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
