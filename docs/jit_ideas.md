# JIT improvement ideas

Just sketching here after looking at some generated code.

## ARM64

### Full-on SIMD

This is a large project, but on x86 we already manage to convert a lot of VFPU
vector math to SSE. The project would be to do something similar on ARM and ARM64,
or maybe only ARM64.

There's some low-hanging fruit to do first, though.

### VFPU register ordering

It's common to see stuff like:

```asm
str #s4, [x27, #256]
str #s6, [s27, #260]
```

in the generated output.

This should be trivially collapsible to a single stp instruction -
though turns out a bit less easy than hoped due to the fact that
stp has a more limited range of offsets than normal stores.
Can be fixed by generating an offset in a temp register...

DONE: register ordering fixed in this case, now just need the offset stuff.

DONE: register allocation ordering also fixed for 3-op instructions like vadd.q.

### Use ldp in MapRegsAndSpillLockV

When we end up mapping in four consecutive regs here, it ends up as
four independent loads, instead of what could be two ldp. Though it's probably
only motivated to do this for a quad operation because it will involve generating
an offset register for the ldp to reach.

### Stack load/store sequences

In the input we often see, in the epilogues of functions:

lw s0, 0x14(sp)
lw s1, 0x18(sp)
lw s2, 0x1c(sp)

etc.

These get turned into a regular sequence of individual loads, then usually
they get flushed immediately as we reach the end of the function.

It would be possible to recognize these sequences and if there are no further
uses of the registers later in the function, just do a memcpy to the register file.
Of course, it's usually the case that some of these registers already have mappings,
but those could just be flushed anyway.

## ARM32

### Register ordering

The above optimizations should translate directly to ARM32, and would enable
more optimized flushes back to memory of allocated registers.
