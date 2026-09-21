# sceGe: how the PSP's display list queue behaves

This describes the behavior `GPU/GPUCommon.cpp` (the queue) and `Core/HLE/sceGe.cpp` (the
interrupt side) model. All of it is what a game can observe, and all of it is pinned down on a
real PSP by the tests in `pspautotests/tests/gpu/ge` (`queue`, `queue2`, `break`, `breakwait`,
`intrsuspend`, `enqueueparam`) and `pspautotests/tests/gpu/signals`.

The headline, because everything else follows from it: **the GE stops at every SIGNAL and every
FINISH, and it takes the interrupt to get it going again.** After a signal it carries on with the
same list. After a FINISH the game's finish callback runs *first*, and only then does the list
leave the queue and the next one start. Nothing the game calls in between makes the GE run.

## Lists and their states

Up to 64 lists can exist at once; with all of them in use, enqueueing fails with `0x80000022`.
Ids are opaque, and an id is reused once its list is gone.

| State | Meaning |
|---|---|
| NONE | Not a list. `sceGeListSync(id, 1)` gives `0x80000100`. |
| QUEUED | On the queue, not executing. |
| RUNNING | The GE is executing it, or is stalled on it. |
| COMPLETED | Hit its FINISH and is off the queue. The id can still be asked about, until `sceGeDrawSync(0)` turns every COMPLETED list into NONE. |
| PAUSED | Stopped by `sceGeBreak(0)`, by a PAUSE signal, or put at the head with `sceGeListEnQueueHead`. |

Besides its state, a list remembers whether it **has started executing** at some point
(`DisplayList::started`), which several checks below depend on, and whether something is pending
on it: a pause that was requested but hasn't been delivered, a pause that has been, or a SYNC.
`DisplayList::signal` holds that, in the same enum as the behaviors of the SIGNAL command, with
`PSP_GE_SIGNAL_HANDLER_SUSPEND` doubling as "paused, and the game has been told".

## Enqueueing

`sceGeListEnQueue` and `sceGeListEnQueueHead` differ only in where the list goes. In order:

1. The list, stall, context and stack addresses all have to be 4-byte aligned (`0x80000103`), and
   a stack depth of 256 or more is `0x80000104`.
2. Games built with SDK version `0x01FFFFFF` or below get a 32-entry stack they didn't ask for,
   and the next step can't fail for them. Otherwise the stack is whatever `PspGeListArgs` says,
   which may well be none.
3. **The new list is compared against every list on the queue:**
   - Same list address: `0x80000021`. What's compared is the address the other list was
     *enqueued* with, not where it has got to since, and the uncached mirror of an address
     counts as the same. After `sceGeBreak(0)` stops a list, it's the address it stopped at that
     collides instead, and the address it was enqueued with is free again.
   - Same stack, **and the other list has started**: `0x80000021`. Two lists that are both still
     waiting their turn can share a stack without complaint.
4. Then, depending:
   - *Tail, queue empty:* RUNNING, and the GE starts on it right away. If it has a context, the
     GE state is saved into it first.
   - *Tail, queue not empty:* QUEUED at the back.
   - *Head, queue not empty:* the current head has to be PAUSED, or it's `0x800001FE`. The new list
     becomes PAUSED at the head, and the old head QUEUED behind it.
   - *Head, queue empty:* PAUSED, and **not started**. It takes a `sceGeContinue` to run it.

"Every list on the queue" includes a list whose finish callback is running right now. So a
finish callback that enqueues its own list again gets `0x80000021` (on a newer SDK), and anything
else it enqueues is QUEUED behind it rather than started.

## SIGNAL

A SIGNAL followed by END stops the GE and raises an interrupt. It always applies to the list at
the head of the queue.

| Behavior | What happens |
|---|---|
| `HANDLER_SUSPEND` (1) | The signal callback runs **with the GE stopped**, then the GE carries on. For SDK <= `0x02000010`, the list reads as PAUSED for the duration. |
| `HANDLER_CONTINUE` (2) | The GE carries on, *then* the signal callback runs. |
| `HANDLER_PAUSE` (3) | The list becomes PAUSED **immediately**, the argument is remembered, and the GE carries on. No callback yet. See below. |
| `SYNC` (8) | The GE carries on, and the list's next FINISH is swallowed: no callback, no completion, it just keeps going. |
| `JUMP`, `CALL`, `RET`, and their relative and origin variants (0x10 - 0x16) | Done in software by the interrupt. CALL uses the list's stack; overflowing or underflowing it leaves the GE stopped. |
| 0x20 - 0x2F, 0x30, 0x38 | Set a texture or CLUT address relative to the list or to the offset address. PPSSPP doesn't implement these. |

