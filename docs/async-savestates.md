# Async savestates

How savestate compression and file I/O were taken off the emulation thread, so saving and loading
don't stall the game long enough to glitch the audio. The worker and the operation queue are in
`Core/SaveState.cpp`, rewind in `Core/SaveStateRewind.cpp`, and the file format code in
`Common/Serialize/Serializer.cpp`. Written 2026-10-19; file references will drift.

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
2. Worker: compress (see [Memory](#memory)) and write the file, to `<file>.tmp` for a slot save, then for a slot save the rotation that `SaveSlot`'s
   `renameCallback` does today (old file to `.undo`, `.tmp` to the real name), including the
   `g_Config.Save()` for the undo bookkeeping. Then set `g_filesStale`.
3. Completion: the callback, with "Saved State" or the failure. **The message appears when the file
   is on disk**, not at snapshot time, so a failed write is reported honestly. A failed write leaves
   the previous file in place (it only ever wrote the `.tmp`).

The thumbnail screenshot is already asynchronous and doesn't change.

**A save to a path that already has a save queued, and not yet snapshotted, is a duplicate
keypress.** It isn't queued again. Its callback is attached to the queued save and called with that
save's status and an empty message (so it shows nothing, and the queued save's own callback shows
the result once). Once the snapshot is taken, another save to the path is a new save: the game may
have moved on, or a load been applied, since.

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
  reads.
- **UI-thread file operations on slots wait for the worker** (`WaitForWorker()`, for all jobs, since
  it's rare and short): `UndoSaveSlot` and `DeleteSlot`. `UndoLoad` is a load, so it's queued behind
  the load undo write like anything else. `LoadSlot`'s existence check counts a slot with a save
  pending as existing. The thumbnail screenshot isn't the worker's, so its rotation doesn't wait.

## What stays synchronous

There's one code path. Waiting for it is `WaitForWorker()`, which simply blocks: worker jobs never
need the emu thread, so it can't deadlock.

- **libretro**: `retro_serialize`/`retro_unserialize` are in memory with no file I/O, and stay as
  they are. So does the freeze-frame feature (`SaveToRam`/`LoadFromRam` every frame).
- **Verify** stays synchronous.
- **Shutdown and game switch** (`SaveState::Shutdown()`, from the kernel's shutdown, which app exit
  goes through too): wait for the worker, so a save whose snapshot was taken is written. Then run the
  completions' bookkeeping (the undo slot in the config) without their callbacks, which can refer to
  an EmuScreen that's going away, and drop whatever is still queued, as before. A save still queued
  at that point (asked for in the pause menu, which doesn't run `Process()`, then quitting) is lost
  like it always was: by then most of the kernel has shut down, too late to take a snapshot.
- Loads at boot (auto-load, `--state`, opening a `.ppst`) and the debugger's `game.state.*` with
  `wait` already work through callbacks, and need no special handling. The game already ran at least
  a frame before a boot-time load applied.

## Rewind

Rewind snapshots used to compress on a `std::thread` per snapshot, which the next snapshot joined
(stalling if it was still running), and the whole restore ran on the emu thread: wait for the
compressor, decode (rebuild the full ~38 MB state from the base and the changed blocks), apply. The
snapshot itself (2-5 ms, mostly copying RAM and VRAM, already a `ParallelMemcpy`) stays on the emu
thread.

Now:

- The emu thread owns the ring itself (which slots hold what); compressing and decoding are jobs on
  the savestate worker, and the emu thread leaves the buffers a job uses alone until it's done.
- **Compression runs on the worker.** If the previous snapshot's job is still running, the snapshot
  is skipped and retried next frame rather than waited for: snapshots are best effort.
- **The deltas get zstd on top** (level 1: higher levels shrink them a little for a lot more time).
  The changed blocks still compress about 3x, so the ring takes about a third of the memory.
- **Restore is an async load from memory**: `StartRestore()` picks the state and has the worker
  decode it (queued behind the newest snapshot's compression, if that's still going), and
  `FinishRestore()` applies it on the emu thread. No snapshots are taken in between. No prefetching
  (see [Memory](#memory)).

## Memory

What each operation holds at its peak, against today:

| | Today | Async |
|---|---|---|
| Save | snapshot (38 MB) + `ZSTD_compressBound` output buffer (about 38 MB) | snapshot (38 MB) + the compressed output as it grows (10-12 MB). The snapshot is freed before writing |
| Load | whole compressed file (about 12 MB) + state (38 MB) | state (38 MB) + a small streaming input buffer |
| Load from a slot | the save, then the load | at apply: the loaded state + the load undo snapshot (76 MB), briefly. The loaded state is freed right after the apply, before the undo is compressed |
| Rewind restore | a `static` 38 MB decode buffer, never freed | the same buffer, freed after the apply |
| Rewind ring | block diffs | the same, zstd-compressed: about a third |

Rules:

- **No pools and no buffers kept between operations.** Allocate a buffer when the operation starts
  and free it when it ends. A 38 MB allocation costs far less than the stall this design removes,
  and an idle emulator then holds nothing for savestates (rewind aside, which is opt-in and labelled
  a memory hog in the settings).
- **At most two save snapshots at a time** (see [Save](#save)), and one load buffer at a time: a
  second load queued behind a first doesn't start reading until the first is applied.
- **Stream compression in both directions.** Saving, `ZSTD_compressStream2` into a buffer that grows
  with its output, instead of allocating the worst case up front. It isn't streamed straight to the
  file, since the header holds the compressed size and seeking back to patch it in isn't safe on
  every kind of storage (Android's content URIs). Loading, `ZSTD_decompressStream` reads the file in
  chunks, so the compressed data never sits in memory in full. The file format doesn't change.
- **Rewind adds nothing.** The worker reuses rewind's existing buffers. There's no prefetching
  (decoding a likely target before the rewind is pressed), which would need a spare full-size buffer.

## Later, if phone measurements call for it

- `MeasureAndSavePtr` traverses the state twice (measure, then write). Writing into a buffer sized
  from the previous measurement plus slack would skip the measure pass, but needs bounds checks in
  the write pass.
- The apply is mostly `DoState` logic rather than copying; leave it unless it shows up.

## Results

Measured the same way as above (Release headless, desktop x64), on the emu thread:

| | Before | After |
|---|---|---|
| Save | 49-75 ms | 2.4-2.5 ms (the snapshot) |
| Load | 28-40 ms | 12-15 ms (the apply, plus the warnings and callback around it) |
| Rewind restore | decode + apply | 12-13 ms (the apply) |
| A frame waiting on the worker | | under 0.05 ms |

Rewind deltas went from 4-13 MB to 1.3-3.8 MB each, at 10-24 ms of worker time.

## Testing

Headless, wsdbg and the `game.state.*` debugger commands cover all of it. `game.state.save` and
`game.state.load` take a `slot` instead of a `path`, which goes through the slot code like the save
and load keys: undo rotation, load undo, repeats. Use a scratch `--memstick` for that, not the app's,
or the tests overwrite real slots. wsdbg's `--sync` moves on when a request is acknowledged, not when
a `wait` result arrives, so put a `:sleep` between steps whose order matters, and one at the end.
