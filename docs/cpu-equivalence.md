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
| `PPSSPP_CPUFUZZ_DUMP` | write every core's end state of every case to a file |
| `PPSSPP_CPUFUZZ_STRICT` | fail on any difference, not only on a crash |

Always set `CI=1` when running it from a script on Windows. Otherwise a JIT assert opens a message
box and the run hangs until someone clicks it.

`Tools/cpu_fuzz_reduce.py` runs the fuzzer and, if a case crashes (usually a JIT emitter assert),
finds it and reduces it to the instructions still needed for the crash. A crash that only happens
after other cases have run usually means uninitialized or stale state, not the reduced code.

To compare hosts: run with the same seed and count and `PPSSPP_CPUFUZZ_DUMP` on each, and diff the
files. Each line starts with the core and the seed. A Windows-on-ARM machine can run the x64 build
under emulation, which is enough for the host-dependent float behavior of the cores. It isn't for
timing, of course.

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

The goal, in order: every core gives the same result on both hosts, and then that result is the
PSP's. Where matching the PSP is expensive, agreeing with each other comes first.

### Answered on hardware

Each of these has a pspautotests test recorded on a PSP (6.61). Distinct outputs across the eight
configurations (four cores, ARM64 and x86-64 under Rosetta), and lines that differ from the PSP:

| Test | Distinct outputs | ARM64 interp / ir / jit / jit-ir | x86-64 interp / ir / jit / jit-ir |
|---|---|---|---|
| `cpu/cpu_alu/shift_encodings` | 2 | 8 / 8 / 8 / 8 | 8 / 8 / 8 / 8 |
| `cpu/fpu/denormal` | 2 | 54 / 54 / 54 / 54 | 16 / 16 / 16 / 16 |
| `cpu/vfpu/fcr31` | 7 | 66 / 74 / 74 / 75 | 74 / 78 / 78 / 79 |
| `cpu/vfpu/signed_zero` | 4 | 14 / 37 / 37 / 37 | 14 / 37 / 36 / 37 |
| `cpu/vfpu/nan_gen` | 7 | 51 / 51 / 57 / 57 | 51 / 51 / 51 / 54 |
| `cpu/vfpu/undefined` | 4 | 4 / 12 / 21 / 12 | 4 / 12 / 15 / 12 |

Rosetta behaves as an x86 CPU here: the x86-64 interpreter makes `0xFFC00000` NaNs and flushes
denormal results but not inputs under FS (FTZ without DAZ).

1. **srl/srlv with stray bits** (`shift_encodings`). Only the low bit of the unused field counts:
   `srl` rotates when bit 21 is set and shifts when it is clear, whatever bits 22-25 hold, and
   `srlv` the same with bit 6. sll/sra/sllv/srav ignore the field. Every core is wrong: the
   interpreter does nothing for values above 1, the others shift.
2. **Flush-to-zero (FS)** (`denormal`). With FS clear the PSP handles denormals in full, which
   both hosts match. With FS set it flushes denormal results to zero, with tininess decided before
   rounding (a product that rounds up to FLT_MIN is still flushed), but never flushes inputs: sqrt,
   div and the compares see a denormal as it is. One exception: add/sub with a zero operand
   returns the other operand, denormal or not. x86 FTZ gets all of it but that exception (16
   lines). ARM64's FZ also flushes inputs (54 lines), so the two hosts differ wherever a denormal
   reaches an FPU op with FS set. Matching needs either the x86-like mode where the host has it
   (FEAT_AFP: FPCR.AH with FIZ clear) or flushing results in software when FS is set.
3. **fcr31 doesn't affect the VFPU** (`fcr31`). All six fcr31 settings give identical results.
   The VFPU always rounds to nearest, and always flushes denormal inputs and outputs to zero
   (`vf2iu` of a denormal is 0, a denormal times 1 is 0). The cores should run VFPU ops in
   round-to-nearest without guest FS, and flush denormals themselves, rather than inheriting the
   FPU's mode. Some of the remaining lines are vdot's exact summation (`1.5u + 1.5u + 3 - 3` is 0
   on the PSP).
4. **Signed zero from sums** (`signed_zero`). Every summing op returns +0 for a zero sum,
   whatever the signs of the products: vdot, vhdp, vfad, vavg, vdet, vcrsp, vqmul, vmmul, vtfm,
   vhtfm, at every size, including vfad of all -0. vcrs (a product per lane) and plain vadd keep
   -0. The interpreter is right apart from vcrsp, vqmul and vtfm/vhtfm.
5. **vmin/vmax and denormals** (`fcr31`). Denormals compare equal to zero, a tie returns the
   second operand, and the result keeps the operand's bits (a denormal comes out unflushed). The
   interpreter does this; the JITs use host min/max.
6. **Generated NaNs** (`nan_gen`). VFPU arithmetic never keeps a payload: every NaN it produces is
   `0x7F800001` or `0xFF800001`. The sign is + for add, sub and every summing op, the XOR of the
   operand signs for mul, div and vscl, the input's sign for vrcp, vrsq, vsin and vasin, and + for
   vsqrt, vlog2 and vexp2. vsat0/vsat1, vabs and vneg pass NaN bits through. Fixed bits like these
   make both hosts agree for free.
7. **Undefined VFPU encodings** (`undefined`). The interpreter matches the PSP on all of them
   (single-lane vdot/vhdp, vdet at every size, vcrsp/vqmul at .s/.p, matrix registers that wrap
   around, vtfm/vhtfm size mismatches, vmfvc past register 15 reading 0, vcmov with bit 20 set)
   except `vcmov` with imm3 = 7, which tests a CC bit that is always clear: vcmovt never moves and
   vcmovf always does. The other cores have to follow the interpreter.

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
- Branches, delay slots, and VFPU prefixes across blocks: the generated code is straight-line.

## Unrelated

On the ARM64 Release build, 11 gpu/ pspautotests (gpu/exact/pixel and others) fail with different
pixel hashes, also with `-i`. An older ARM64 Debug build passes them.

In headless, `-r` is both the IR interpreter and `--root`, and `--root` wins: it takes the next
argument as a directory. Use `--cpu=ir`.