PPSSPP treats `HANDLER_CONTINUE` like `HANDLER_SUSPEND`: the GE waits for the callback. On
hardware the two run side by side, so a callback that looks at the GE finds it further along,
which is all `gpu/signals/continue` fails on. Waiting is the safe side to err on - a callback
that prepares something the list is about to use still gets there first.

Callbacks get the low 16 bits of the SIGNAL (or FINISH) command, the argument they were
registered with, and - for SDK > `0x02000010` only, otherwise 0 - the address after the END.

### The pause window

Between a PAUSE signal and the FINISH that delivers it, a list is in an odd place. The GE is
still executing it, but:

- `sceGeListSync(id, 1)` already says PAUSED.
- `sceGeContinue` returns `0x80000021`. So does `sceGeBreak(0)`.
- `sceGeListUpdateStallAddr` returns 0 but **has no effect on the GE**, because only a RUNNING
  list's stall address does. So a list that stalls inside the window is stuck for good, and
  `sceGeBreak(1)` is the only way out. Games therefore never put a stall there, and neither
  should a test that wants to finish.

When the FINISH arrives, the **signal** callback (not the finish one) is called, with the
argument from the PAUSE signal. The list stays PAUSED at the head of the queue, and nothing
behind it runs until the game calls `sceGeContinue`, which now works.

## FINISH

A FINISH followed by END stops the GE and raises an interrupt. If a SYNC or a pause is pending on
the list, see above. Otherwise, in this order:

1. The list becomes COMPLETED. It is still at the head of the queue.
2. The **finish callback** runs. The GE is idle. From in here, `sceGeListSync` on the next list
   says QUEUED, `sceGeDrawSync(1)` says DRAWING if there is a next list and COMPLETED if not,
   `sceGeListDeQueue` on the finished list says `0x80000021`, and `sceGeListUpdateStallAddr` on
   it says `0x80000020`.
3. If the list had a context, the GE state is restored from it.
4. The list leaves the queue.
5. The new head, if there is one and it isn't PAUSED, is started. If it has a context and hasn't
   started before, the GE state is saved into it.
6. If the queue is empty instead, threads waiting in `sceGeDrawSync(0)` are woken.
7. Finally the threads waiting in `sceGeListSync(id, 0)` for the finished list are woken - after
   the ones from step 6, which shows when they have the same priority.

If a SIGNAL and a FINISH are both pending by the time the interrupt runs, only the FINISH counts.

### While the interrupt can't be taken

All of the above happens in the interrupt, so with interrupts off (`sceKernelCpuSuspendIntr`)
the queue simply stands still at the first FINISH: that list keeps reading as DRAWING, the next
one as QUEUED, and no callback runs. It all catches up, in order, when interrupts come back.
Suspending thread dispatch (`sceKernelSuspendDispatchThread`) doesn't hold any of it up.

### How PPSSPP does this

PPSSPP doesn't run the GE in parallel with the CPU. `ProcessDLQueue()` executes a list all the way
to whatever stops it - a stall, a SIGNAL, a FINISH - adding up how long that would have taken, and
schedules the interrupt for that point in the future. That's the accepted inaccuracy, and it means
there's a stretch where a list has reached its FINISH but the game hasn't been told, which doesn't
exist on hardware. `DisplayList::pendingInterrupt` marks it.

What keeps the *sequencing* right in spite of that is one rule in `ProcessDLQueue()`: **while the
head of the queue has an interrupt pending, nothing runs**, whoever asks - an enqueue, a stall
update or a continue. The finished list stays on the queue until `InterruptEnd()`, which takes it
off and lets the next one go, as in steps 4 to 7 above. Before that rule, the next list ran right
away and the finished one left the queue at once, so a finish callback saw an empty queue: a list
it enqueued was started instead of queued, and then couldn't be dequeued.

Two places deliberately treat a list with its FINISH pending as already gone, since on hardware it
would be: the duplicate check when enqueueing (Exit enqueues the same list again right after it
ends), and `drawCompleteTicks`, which is set when the last list reaches its FINISH rather than
when the interrupt is delivered, so that a `sceGeDrawSync(0)` in between doesn't wait for nothing.

## Waiting

The two waiting calls, `sceGeListSync(id, 0)` and `sceGeDrawSync(0)`, refuse outright where a
thread can't wait, *even if there is nothing to wait for*: `0x80020064` from a GE callback or
any other interrupt, `0x800201A7` with dispatch or interrupts suspended.

`sceGeListSync(id, 0)` doesn't care what state the list is in. It waits for that id to complete or
be dequeued, and for an id that isn't pending - never used, or done - it returns 0 at once.

`sceGeListSync(id, 1)`:

