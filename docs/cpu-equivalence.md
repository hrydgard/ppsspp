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
| `PPSSPP_CPUFUZZ_SKIP` | cores to skip, by digit: 1 IR interpreter, 2 JIT |
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
- JITs: the mode was only taken into account once a `ctc1` had been compiled (`js.hasSetRounding`),
  so a mode from a savestate made on another core, a core or thread switch, or an interpreted
  `ctc1` was ignored, and syscalls then ran in the guest's mode. `UpdateFCR31()` now sets the flag
  and throws away the blocks compiled before. Ops left to the interpreter run in the guest's mode,
  except syscalls, and an interpreted `ctc1` takes effect at once. `CPURoundingMode` tests all of
  this on every core, with and without `JitDisableFlags`.
- ARM64 JIT: `mfhi`/`mflo` into `$zero` wrote the host register holding zero.
- x86-64 IR JIT: helper calls (vsin, vrsq and the like) didn't flush RCX and RDX outside Windows,
  where the System V ABI doesn't preserve them.
- MIPSAnalyst: `ctc1`/`cfc1` of a control register other than 31 counted as touching the FPU
  condition, so the x86 JIT's register cache dropped a dirty fpcond before one.
- x86 JIT: fpcond held the compare's all-ones mask instead of 1.
- ARM JITs: `vmfvc` of a control register past the defined ones mapped the dest without
  `MAP_DIRTY`, so its 0 was never written.
- The legacy JITs' `Comp_Generic` ran the interpreted instruction in the host's default rounding
  mode instead of the guest's.
- Hardware answers 1, 2 (host agreement), 5 and 7 (the interpreter and `vcmov`) below.

## Open differences

The goal, in order: every core gives the same result on both hosts, and then that result is the
PSP's. Where matching the PSP is expensive, agreeing with each other comes first.

### Answered on hardware

Each of these has a pspautotests test recorded on a PSP (6.61). Distinct outputs across the eight
configurations (four cores, ARM64 and x86-64 under Rosetta), and lines that differ from the PSP.
The `jit` columns were measured with the legacy JITs, since removed; `jit` is now `jit-ir` on both.

| Test | Distinct outputs | ARM64 interp / ir / jit / jit-ir | x86-64 interp / ir / jit / jit-ir |
|---|---|---|---|
| `cpu/cpu_alu/shift_encodings` | 1 | 0 / 0 / 0 / 0 | 0 / 0 / 0 / 0 |
| `cpu/cpu_alu/allegrex_encodings` | 1 | 0 / 0 / 0 / 0 | 0 / 0 / 0 / 0 |
| `cpu/fpu/denormal` | 2 | 54 / 54 / 54 / 54 | 54 / 54 / 54 / 54 |
| `cpu/vfpu/fcr31` | 2 | 50 / 50 / 50 / 51 | 50 / 50 / 50 / 51 |
| `cpu/vfpu/signed_zero` | 4 | 14 / 37 / 37 / 37 | 14 / 37 / 36 / 37 |
| `cpu/vfpu/nan_gen` | 7 | 51 / 51 / 57 / 57 | 51 / 51 / 51 / 54 |
| `cpu/vfpu/undefined` | 4 | 0 / 6 / 3 / 6 | 0 / 6 / 9 / 6 |
| `cpu/fpu/fpu_nan` | 2 | 0 / 0 / 0 / 0 | 4 / 4 / 4 / 4 |

Before the fixes, `shift_encodings` had 2 outputs and `fcr31` 7. `nan_gen`, `fpu_nan` and
`denormal` still come out as each host's own NaNs (item 6). What's left of `fcr31` is items 3 and
vdot's summation, and of `undefined` the horizontal ops and matrix registers with an offset that doesn't fit the size. Rosetta behaves as an x86 CPU here: the x86-64 interpreter made
`0xFFC00000` NaNs and flushed denormal results but not inputs under FS (FTZ without DAZ).

