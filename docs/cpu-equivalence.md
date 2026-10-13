# CPU core equivalence

The goal: every CPU core (interpreter, IR interpreter, JIT, IR JIT) produces the same bits on every
host (x86-64 and ARM64), NaN payloads (and possibly NaN signs) aside. Where the cores disagree, the
hardware decides which one is right, so several items below need a test on a real PSP before they
can be fixed.

## The fuzzer

`UnitTest CPUEquivalence` (in `unittest/JitHarness.cpp`) generates random straight-line code
(ALU, immediate, FPU, VFPU, loads and stores) and a random start state, runs it on every core, and
compares the full end state against the interpreter: GPRs, FPRs, VFPU registers and control
registers, hi/lo, fcr31 (bit 23 aside, which some cores sync from fpcond and some don't), fpcond,
llBit, and 1KB of scratch memory. NaNs compare equal to each other. Loads and stores go through
`k0`, which points at the scratch memory and is never written.

Each differing case is minimized (instructions dropped while it still differs) and printed with its
inputs. It ends with a count of the reported cases by their last instruction, which is usually the
one at fault. By default only a crash fails the test, as known differences remain.

Environment variables:

| Variable | |
|---|---|
| `PPSSPP_CPUFUZZ_COUNT` | number of cases (default 2000) |
| `PPSSPP_CPUFUZZ_SEED` | seed of the first case; case i uses seed + i |
| `PPSSPP_CPUFUZZ_CLASSES` | weighted string of `a` (ALU), `i` (immediate), `f` (FPU), `v` (VFPU), `m` (memory) |
| `PPSSPP_CPUFUZZ_CODE` | comma-separated hex instructions to run instead of the generated code |
| `PPSSPP_CPUFUZZ_CODE_SEED` | apply `PPSSPP_CPUFUZZ_CODE` to this seed only |
| `PPSSPP_CPUFUZZ_SKIP` | cores to skip, by digit: 1 IR interpreter, 2 JIT, 3 JIT IR |
| `PPSSPP_CPUFUZZ_REPORTS` | how many differences to minimize and print (default 20) |
| `PPSSPP_CPUFUZZ_VERBOSE` | print every case before running it, to find one that crashes |
| `PPSSPP_CPUFUZZ_DUMP` | write the interpreter's end state of every case to a file |
| `PPSSPP_CPUFUZZ_STRICT` | fail on any difference, not only on a crash |

Always set `CI=1` when running it from a script on Windows. Otherwise a JIT assert opens a message
box and the run hangs until someone clicks it.

`Tools/cpu_fuzz_reduce.py` runs the fuzzer and, if a case crashes (usually a JIT emitter assert),
finds it and reduces it to the instructions still needed for the crash. A crash that only happens
after other cases have run usually means uninitialized or stale state, not the reduced code.

To compare hosts: run with the same seed and count and `PPSSPP_CPUFUZZ_DUMP` on each, and diff the
files. A Windows-on-ARM machine can run the x64 build under emulation, which is enough for the
host-dependent float behavior of the interpreter. It isn't for timing, of course.

## Fixed

Crashes (JIT emitter asserts) and wrong code found by the fuzzer:

- ARM64 JIT: `ext` with pos + size past 32, and `ins` with msb below pos, asserted in UBFX/BFI.
- ARM64 JIT: `ApplyPrefixST` read an uninitialized register for a swizzle naming a lane past the
  op's size. The ARM64 JIT now gates its VFPU ops on prefixes the way the IR frontend does (vdot,
  vcmp, vi2f, vi2x, vocp, vhdp, vcmov, the matrix inits), and the x86 JIT got the gates it was
  missing too (vdot, vhdp, vcmp, vf2i, vh2f, vi2f, vi2x, vocp, vsgn, the color conversions, the
  matrix inits, vmmov, vmscl).
- ARM64 JIT: vdot, vhdp, vmmul and vqmul used fused multiply-add, which rounds differently from
  every other core. They now multiply and add separately, in the interpreter's order.
- vcmov in both JITs ignored a T prefix, which the interpreter applies to D (read as T).
- IR: vmmul, vmscl, vmidt/vmzero/vmone and vmmov on a 1x1 matrix flipped the transposed bit, which
  for 1x1 selects a different register. vmmul's fallback never wrote a 1x1 result at all.
- IR: vmmul, vtfm, vmscl, vmmov and the matrix inits took a matrix that isn't a plain Vec4 layout to
  be a transposed one, so a 4x4 matrix with a row/column offset (like `E723` or `M212`) produced
  Vec4 ops on unaligned registers. Native IR backends asserted, the IR interpreter computed garbage.
- IR: with every lane write-masked by the D prefix, the destination lanes were all D temps, which
  looked like a Vec4, and the mask blend then went into an unaligned destination.
- x86 JIT: `sc` into `$zero` wrote its result to an immediate and asserted. vmmul's SIMD path
  crashed on `vmmul.s` and on matrices that wrap around (`MatrixWrapsAround()`), which now go to
  the interpreter and the scalar path.
- Native IR backends (all four): `ll` into `$zero` mapped `$zero` for writing and asserted.
- ARM64 and x86-64 IR JITs: `sc` didn't mark rt dirty, so its 0/1 result was lost.
- IR interpreter: never applied the guest's rounding mode or flush-to-zero (fcr31) on block entry,
  only after a `ctc1` in the same block. It now does what the native dispatchers do, and leaves the
  host's mode in place around syscalls and replacement functions.

## Open differences

### Need a hardware test

