# sceKernel HLE review notes

Notes from a review pass over `Core/HLE/sceKernel*.cpp` (~16k lines across 14 files), done
2026-09-03. Kept mostly so the next person doesn't re-derive the same conclusions - the
"verified clean" section is as much the point as the findings.

Cross-referenced against JPCSP (`../JPCSP`) where the correct behaviour wasn't obvious from
the PSP docs.

## Bug classes that turned up

Each of these appeared in more than one place, so they're worth grepping for when touching this code:

**Timeout writeback without a clamp.** `CoreTiming::UnscheduleEvent` returns the scheduled time
minus the current time, which is *negative* when the event is overdue but not yet processed - the
exact situation when a wait is satisfied around its own timeout. Nine of ten sites wrote
`(u32)cyclesToUs(cyclesLeft)` straight into the game's timeout variable. Now funnelled through
`HLEKernel::WriteRemainingTimeout` in `KernelWaitHelpers.h`; the two thread-end sites keep their own
copy because they unschedule even when the game passed no timeout pointer.

**Addresses validated, extents not.** `Memory::IsValidAddress` checks a single byte.
`KernelImportModuleFuncs` used it for `nidData` and `varData` and then indexed those arrays
`numFuncs` (u16) and `numVars` (u8) times; `sysclib_strcpy` used it for a destination it then wrote
`strlen(src) + 1` bytes into. Use `IsValidRange` with the size you're actually going to touch.

**Unbounded walks over game-supplied data.** The module variable relocation list was scanned until
it happened to hit a zero word; the mbx packet list was walked until it came back around to its
head. Both follow only *valid* pointers, so nothing faults - they just run off the end of memory or
spin forever. Bound them by what's mapped (`Memory::ClampValidSizeAt`) or by the count the object
already tracks.

**Overflow checks that don't match the arithmetic they guard.** `sceKernelCreateFpl` and
`sceKernelCreateTlspl` validate `blockSize * count` at 4-byte alignment, but then allocate using an
alignment the caller chooses, so the aligned total can still wrap - allocating a small block while
the block count stays huge, and later handing out `address + block * alignedSize` outside it.

**Functions that return a pointer, returning an error code.** `sceKernelAllocHeapMemory` passed
`BlockAllocator::Alloc`'s `(u32)-1` straight out, and returned UID error codes for a bad heap id.
JPCSP documents the contract as "the address of the allocated memory block, or NULL on error", so
every failure has to be 0 or callers checking for null see success.

## Verified clean - don't re-flag these

- **`readyCallbacksCount` bookkeeping.** There's a self-suspecting `"became negative"` report in
  `__KernelCheckCallbacks`, but the invariant holds. The increment (only on the 0 -> nonzero
  transition in `__KernelNotifyCallback`) is matched by three decrements: running a callback
  (`__KernelRunCallbackOnThread` zeroes `notifyCount` first), `sceKernelDeleteCallback`, and thread
  deletion. The thread-deletion path looks like it should double-decrement with a later
  `sceKernelDeleteCallback`, but `PSPThread::Cleanup()` destroys the callbacks immediately after the
  decrement loop, so the second lookup fails.
- **`u32 error;` uninitialized before `kernelObjects.Get`, then `if (error) return;`.** Safe -
  `Get` writes `outError` on both paths, 0 on success.
- **Kernel object name copies.** Every `strncpy(x.name, name, KERNELOBJECT_MAX_NAME_LENGTH)` writes
  into a `char[KERNELOBJECT_MAX_NAME_LENGTH + 1]` and follows with an explicit NUL. Correct.
- **`strncpy(moduleName, modinfo->name, ARRAY_SIZE(module->nm.name))`** in `sceKernelModule.cpp`
  mixes two buffers' sizes but is safe: 28 into a zero-initialised 29.
- **VPL and FPL allocation failure handling.** Both check for `(u32)-1` properly; the heap was the
  outlier.
- **The unchecked `ReadUnchecked_U32` sites** in `sceKernelThread.cpp` (register restore, extended
  stack restore, the exit-callback parameter area) all have `IsValid4AlignedRange` checks a few
  lines above.
- **`sceKernelCreateHeap`'s `alloc.Init(address + 128, size - 128)`** looks like it underflows for a
  heap smaller than 128 bytes, but can't: `BlockAllocator::AllocAligned` rounds the caller's size
  variable up by reference and every partition allocator is constructed with grain 256.
- **VTimer scheduling arithmetic.** The `base + schedule - current` u64->s64 conversion wraps
  negative on underflow, which the `goalUs < minGoalUs` clamp catches.

## Latent, worth knowing about

`__KernelSendMsgPipe` / `__KernelReceiveMsgPipe` have two shapes of transfer loop. The buffered one
explicitly breaks when a transfer would move zero bytes; the unbuffered ones (`bufSize == 0`) don't -
they only make progress inside `if (bytesToSend > 0)`, so a queued waiting thread with a zero
`freeSize` would spin forever. That can't happen today because every `AddSendWaitingThread` /
`AddReceiveWaitingThread` call site is guarded by a `size != 0` check, but the invariant lives two
call sites away from the loop that depends on it.

## Open, deliberately not changed

- **`__KernelStartThread` doesn't bound `argSize`.** `sp -= (argSize + 0xf) & ~0xf` with `argSize`
  up to `INT_MAX` pushes the stack pointer below the thread's stack. The copy itself goes through
  the validating `Memory::Memcpy`, so this is guest-side corruption only, and real hardware probably
  doesn't validate it either - changing it risks breaking games that depend on that. The
  `argSize + 0xf` signed overflow is UB regardless and deserves a cast.
- **Three unchecked `__GetCurrentThread()` dereferences**: `__KernelCurHasReadyCallbacks`,
  `__KernelWaitCallbacksCurThread`, `KernelRotateThreadReadyQueue`. Of 40 uses in that file the rest
  check, assert, or use a ternary - and `__KernelWaitCurThread`, the direct sibling of the second
  one, has `_assert_(thread != nullptr)`. No path was found where a syscall runs without a current
  thread, so these were left alone rather than papering over a real invariant.

## Coverage

Read closely: `Heap`, `Semaphore`, `Alarm` in full; `Module`'s import path; `Interrupt`'s sysclib
block; `Memory`'s pool creation and `PartitionMemoryBlock`; `sceKernel.cpp`'s object pool and
savestate loop; `Thread`'s start/refer paths and the callback machinery; `Mbx`'s message list;
`VTimer`'s scheduling.

Audited across all 14 files by pattern: unchecked memory access, timeout writeback, null checks
after `kernelObjects.Get`, name copies, allocator return values, uninitialised members.

Also checked: `MsgPipe`'s transfer loops, and the interrupt dispatch in `sceKernelInterrupt.cpp`
(`__TriggerInterrupt` indexes `intrHandlers` without a bounds or null check, unlike
`__RunOnePendingInterrupt`, but every caller passes an internal constant).

Not read line by line: the `EventFlag` / `Mutex` wait-queue bodies, and thread scheduling proper.
