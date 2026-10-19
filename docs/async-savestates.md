# Async savestates

The design for taking savestate file work off the emulation thread, so saving and loading don't
stall the game long enough to glitch the audio. Not implemented yet. Written 2026-10-19; file
references will drift.

**Keep memory use low.** A state is about 38 MB uncompressed, and much of the hardware we run on is
short of memory. So don't waste it: no full-size buffers beyond what an operation needs, freed when
it's done. Within reason, though; a brief extra buffer in a rare case is fine. See [Memory](#memory).

## Where the time goes

Measured with Release headless on desktop x64, local disk, three commercial games:

| | Emu thread today | Of which can move to a worker |
|---|---|---|
| Save | 49-75 ms | compress (zstd, default level) + write: 47-70 ms. The snapshot itself is 2-5 ms |
| Load | 28-40 ms | read + decompress: 21-30 ms. Applying the state is 6-10 ms |
| Load from a slot | the save and the load above | `LoadSlot` first saves `load_undo.ppst` in full |

The waits at the start of `SaveStart::DoState` (utility IO, sas mix, async IO, pending GPU drawing)
were under 0.2 ms. Phones are several times slower, and their storage can be much slower again, so
the worker column is the part that hurts. The emu thread keeps only the snapshot and the apply.

## Structure

Each operation splits into the part that touches emulator state (emu thread) and the part that
doesn't (worker).

- **One savestate worker thread**, with a FIFO job queue. Not `g_threadManager`: file work on a slot
  has to happen in order. On Android it holds an `AndroidJNIThreadContext` for its lifetime, since
  storage access can need JNI.
- **Callbacks still run on the emu thread**, from `SaveState::Process()`. The worker posts results to
  a completion queue that `Process()` drains, so callers see the same threading as before, only
  later.
- An operation goes through: queued, emu phase, worker phase, completion, callback. A load does its
  worker phase (reading) before its emu phase (applying).

## Save

1. Emu thread, in `Process()`: `SaveStart::DoState` into a fresh buffer (the snapshot). The waits
   and `FlushPendingDrawing()` stay here, and so does the `saveStateGeneration` bump.