The fuzzer agrees: integer, FPU and memory code (`PPSSPP_CPUFUZZ_CLASSES=aaiiffm`) gives the same
end state on every core and on both hosts in 20000 cases, apart from item 8 below. With VFPU code
mixed in, the interpreter differs between the hosts in 7 of 20000 cases, each a host NaN out of a
horizontal op (vmmul, vtfm, vdot, vdet) that vmin, vmax or vf2i then orders or converts by its
sign.

1. **srl/srlv with stray bits** (`shift_encodings`, fixed). Only the low bit of the unused field
   counts: `srl` rotates when bit 21 is set and shifts when it is clear, whatever bits 22-25 hold,
   and `srlv` the same with bit 6. sll/sra/sllv/srav ignore the field. The Allegrex bit ops
   (`allegrex_encodings`, fixed) decode only part of their op field too, so every value is one of
   wsbh, wsbw, seb, seh and bitrev (see `AllegrexBitOp()`), and special3 function 0x18 is a no-op.
2. **Flush-to-zero (FS)** (`denormal`). With FS clear the PSP handles denormals in full, which
   both hosts match. With FS set it flushes denormal results to zero, with tininess decided before
   rounding (a product that rounds up to FLT_MIN is still flushed), but never flushes inputs: sqrt,
   div and the compares see a denormal as it is. One exception: add/sub with a zero operand
   returns the other operand, denormal or not. x86 FTZ gets all of it but that exception (16
   lines). ARM64's FZ also flushes inputs (54 lines). The x86 cores now set DAZ along with FTZ, so
   both hosts flush inputs and agree, 54 lines away from the PSP. Matching the PSP needs either
   the x86-like mode where ARM64 has it (FEAT_AFP: FPCR.AH with FIZ clear, not on every device),
   or flushing results in software when FS is set.
3. **fcr31 doesn't affect the VFPU** (`fcr31`). All six fcr31 settings give identical results.
   The VFPU always rounds to nearest, and always flushes denormal inputs and outputs to zero
   (`vf2iu` of a denormal is 0, a denormal times 1 is 0). The cores should run VFPU ops in
   round-to-nearest without guest FS, and flush denormals themselves, rather than inheriting the
   FPU's mode. Some of the remaining lines are vdot's exact summation (`1.5u + 1.5u + 3 - 3` is 0
   on the PSP). The cores agree with each other here (they all follow fcr31), so this is about
   matching the PSP. A check on every VFPU op would cost too much. Since games run VFPU ops in
   clumps, switching the host into round-to-nearest with flush-to-zero (inputs too) around a clump
   and back afterwards may be cheap enough; not tried yet. The ops that move to the accurate dot
   product get this right by themselves.
4. **Signed zero from sums** (`signed_zero`). Every summing op returns +0 for a zero sum,
   whatever the signs of the products: vdot, vhdp, vfad, vavg, vdet, vcrsp, vqmul, vmmul, vtfm,
   vhtfm, at every size, including vfad of all -0. vcrs (a product per lane) and plain vadd keep
   -0. The interpreter is right apart from vcrsp, vqmul and vtfm/vhtfm.
5. **vmin/vmax and denormals** (`fcr31`). Denormals compare equal to zero, a tie returns the
   second operand, and the result keeps the operand's bits (a denormal comes out unflushed). Fixed
   on every core with `vfpu_minmax_key()`, an integer key compared instead of the floats
   (`minmax_zero` and `minmax_tie` pass everywhere).
