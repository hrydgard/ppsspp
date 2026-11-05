# Decrypting captured PSP ad hoc traffic

A real PSP encrypts its ad hoc Wi-Fi link, so a sniffed capture of two consoles playing is
ciphertext. `Tools/adhoc_decrypt.py` decrypts it and decodes the PDP/PTP/matching framing. This is a
debugging aid: it lets you compare, byte for byte and in timing, what a game actually sends on
hardware against what PPSSPP sends for the same game at the `sceNetAdhoc` HLE level. It is **not**
needed to run ad hoc multiplayer in PPSSPP (PPSSPP never encrypts; it works at the HLE layer), and it
has nothing to do with cross-play - a real PSP will not accept an emulator as a peer, because the
handshake is signed with a per-console key.

The two fixed keys the tool uses already ship in PPSSPP (`ext/libkirk/kirk_engine.c`). The per-session
key is not one of them (see below).

## Capturing

Ad hoc is peer-to-peer, so there is no access point to join - you need a Wi-Fi interface in **monitor
mode** on the consoles' ad hoc channel.

- Set both PSPs to a fixed ad hoc channel (Settings > Network Settings), so you know where to listen.
- Capture raw 802.11 on that channel to a pcap. On macOS, Wireless Diagnostics > Sniffer writes one to
  `/var/tmp`; on Linux, `iw`/`airmon-ng` to put a card in monitor mode, then `tcpdump`/Wireshark. The
  capture must include the radiotap header.
- Not every Wi-Fi chip captures other devices' ad hoc frames in monitor mode; if a capture of a known
  exchange has no `0x88C8` frames between the two PSP MACs, the adapter can't see them.

PPSSPP's ad hoc traffic is **not** encrypted (it is plain UDP/TCP on the relay or P2P), so this tool is
only for captures of real hardware.

## Decrypting with a known key

    Tools/adhoc_decrypt.py CAPTURE.pcap --key HEX

A 16-byte key decrypts; a 20-byte key also verifies the integrity tags. Output is one line per data
frame with the decoded envelope, e.g.

    1.334 57:3f -> e6:7c ctr   7 tag -  PTP  2300->2300 SYN     seq 493060044 len 16 ack/wnd 8192
    1.474 57:3f -> e6:7c ctr  16 tag -  PTP  2300->2300 DATA    seq 0 len 1012
    1.481 e6:7c -> 57:3f ctr  21 tag -  PTP  2300->2300 WND-ACK seq 0 len 16 ack/wnd 7192

Pass `--raw` for plaintext hex instead of the decode. With no `--key`, it decodes the auth handshake,
lists the data frames, and reports which counters both directions used (a hint for key recovery).

## Recovering the key from the capture

The session key is ephemeral - negotiated per ad hoc group membership by an ECDH handshake - so it is
not in the capture, and it is not one of the fixed KIRK keys. But the data cipher has a known-plaintext
weakness, so a capture that contains some guessable plaintext is enough to recover the key, no console
memory needed. (This is not a break of the key exchange.)

Each keystream block is `AES(counter || half-of-key)`, so given a known 16-byte first-block plaintext,
one AES operation yields 8 key bytes, and the frame's own counter (in its cleartext header) confirming
the guess is a 64-bit check. One even-counter frame and one odd-counter frame give the 16-byte key.

**If you know a full 16-byte plaintext block** (pure Python, instant):

    Tools/adhoc_decrypt.py CAPTURE.pcap --known HEX16@COUNTER --known HEX16@COUNTER

Give one `--known` for an even counter and one for an odd counter. It prints the 16-byte key and
decrypts.

**If part of that block is unknown** (e.g. the sequence number), brute the unknown bytes with the C
helper, which is much faster than Python:

    cc -O3 Tools/adhoc_recover.c -o adhoc_recover        # macOS; Linux: add -DUSE_OPENSSL -lcrypto
    ./adhoc_recover half TEMPLATE16 CT16 COUNTER         # "xx" marks each unknown byte (<= ~5)

`TEMPLATE16` is the 32-hex-char first-block plaintext with `xx` for each unknown byte; `CT16` is that
frame's first 16 ciphertext bytes (from the tool's `--raw`/listing). Run it for an even- and an
odd-counter frame and concatenate the two `half:` outputs (even first) to get the 16-byte key.

**What you can usually know.** The ad hoc envelope has constant fields in every frame regardless of
game: byte 0 is the type, byte 1 is `00` (PDP) or a flags byte (PTP), and for PDP bytes 10-11 are
`0000`; the length field is pinned to a 16-byte range by the ciphertext size. The remaining unknowns
are the **ports**, the **sequence**, and the first payload bytes. The cheap way to learn the ports (and
any constant packet header) for a specific game is to run that game in PPSSPP once and note which ports
it opens (`sceNetAdhocPdpCreate` / `sceNetAdhocPtpOpen` in the log) - they are the same on hardware.
With the ports known, only the small sequence field is left to brute, so any past or future capture of
that game then cracks at once.

**Tag verification** (optional) needs the last 4 bytes of the 20-byte key, which only feed the tag.
Brute them once you have the 16-byte key:

    ./adhoc_recover tag KEY16 CT16 COUNTER TAG16         # ~15 s; prints the 20-byte key

## Frame format reference

802.11 data frames, LLC/SNAP, ethertype `0x88C8`. The `0x88C8` payload starts with a 2-byte kind:

- `00 01` **auth handshake** - AES-CBC under a fixed KIRK key, so it always decodes. Carries each
  side's ephemeral ECDH public key and its console certificate; this is what establishes the session
  key and what makes the link console-authenticated.
- `00 02` **data** - `00 02 | counter(8, little-endian) | tag(16) | ciphertext`. The counter counts
  16-byte blocks sent in that direction, cumulative from 0. The tag is the first 16 bytes of
  `SHA1(key20 || counter-header(16) || ciphertext)`.

The decrypted data payload is a PDP or PTP frame. Both start the same: `type(1) | flags-or-00(1) |
length(2, big-endian) | source port(2, BE) | dest port(2, BE)`.

- **PDP** (type `01`): then a 16-bit sequence and `00 00`, then the datagram. A matching-library
  message rides in that payload as `01 | opcode(1) | length(2, BE) | data`.
- **PTP** (type `02`): byte 1 is a flags byte (SYN / SYN-ACK / ACK / DATA / window-ACK), then a 32-bit
  sequence; control frames carry a 32-bit acknowledgement / advertised receive window after it, data
  frames carry the payload. It is a TCP-like reliable stream, so a capture shows the three-way
  handshake and the receive window changing as a buffer fills or drains.

## See also

- [adhoc-multiplayer-testing.md](adhoc-multiplayer-testing.md) - playing a game's ad hoc multiplayer
  with several headless PPSSPP instances, to produce the HLE-level side of a hardware-vs-emulator
  comparison.