2. Worker: compress with streaming zstd straight into `<file>.tmp` (see [Memory](#memory)), then for a slot save the rotation that `SaveSlot`'s
   `renameCallback` does today (old file to `.undo`, `.tmp` to the real name), including the
   `g_Config.Save()` for the undo bookkeeping. Then set `g_filesStale`.
3. Completion: the callback, with "Saved State" or the failure. **The message appears when the file
   is on disk**, not at snapshot time, so a failed write is reported honestly. A failed write leaves
   the previous file in place (it only ever wrote the `.tmp`).

The thumbnail screenshot is already asynchronous and doesn't change.

**A save to a path that already has a save queued or in flight is a duplicate keypress.** It isn't
queued again. Its callback is attached to the save in flight and called with that save's status and
an empty message (so it shows nothing, and the in-flight save's own callback shows the result once).

A save to a different path is queued, and takes its snapshot right away like any other. **At most two
save snapshots exist at once**: a third save, while two are still being written, waits for the
older of them before taking its snapshot. Two quick saves to different slots are rare and briefly
cost one extra buffer; more than that isn't worth holding 38 MB each for.

## Load

1. Worker: read the header and check it, then stream the file through the zstd decompressor into
   the state buffer, while the game keeps running.
2. Emu thread, in the first `Process()` after the read completes:
   - For a slot load, snapshot the current state for load undo. This has to happen here, at the
     moment the game actually leaves that state, not when the load was asked for.
   - Apply the loaded state (`LoadPtr`), with everything `Process()` does around it today
     (`hasLoadedState`, `Core_ResetException()`, AV dump restarts, `g_lastSaveTime`).
3. Worker: compress and write the load undo snapshot to `load_undo.ppst`, and update
   `sStateLoadUndoGame`. This happens even when the apply failed: that's when the undo is most
   useful, since a failed load resets the game.
4. Completion: the callback, after the apply.

The game runs a frame or two longer before the loaded state takes over. That isn't observable,
since the load replaces all of it.

## Ordering

Operations still take effect in the order they were asked for:

- **Emu phases run in queue order.** A save queued after a load waits until that load is applied,
  and snapshots the loaded state, as it does today when both run in the same `Process()`. A load
  waiting for its file holds up only the emu phases queued behind it.
- **Worker jobs run in queue order**, so a save to a slot followed by a load of it writes before it
  reads. As a shortcut, a load whose path has a write pending takes that save's uncompressed
  snapshot directly and skips the file round trip.
- **UI-thread file operations on slots wait for pending worker jobs on the same slot**: `UndoSaveSlot`,
  `UndoLoad`, `DeleteSlot`, and `SaveSlot`'s screenshot rotation. `LoadSlot`'s existence check
  counts a slot with a write pending as existing.

## What stays synchronous

There's one code path. Synchronous means enqueue and wait for completion. On the emu thread that
wait runs the worker phase inline instead of blocking on the worker, so it can't deadlock against a
queue that needs the emu thread.

- **libretro**: `retro_serialize`/`retro_unserialize` are in memory with no file I/O, and stay as
  they are.
- **Verify** stays synchronous.
- **Shutdown and game switch** (`SaveState::Shutdown()`): flush pending writes (a save that was asked
  for is never lost), cancel pending reads and call their callbacks with a failure.
- **App exit**, including Android activity destruction, calls `SaveState::FlushPendingWrites()` with
  a timeout.
- Loads at boot (auto-load, `--state`, opening a `.ppst`) and the debugger's `game.state.*` with
  `wait` already work through callbacks, and need no special handling. The game already runs at
  least a frame before a boot-time load applies today.

## Rewind

Rewind snapshots already compress on a thread. What's still on the emu thread:

- The snapshot (2-5 ms, mostly copying RAM and VRAM, already a `ParallelMemcpy`).
- `compressThread_.join()` at the start of each snapshot, which stalls if the previous compress is
  still running. That gets more likely once the deltas are compressed with zstd.
- The whole restore: wait for the compressor, decode (rebuild the full ~38 MB state from the base and
  the changed blocks, essentially a 38 MB copy), apply.

Changes:

- Compression moves to the savestate worker, replacing the `std::thread` per snapshot. If the
  previous snapshot's job is still running, skip the snapshot rather than wait: snapshots are best
  effort.
- **Restore becomes an async load from memory**: the worker decodes, the emu thread applies. No
  prefetching (see [Memory](#memory)).

## Memory

What each operation holds at its peak, against today:

| | Today | Async |
|---|---|---|
| Save | snapshot (38 MB) + `ZSTD_compressBound` output buffer (about 38 MB) | snapshot (38 MB) + streaming output buffer (a few hundred KB) |
| Load | whole compressed file (about 12 MB) + state (38 MB) | state (38 MB) + streaming input buffer |
| Load from a slot | the save, then the load | at apply: the loaded state + the load undo snapshot (76 MB), briefly. The loaded state is freed right after the apply, before the undo is compressed |
| Rewind restore | a `static` 38 MB decode buffer, never freed | the same buffer, freed after the apply |

Rules:

- **No pools and no buffers kept between operations.** Allocate a buffer when the operation starts
  and free it when it ends. A 38 MB allocation costs far less than the stall this design removes,
  and an idle emulator then holds nothing for savestates (rewind aside, which is opt-in and labelled
  a memory hog in the settings).
- **At most two save snapshots at a time** (see [Save](#save)), and one load buffer at a time: a
  second load queued behind a first doesn't start reading until the first is applied.
- **Stream compression in both directions.** `ZSTD_compressStream2` writing to the file in chunks,
  and `ZSTD_decompressStream` reading from it, so a compressed copy of the state never sits in
  memory in full. The file format doesn't change: the header's sizes are known before writing
  (`UncompressedSize`) or patched in afterwards (`ExpectedSize`, by seeking back).
- **Rewind adds nothing.** The worker reuses rewind's existing buffers. There's no prefetching
  (decoding a likely target before the rewind is pressed), which would need a spare full-size buffer.

## Later, if phone measurements call for it

- `MeasureAndSavePtr` traverses the state twice (measure, then write). Writing into a buffer sized
  from the previous measurement plus slack would skip the measure pass, but needs bounds checks in
  the write pass.
- The apply is mostly `DoState` logic rather than copying; leave it unless it shows up.

## Implementation order

1. The worker, job and completion queues, ordering, flush and cancel on shutdown, with every job
   still waited for inline (no behavior change).
2. Async saves: plain and slot saves, the undo rotation, duplicate detection.
3. Async loads, with the load undo snapshot moved to apply time.
4. Rewind compression on the worker, then async restore.
5. zstd on rewind deltas (the changed blocks only), which slots into step 4's worker job.

Each step can be tested with headless, wsdbg and the `game.state.*` debugger commands. A debugger
query for pending savestate operations would let tests wait for the worker to drain.