| List state | Result |
|---|---|
| RUNNING | STALLING (3) if the GE is at the stall address, else DRAWING (2) |
| QUEUED | PAUSED (4) if it has started before (an EnQueueHead bumped it), else QUEUED (1) |
| COMPLETED | 0 |
| PAUSED | 4 |
| NONE | `0x80000100` |

`sceGeDrawSync(0)` waits for the queue to drain, and then **turns every COMPLETED list into
NONE**. Nothing else does, short of `sceGeBreak(1)`. `sceGeDrawSync(1)` looks at the head of the
queue, or the one after if the head is COMPLETED, and answers STALLING or DRAWING depending on
whether the GE is at that list's stall address - which it can't be if it isn't on that list.

## Break, continue, dequeue, stall

`sceGeBreak(0)`, by the state of the head of the queue:

| State | Result |
|---|---|
| RUNNING | The GE is stopped, and the list becomes PAUSED, remembering where it got to. If that was in the middle of a SIGNAL/END or FINISH/END pair, it resumes from the start of the pair. Returns the id. |
| QUEUED | Becomes PAUSED, id returned. |
| PAUSED | `0x80000021` for SDK <= `0x02000010` or inside the pause window, otherwise `0x80000020`. |
| no list | `0x80000020` |

**`sceGeBreak(1)` throws the whole queue away** and resets the GE: every list becomes NONE. An
interrupt that was raised but not taken yet goes too, so a list that reached its FINISH just
before never gets its finish callback. It doesn't wake anybody. A thread in `sceGeListSync(id, 0)` or `sceGeDrawSync(0)` stays there - until
a *new* list happens to get the same id and completes, or the queue next drains, at which point
they return 0 as if nothing had happened.

`sceGeContinue`: with no list, 0. A PAUSED head resumes where it left off (`0x80000021` inside the
pause window). On a RUNNING head, `0x80000020`; anything else, `0x80000004`. Both of those are -1
for SDK <= `0x01FFFFFF`.

`sceGeListDeQueue`: `0x80000100` for NONE, `0x80000021` for a list that has started - which
includes COMPLETED ones, until `sceGeDrawSync(0)` - and otherwise the list leaves the queue,
becomes NONE, and threads waiting for it are woken.

`sceGeListUpdateStallAddr`: takes effect only if the list is RUNNING, and a GE stalled on that
list resumes by itself. For QUEUED and PAUSED lists it's just remembered for when they run.
COMPLETED is `0x80000020`.

One thing not to do, in a test or anywhere: `sceGeBreak(0)` from inside a `HANDLER_SUSPEND`
callback with an SDK version above `0x02000010`. With an older one the list is PAUSED for the
duration and the break is refused (`0x80000021`). With a newer one it goes through, the GE is
restarted anyway when the callback returns, and the PSP hangs.

## Known leftovers in PPSSPP

- `gpu/signals/jumps` and `gpu/signals/simple` ask for a list's state the moment
  `sceGeListUpdateStallAddr` returns. On hardware the list is long done by then, callbacks and
  all. For PPSSPP it has reached its FINISH, but the interrupt is still to come.

- `IgnoreEnqueue` in `compat.ini` (Metal Gear Acid 2, #10906) skips the stack check. It was most
  likely hitting the not-started case, which no longer fails. If the game is fine without it,
  remove it.
- The "Discarding display list with state NONE" workaround in `ProcessDLQueue()` (Crazy Taxi,
  #19894) guards against a NONE list being on the queue. With the finished list now staying on
  the queue until its interrupt is done, the known ways for that to happen are gone, but it
  hasn't been proven unreachable.
- The GE has one stall address, which isn't the same thing as the one each list remembers:
  `sceGeListUpdateStallAddr` always updates the list's, but only a RUNNING list's update reaches
  the GE. PPSSPP has just `DisplayList::stall` for both. The pause window gets away with that,
  since a PAUSED list doesn't run. What doesn't is an old-SDK game updating the stall from inside
  a `HANDLER_SUSPEND` callback, where the list reads as PAUSED: on hardware the GE then stops at
  the *old* stall address until the next update from a thread, while reading as DRAWING rather
  than STALLING. PPSSPP lets it run on. `gpu/signals/handlercalls` shows it, and fails on it.
  Fixing it means giving `GPUCommon` a stall address of its own for execution, which touches the
  fast paths in `GPUCommonHW.cpp` and both GE debuggers.
- `sceGeBreak(0)` doesn't back up over a half-done SIGNAL or FINISH pair. With lists executing in
  one go, it's rarely in the middle of one.
- Signal CALL and RET save and restore only the pc, the offset address and the base address. On
  hardware the list's own CALL depth and return addresses are part of it as well.
- On hardware, a `sceGeListEnQueueHead` that fails with `0x800001FE` still uses up one of the 64
  lists until the next `sceGeBreak(1)`. Not emulated, and not worth it.