6. **Generated NaNs** (`nan_gen`). VFPU arithmetic never keeps a payload: every NaN it produces is
   `0x7F800001` or `0xFF800001`. The sign is + for add, sub and every summing op, the XOR of the
   operand signs for mul, div and vscl, the input's sign for vrcp, vrsq, vsin and vasin, and + for
   vsqrt, vlog2 and vexp2. vsat0/vsat1, vabs and vneg pass NaN bits through. The cores make the
   host's NaN instead, so x86-64 and ARM disagree. The
   branch `cpu-equiv-4` fixes the single-lane ops (vadd, vsub, vmul, vdiv, vscl, vcrs, vsat0/1, vbfy,
   vocp, vsocp) on every core, through four new IR ops and checks in the legacy JITs, and is held
   back until its cost on every multiplication is measured. The horizontal ops wait for the
   accurate dot product.

   The FPU makes the positive quiet NaN `0x7FC00000` from operands that aren't NaN
   (`cpu/fpu/fpu_nan`), which ARM64 does and x86 doesn't for add, sub and div. Also fixed on
   `cpu-equiv-4`.
7. **Undefined VFPU encodings** (`undefined`). The interpreter matches the PSP on all of them
   (single-lane vdot/vhdp, vdet at every size, vcrsp/vqmul at .s/.p, matrix registers that wrap
   around, vtfm/vhtfm size mismatches, vmfvc past register 15 reading 0, vcmov with bit 20 set),
   now including `vcmov` with imm3 = 7, which tests a CC bit that is always clear: vcmovt never
   moves and vcmovf always does (fixed on every core). The JITs still differ on the single-lane
   dot products and vcrsp/vqmul at .s/.p (for the accurate dot product), and the IR cores on
   matrix registers whose offset doesn't fit the size.

### Known, no hardware test needed

8. **Divide-by-zero flags.** Only the interpreter sets the cause and flag bits for `div.s` by zero
   (see `Int_FPU3op`). The other cores leave fcr31 alone. Making the others match needs a new IR op
   or an interpreter fallback for div.s, and the latter costs every division.

### ARM32

Measured on a Galaxy S8+ and A21 with the armeabi-v7a build (`android/ab.sh APP_ABI=armeabi-v7a HEADLESS=1
UNITTEST=1 SKIPAPP=1`, run over adb). These measurements predate the IR-based ARM32 JIT, when
`CreateNativeJit` returned the legacy `ArmJit` (since removed). The IR JIT gives the IR interpreter's
end state in every fuzz case under qemu-arm (`./b.sh --armhf`, see building.md).

- The interpreter gives the same end state as on ARM64 in all 20000 integer/FPU/memory fuzz
  cases, and in all but 3 of 20000 mixed ones (horizontal-op NaNs into vmin/vmax, as on x86-64).
- All three cores match the Mac's same core on every `cpu/` pspautotest, apart from horizontal ops
  and NaNs: `signed_zero` (vdet, vqmul in the JIT), `nan_gen`, and in the JIT the NaNs out of vsat
  (`prefix_sat`) and vbfy (`specials`). Those wait for the accurate dot product and item 6.
- What it took: the interpreters apply the guest's FPSCR (rounding mode and FZ); the IR
  interpreter's NEON Vec4 paths are ARM64-only, since ARMv7 NEON always flushes and rounds to
  nearest; ARM32's libm `ceilf`/`floorf` don't flush a denormal input, so `FlushForConversion()`
  does that under FS; and the ARM JIT got the other JITs' prefix gates, mtvc masking, and fixes to ins/ext/max/min, round.w.s of NaN, and vsgn. `vh2f` and `vasin` go to the
  interpreter (the hardware half conversion quiets NaNs).
- Before these JIT fixes, the fuzzer on the S8+ had the JIT differ from the interpreter in 1298
  of 20000 mixed cases (the IR interpreter in 1446), not broken down. It crashes on the A21 in its
  memory setup, so it hasn't been rerun since.

### Not looked into yet

- With VFPU code in the mix, the IR cores differ from the interpreter in 147 of 2000 default
  cases. Most of those looked at are the horizontal ops (items 4 and 6) and item 7.
- Branches, delay slots, and VFPU prefixes across blocks: the generated code is straight-line.

## Unrelated

In headless, `-r` is both the IR interpreter and `--root`, and `--root` wins: it takes the next
argument as a directory. Use `--cpu=ir`.