1. **srl/srlv with stray bits.** `srl` with rs other than 0 (plain) or 1 (`rotr`), and `srlv` with
   sa other than 0 or 1 (`rotrv`): the interpreter treats them as invalid and does nothing, every
   other core shifts (rs/sa != 1) without rotating. Real behavior is probably decided by bit 21
   (bit 6 for `srlv`) alone, but that's a guess. Test: every value of the unused field, and also
   sll/sra/sllv/srav with the unused field set. The fuzzer skips these encodings for now.
2. **Flush-to-zero (fcr31 bit 24, FS).** The interpreter puts the host FPU in flush-to-zero.
   ARM64's FZ flushes denormal inputs and outputs, x86's FTZ (without DAZ) flushes only outputs, so
   the interpreter already differs between hosts, for example `sqrt.s` of a negative denormal gives
   -0 on ARM64 and NaN on x86, and comparisons with denormals differ. What does the PSP do with
   denormal inputs and outputs, with FS clear and set? Test add/sub/mul/div/sqrt/abs/neg/cvt/c.cond
   with denormal inputs and with results in the denormal range, both ways. Also where the result
   rounds up to FLT_MIN: x86 and ARM decide "tiny" before/after rounding differently.
3. **Does fcr31 affect the VFPU?** All cores run VFPU ops with the host FPU in the guest's FPU
   rounding mode and flush-to-zero setting. The VFPU has no such control on hardware, as far as we
   know. Test vadd/vmul/vdiv/vdot/vf2in etc. with fcr31 set to each rounding mode and FS.
4. **Signed zero from sums.** The interpreter sums vdot, vhdp, vmmul and vtfm from +0 (and for
   short vectors, adds +0 products for the missing lanes), so a sum of -0 products is +0. The other
   cores start from the first product and can return -0. Test: vdot.q/t/p of vectors whose products
   are all -0, and the same for vhdp, vmmul, vtfm.
5. **vmin/vmax and denormals**: the interpreter compares as the hardware does (denormals equal to
   zero) but returns the original bits. The JITs use host min/max, which under flush-to-zero returns
   -0 for a denormal. Depends on 2 and 3.
6. **Generated NaNs, cross-host.** The interpreter's VFPU arithmetic (vadd, vmul, vmmul, vtfm, ...)
   returns whatever NaN the host makes: x86 makes `0xFFC00000` (negative), ARM64 `0x7FC00000`.
   That would be harmless, but vmin/vmax order NaNs by sign as the hardware does, so the sign picks
   which operand they return, and the result differs between hosts as a plain number. This is the
   only interpreter difference between x64 and ARM64 the fuzzer found in 20000 mixed cases (all 5
   hits were a matrix op making a NaN, then vmin/vmax). Test: which NaN does each VFPU op produce
   for inf - inf, 0 * inf, 0 / 0, sqrt(-1) and the like (the accurate vmmul path assumes
   `0x7F800001`)? Then all cores can return that one.
7. **Undefined VFPU encodings**, where the cores disagree and nothing defines what's right:
   - `vdot.s`, `vhdp.s` (single-lane dot products).
   - `vdet` other than `.p`, `vcrsp`/`vqmul` at sizes other than `.t`/`.q`, the `???` ops in the
     same group.
   - Matrix ops whose register has a row/column offset that doesn't fit the size, like 3x3 `E110`
     or `vmscl.t E603, E212`. `GetMatrixRegs` wraps those around; is that what the PSP does?
   - `vmmul.s` (1x1).
   - `vtfm`/`vhtfm` with the size bits not matching the transform's size ("BADVTFM").
   - `vcmov` with bit 20 set (shown as `ARGH2`/`ARGH3`) or imm3 = 7.
   - `vmfvc` from a control register index past the defined ones.
   These are worth a hardware test each, as the cores have to agree on something. Games are
   unlikely to contain them, so they come after the rest.

### Known, no hardware test needed

8. **Divide-by-zero flags.** Only the interpreter sets the cause and flag bits for `div.s` by zero
   (see `Int_FPU3op`). The other cores leave fcr31 alone. Making the others match needs a new IR op
   or an interpreter fallback for div.s, and the latter costs every division.
9. **Rounding mode in the JITs** is applied only once a `ctc1` has been compiled
   (`js.hasSetRounding`). A non-default mode that reaches fcr31 some other way (a savestate made on
   another core, a thread switch restoring a context, the debugger) is ignored by the JITs'
   `cvt.w.s` and float ops until then. The fuzzer starts every case in the default mode for this
   reason, and only `ctc1` in the generated code changes it.
10. The legacy ARM64 JIT ignores the D prefix's write mask for `viim.s`/`vfim.s`, and `vmfvc` of an
   invalid control register writes a value where the interpreter writes 0.
11. Syscalls in the native JITs run in the guest's rounding mode unless a `ctc1` was compiled (same
    flag as 9), unlike the interpreter and IR interpreter, which always restore the host's mode
    around HLE.

### Not looked into yet

- The x86-64 JIT (under emulation on Windows on ARM) differs from the interpreter in 674 of 3000
  mixed cases, against 226 for the IR interpreter and 229 for the x86-64 IR JIT. It doesn't crash
  any more, but the differences aren't triaged.
- Of the remaining VFPU differences on ARM64 (about 20% of VFPU-only cases on every core), most of
  the ones looked at are items 4 and 7 above.
- The interpreter with flush-to-zero set (item 2) on both hosts: the cross-host run above starts
  every case in the default mode, and only the generated `ctc1`s turn FS on.
- Branches, delay slots, and VFPU prefixes across blocks: the generated code is straight-line.

## Unrelated

On the ARM64 Release build, 11 gpu/ pspautotests (gpu/exact/pixel and others) fail with different
pixel hashes, also with `-i`. An older ARM64 Debug build passes them.
