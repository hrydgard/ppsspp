# Debugger threading model (Core_RunOnCPUThread / g_frameMutex)

Which thread may touch CPU-thread-owned debugger state (breakpoints, symbol map, registers, memory,
kernel threads), and in which order the locks have to be taken. Read this before touching
`Windows/Debugger/*`, `Core/Debugger/WebSocket*` or anything else that reaches into core state from
outside the CPU thread.

CPU-thread-owned debugger state (`g_breakpoints`, `g_symbolMap`, `g_disassemblyManager`, registers,
memory, kernel threads) used to be touched directly from other threads (the WebSocket handler
thread, and the legacy Win32 debugger's message-pump thread) with no synchronization. Two
mechanisms now exist for doing this safely - pick based on whether you're mutating or just reading:

- **`Core_RunOnCPUThread(func)`** (`Core.h`/`Core.cpp`) - queues `func` to run on the CPU thread,
  blocking the caller until it's done. Use for *mutations* (breakpoint add/remove, register/memory
  writes, symbol map edits, stepping requests, thread wake/kill). Drained at the top of every
  `Core_RunLoopUntil()` iteration, so it's reached whether the CPU is running or stepping/paused;
  runs immediately if already called from the CPU thread. Two hard rules learned the hard way:
  never put a modal Win32 dialog call (`MessageBox`, `DialogBoxParam`, `InputBox_GetString`,
  `SomeDialog::exec()`) inside the queued lambda - it would block the CPU thread on user input, so
  split the function into "read/decide", "show modal", "mutate" pieces instead. And never call
  `SendMessage()` targeting one of your own GUI-thread windows from inside the lambda - the calling
  GUI thread is blocked waiting on the CPU thread rather than pumping messages, so a cross-thread
  `SendMessage()` back to it deadlocks; keep such calls outside the lambda instead.
- **`g_frameMutex`** (`Core.h`/`Core.cpp`) - a plain `std::mutex`, held by `NativeFrame()`
  (`UI/NativeApp.cpp`) only across the span where it actually touches that state
  (`g_breakpoints.Frame()` through `g_screenManager->render()` - where `Core_RunLoopUntil()`/actual
  CPU stepping happens - through `runImDebugger`/`renderImDebugger`), not across input handling or
  the present/frame-pacing waits. Use for *reads* invoked very frequently (`WM_PAINT`, a list
  reload triggered on every debugger-state-changed notification) where routing through
  `Core_RunOnCPUThread` would be too slow/heavy. The legacy Win32 debugger windows do this now -
  see `CtrlRegisterList::onPaint`, `CtrlDisAsmView::onPaint`, `CtrlMemView::onPaint`,
  `CtrlBreakpointList::reloadBreakpoints`, `CtrlThreadList::reloadThreads`, etc. in
  `Windows/Debugger/*.cpp`.

Architecture fact that makes `g_frameMutex` correct: regardless of graphics backend, `NativeFrame()`
(and thus CPU emulation via `Core_RunLoopUntil()`, and the Dear ImGui debugger) always runs on the
*same* thread - see `Core/EmuThread.cpp`. When a backend needs its own thread for actual graphics
API calls (`GraphicsContext::NeedsSeparateEmuThread()` - true for OpenGL, SDL, headless, libretro,
Qt; false for D3D11/Vulkan on Windows), the *original* thread stays behind purely to pump
`graphicsContext->ThreadFrame()` (i.e. just executes queued graphics API calls), and a *newly
spawned* thread takes over `NativeFrame()`/game logic/CPU duty. So `UI/ImDebugger/*.cpp` is always
safe to read/write this state directly, without either mechanism - it's always on the same thread
as `Core_RunLoopUntil()`. The legacy Win32 debugger is different: its dialogs are pumped by the
*original* `WinMain` message-loop thread, which is a genuinely separate OS thread from whichever
thread ends up running `NativeFrame()`/CPU, regardless of backend (this split happens one level
above the `NeedsSeparateEmuThread()` branch) - that's the whole reason it needed this mechanism.

**Update (2026-08-08): both `BreakpointManager`'s and `SymbolMap`'s internal mutexes have been
removed** after auditing every touchpoint across the codebase (WebSocket subscribers,
`Windows/Debugger/*.cpp`, `Windows/MainWindowMenu.cpp`/`Windows/MainWindow.cpp` main-window menu
items, `Core/Core.cpp`'s free-threaded `Core_Break`/`Core_Resume`, `UI/ImDebugger/*.cpp`,
JIT/interpreter backends, `Core/Debugger/MemBlockInfo.cpp`) and confirming each is covered by one of
the two mechanisms above or is already on the CPU/NativeFrame thread. Notably, `Windows/main.cpp`'s
`SortSymbols()` calls (fired from `System_Notify(BOOT_DONE)`/`System_Notify(SYMBOL_MAP_UPDATED)`)
turned out to already be safe without any change - both notifications are only ever fired from the
CPU/NativeFrame thread (`UI/EmuScreen.cpp`, `Core/HLE/sceKernelModule.cpp`), despite an old comment
there claiming reliance on the (now-removed) internal lock. `Qt/mainwindow.cpp`/`Qt/QtMain.cpp`
still poke at `g_symbolMap` directly and unguarded on the Qt UI thread - a pre-existing issue,
deliberately left alone since Qt isn't a maintained backend and is slated for removal; removing the
lock doesn't change `SymbolMap`'s public API, so Qt still builds, just without that safety net.
`GPU/Common/GPUDebugInterface.cpp` (GE debugger expression evaluation) and `Core/MemFault.cpp`
(crash-time diagnostics) also touch `g_symbolMap` and were deliberately not audited this round -
different subsystem / best-effort-by-nature respectively, follow up if they ever come up.

