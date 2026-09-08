# Reverse-engineering a PSP firmware module

`PPSSPPHeadless --re-module` loads a single PRX on its own - no game, no boot - and writes a report
about it: the module header, its exports and imports with NIDs resolved to names, one annotated
disassembly file per function, and the call graph.

This is a developer tool for understanding the PSP, not something a user ever runs.

## Running it

```bash
./build/PPSSPPHeadless --memstick ~/.config/ppsspp \
    --re-module flash0:/kd/libmp3.prx \
    --re-out /tmp/re
```

`--re-module` takes either a host path or a PSP-style `flash0:/kd/foo.prx`, which is resolved
against the configured NAND directory - so `--memstick` (or `--nand`) has to point at a dump.
PPSSPP can produce one itself from a firmware updater with `--unpack-updater`; see
[PsarFileFormat.md](PsarFileFormat.md).

| Option | |
|---|---|
| `--re-module PATH` | The module to load. Host path, or `flash0:/kd/foo.prx`. |
| `--re-out DIR` | Where the report goes. Created if missing; defaults to `re-out`. |
| `--re-func NAME` | Only disassemble this one function, by name or `0x08801234`. |
| `--re-syms FILE` | A `.ppsym` file of names to apply first, so the disassembly reads properly. |

## What comes out

- `<module>.index.md` - header, segments, export table (library, NID, address, name), import table,
  and every function with its size, caller/callee counts and whether it writes `v0`.
- `<module>/<addr>_<name>.asm` - one file per function.
- `<module>.xref.json` - the call graph, for asking "who calls this" without grepping.

Two things in the disassembly are worth knowing about:

**`lui`/`addiu` pairs are folded** and reported as the address they form, with a symbol name where
one is known (`; = 08004a10 <sampleRateTable>`). Every global and constant table is reached through
such a pair, so this is usually how you find the data a function works on.

**Each function gets a register evidence block rather than a guessed signature.** It reports, for
`a0`-`a3`, whether each was read before being written, written before being read, never touched, or
- the interesting case - *never read but still live across a call*:

```
;   a0   READ before written  -> used as a parameter here
;   a1   never read, but live across a call -> FORWARDED from our caller
```

That last one matters because MIPS code routinely takes an argument it never touches and leaves it
in place for a callee to pick up. A tool that inferred "this function takes one argument" from the
reads alone would be wrong, and so would you. Treat `FORWARDED` as evidence that the real arity is
larger than what is read here, and settle it by looking at what the callees do with the register.

## Naming things

The loader's scan names every function it finds `z_un_<address>`, which makes for unreadable
disassembly. The tool names what it can automatically - exported functions get their name from the
NID via PPSSPP's own HLE tables, and every import stub is named after the function it resolves to -
but the rest is up to you.

Accumulated names go in a `.ppsym` file, the same module-relative format the emulator saves from
`hle.module.saveSymbols` and the ImDebugger, and are applied with `--re-syms`. Names compound: once
a function is named, every call site that reaches it reads as that name, so later functions get
progressively cheaper to work out.

Those files are keyed by module name and crc32 (`PSP/SYSTEM/SYMBOLS/<name>_<crc>.ppsym`), so a set
of names only ever attaches to the exact build of the module it was written against. The report's
header line prints the crc to match.

## Notes and caveats

- Modules load at a fixed base (`0x08000000` for kernel modules), and the index prints a `+offset`
  column, so addresses can be compared against another tool's view of the same module.
- Some modules - `sysmem.prx` and `loadcore.prx` among them - have `modinfo` pointers that are file
  offsets rather than addresses, so the loader's own function scan finds nothing in them. The tool
  falls back to scanning the module's text range directly. Those modules are hand-written assembly
  without standard prologues, so expect few, large "functions".
- Nothing is executed. The tool brings up the memory map, timing, the HLE tables and the kernel
  allocators, then runs the real module loader and stops.