Two more things found while doing this:
- **`Core_RunOnCPUThread()`'s queue is only drained where `Core_RunLoopUntil()` runs, which requires
  a game to be loaded** (it's called from `EmuScreen::render()`). Calling `Core_RunOnCPUThread()`
  while at the main menu with nothing loaded used to hang forever. Fixed by also calling the
  (now public) `Core_ProcessCPUQueue()` directly from `NativeFrame()`, right before
  `g_screenManager->render()`, inside the same `g_frameMutex`-locked span - so it always runs, not
  just while a game is active.
- **Lock-ordering rule**: because `Core_ProcessCPUQueue()` is called from inside `NativeFrame()`'s
  `g_frameMutex`-locked span, any `Core_RunOnCPUThread()` lambda that itself tries to lock
  `g_frameMutex` (directly, or indirectly - e.g. by calling something like
  `CDisasm::NotifyMapLoaded()`, which locks it internally) will deadlock. Keep such calls outside
  the queued lambda, same as the modal-dialog and `SendMessage()` rules above.

## Lock ordering: `g_frameMutex` before `Core_LockAgainstShutdown()`, always

`Core_LockAgainstShutdown()` / `CoreShutdownLock` (a `recursive_mutex`, `g_shutdownLock` in
`Core/Core.cpp`) is held across `CPU_Shutdown()` and `Memory::Reinit()`, i.e. while the core is
going away. Take it on any thread other than the CPU thread before reading core state - emulated
memory, the symbol map, kernel objects - so none of it is freed mid-read. It was called
`Memory::Lock()` and only covered the memory map; the name misled people into thinking it was about
memory access. When a function needs both it and `g_frameMutex`, **take `g_frameMutex` first**.

The CPU thread's order is structural and can't be changed: `NativeFrame()` wraps everything below it
in `g_frameMutex`, and several things under there lock memory - `Core_ProcessCPUQueue()` running a
queued WebSocket handler, and `runImDebugger()` -> `ImMemView` -> `DisassembleRange()`. So the
GUI-thread side is the one that has to match. (Getting it backwards deadlocked for real: a paint
handler held the memory lock and waited for `g_frameMutex` while the CPU thread did the reverse.)

Also: **a `Core_RunOnCPUThread()` callback does not need `Core_LockAgainstShutdown()`** - teardown only happens
on the CPU thread itself (`Memory::Shutdown()` via `CPU_Shutdown()` <- `PSP_Shutdown()`, all callers
on that thread; `Memory::Reinit()` from `Memory::DoState()` on savestate load). Don't add one.

**The general rule behind both of these: never make the CPU thread wait for a thread that is (or may
be) waiting on the CPU thread.** `Core_RunOnCPUThread()` blocks until the CPU thread drains the
queue, so anything the CPU thread might block on must not be held across such a call. The WebSocket
debugger's `lifecycleLock` hit exactly this - it was held across a whole event handler, and the CPU
thread took it in `Core_NotifyLifecycle(STOPPING)`, so stopping a game with a debugger request in
flight hung both threads. That lock is gone now; the rule is what's left of it.

**The WebSocket debugger no longer has any lock guarding it against startup/shutdown, and must not
grow one back.** The invariant instead is: a handler either does its emulator-state access inside
`Core_RunOnCPUThread()` - which serializes it against startup and teardown, since those run on the
CPU thread too - or touches only state that carries its own lock (the log ring buffer, `ctrlMutex`,
`GPUStepping`'s pause-action rendezvous). When adding a subscriber, put the core access in the
queued callback, including the `isAlive()`/`IsValidAddress()` checks: answering those outside it
just means acting on an answer that may already be stale.

`game.*` and `cpu.stepping`/`cpu.resume` are pushed rather than polled - `WebSocketDebuggerTick()`
(called from `Core_ProcessCPUQueue()`) notices the transition on the CPU thread, formats the event
there, and drops it in a per-connection mailbox. Don't add a broadcaster that reads emulator state
from the connection's own thread; produce the event on the CPU thread and push it instead.

The Win32 debugger's GUI-thread readers *do* still need it, so don't "simplify" those away:
teardown is not yet fully inside the `g_frameMutex` span. `EmuScreen::render()`'s `PSP_Shutdown()` is
inside it, but the ones in `EmuScreen::sendMessage()` (`REQUEST_GAME_RESET`, loading a new game) run
from `g_screenManager->sendMessage()` in `NativeFrame()`, which sits *above* where the guard is
taken. Closing that hole - moving those shutdowns inside the span, or deferring them to render time -
is the prerequisite for dropping the shutdown lock from the debugger entirely.

Painting-problem design history, in case a similar tradeoff comes up elsewhere: routing every paint
through `Core_RunOnCPUThread` was rejected as too slow for something invoked continuously. A
per-window snapshot/cache with a per-row-rechecked `Core_IsStepping()` guard was tried first and
worked, but still had a narrow TOCTOU race (the CPU could resume between the check and that row's
reads) and the per-row-recheck pattern itself wasn't liked. Settled on `g_frameMutex` instead -
simpler, and actually race-free rather than just lower-risk. `CtrlRegisterList` shows live values
always now, grayed out by color alone (not cached) while the core is running, since a
constantly-moving value isn't meaningful to read closely anyway.

