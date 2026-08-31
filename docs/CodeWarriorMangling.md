# Metrowerks CodeWarrior C++ symbol mangling (PSP)

Some PSP titles were built with Metrowerks CodeWarrior rather than the SDK's GCC, so their
symbols aren't Itanium-mangled and `_Z`-based demanglers do nothing with them. PPSSPP
demangles them in `Common/Data/Text/Demangle.cpp` (`DemangleCodeWarrior`), which is what makes
the symbol map readable for those games.

The scheme is a descendant of the AT&T cfront mangling, and the basics match the Macintosh C++
ABI. It deviates in enough places that following that document alone produces wrong answers, so
this is a description of what PSP binaries actually contain.

## How this was worked out

From two PSP executables that shipped with intact symbol tables - one small C++ test program,
one large application of about 10,000 symbols - by demangling every symbol and checking the
result against the disassembly and against the bytes each data symbol points at.

Constructs the corpus did not contain are marked **(inferred)** below. They come from the cfront
scheme, are implemented, and are believed right, but nothing here proves them.

## Overall shape

A mangled symbol is:

```
<basename> __ [<class>] [<cv>] F <parameters> [_ <return type>]
```

with the class, the cv-qualifiers, the `F` and everything after it all optional. A few examples,
building up:

| Mangled | Demangled |
| --- | --- |
| `run_tests__Fv` | `run_tests()` |
| `getDistance__6KzUtilFP7st_unitP7st_unit` | `KzUtil::getDistance(st_unit *, st_unit *)` |
| `what__Q23std9exceptionCFv` | `std::exception::what() const` |
| `count__Q23foo3bar` | `foo::bar::count` |

The last one has no `F`, so it isn't a function at all - it's a static data member, and there is
no parameter list to print. A name with neither a class nor an `F` is not mangled.

### Finding the separator

The `__` separator is genuinely ambiguous. A base name can itself start with underscores
(`__SetupFrameInfo__FP12ThrowContext...`), contain a `__` of its own
(`__TableUnit__SetValue<i,1>__5shTbbF...`), and plain C code in the same binary is full of names
like `I3dCacheManager__GarbageCollect` that are not mangled at all.

There is no way to resolve this from the grammar; PPSSPP tries every `__` in the symbol and keeps
the first split whose right-hand side parses completely. Only if none does will it accept a split
whose parameters didn't decode - and then only when a class qualifier is present, because
otherwise every C name with a double underscore in it decodes as a garbage function signature.

## Names

An identifier is written as its length in decimal followed by its characters: `6KzUtil`.

A qualified name is `Q<count>` followed by that many identifiers: `Q23std9exception` is
`std::exception` (2 components, `3std` and `9exception`). The count is a single digit; ten or
more components are written `Q_<count>_` **(inferred)**.

### Template arguments

Template arguments are written literally between `<` and `>`, *inside* the length-prefixed name -
the length covers the whole thing, brackets included. (The Macintosh ABI document describes a
`__PT` prefix instead. PSP binaries do not use it.)

```
39CList<Q38hlScreen5Brwsr13CContentsUnit>     shList::CList<hlScreen::Brwsr::CContentsUnit>
```

Here `39` is the length of `CList<Q38hlScreen5Brwsr13CContentsUnit>`. Each argument is either a
mangled type, or a plain integer for a non-type parameter, separated by commas:

| Mangled | Demangled |
| --- | --- |
| `15CSimpleChar<24>` | `CSimpleChar<24>` |
| `61ForwardIterator<16TABLE_VAR_MEMBER,21TABLE_VAR_SECT_HEADER,v>` | `ForwardIterator<TABLE_VAR_MEMBER, TABLE_VAR_SECT_HEADER, void>` |
| `47CList<Q26ssTool29tag_<Q26shFont12SysCmdString>>` | `CList<ssTool::tag_<shFont::SysCmdString>>` |

They nest, so a parser must match brackets rather than scanning for the next `>`, and split
arguments only on top-level commas.

A *function* template puts its arguments in the base name instead, and - unlike a normal function -
encodes its return type after a trailing `_`:

```
sort<Pf>__3stdFPfPf_v          void std::sort<float *>(float *, float *)
```

### Special base names

| Base name | Meaning |
| --- | --- |
| `__ct` | Constructor; the name is taken from the class |
| `__dt` | Destructor |
| `__op<type>` | Conversion operator, e.g. `__opCi` is `operator const int()` |
| `__<code>` | Overloaded operator, see below |

Operator codes are the cfront set: `nw` `dl` `nwa` `dla` for `new`/`delete`, `pl` `mi` `ml` `dv`
`md` for arithmetic, `apl` `ami` `amu` `adv` `amd` `aad` `aor` `aer` `als` `ars` for the compound
assignments, `eq` `ne` `lt` `gt` `le` `ge` for comparisons, `aa` `oo` `nt` for logic, `ad` `or`
`er` `co` `ls` `rs` for bitwise, and `as` `pp` `mm` `cl` `vc` `rf` `rm` `cm` for
`=` `++` `--` `()` `[]` `->` `->*` `,`.

So `__as__9ANIMEDataFRC9ANIMEData` is `ANIMEData::operator=(const ANIMEData &)`.

## Types

Type codes are read left to right, each one modifying what follows.

| Code | Type | |
| --- | --- | --- |
| `v` | `void` | |
| `b` | `bool` | |
| `c` | `char` | |
| `s` | `short` | |
| `i` | `int` | |
| `l` | `long` | |
| `x` | `long long` | |
| `f` | `float` | |
| `d` | `double` | |
| `r` | `long double` | **(inferred)** |
| `w` | `wchar_t` | **(inferred)** - a Metrowerks addition to the cfront set |
| `e` | `...` | varargs, always last |
| `P` | pointer to | |
| `R` | reference to | |
| `C` | `const` | |
| `V` | `volatile` | |
| `U` | `unsigned` | |
| `S` | `signed` | **(inferred)** |
| `A<n>_` | array of `n` | **(inferred)** |
| `F<params>_<ret>` | function | |
| `<len><name>` | class | |
| `Q<n>...` | qualified class | |

`PCc` is `const char *`; `CPc` is `char * const`. A pointer to a function or an array needs the
declarator wrapped rather than a `*` appended - `PFi_i` is `int (*)(int)`, and `PPFv_v` is
`void (**)()`.

Two forms back-reference an earlier parameter of the same function **(inferred)**:

| Form | Meaning |
| --- | --- |
| `T<index>` | Same type as parameter `<index>`, 1-based |
| `N<count><index>` | `<count>` more parameters, each the type of parameter `<index>` |

So `foo__FPCcUiN21` is `foo(const char *, unsigned int, const char *, const char *)`.

### cv-qualifiers on the function

A `C` or `V` sits between the class and the `F`, and qualifies the member function rather than a
parameter: `InitRun__Q28shCamera11TStillParamVFv` is
`shCamera::TStillParam::InitRun() volatile`.

## Compiler-generated symbols

CodeWarrior emits a family of symbols for things with no C++ name of their own. These wrap a
mangled name rather than being one, and none of them are described in the ABI document.

| Form | Meaning | Example |
| --- | --- | --- |
| `__vt__<class>` | Vtable | `__vt__Q23std9exception` |
| `__RTTI__<class>` | Typeinfo record | `__RTTI__Q23std9exception` |
| `__sinit_<file>` | Static initializers for a translation unit | `__sinit_hl_app.cpp` |
| `__sterm_<file>` | Static destructors | |
| `@<n>@<symbol>` | `this`-adjusting thunk, `n` bytes | `@12@__dt__3SonFv` |
| `@STRING@<symbol>[@<n>]` | A string literal used inside that function | `@STRING@Get_BGM__Q25hlBHC4SBhcFi@0` |
| `@LOCAL@<symbol>@<var>[@<n>]` | A function-local static | `@LOCAL@sort<Pf>__3stdFPfPf_v@shuffle@0` |
| `@GUARD@<var>$<n>` | Its "already constructed" flag | `@GUARD@app$16079` |
| `@<n>` | An anonymous string constant | `@10046` |

The trailing `@<n>` on `@STRING@` and `@LOCAL@` is a discriminator, present only when a function
has more than one of them - so a parser has to treat it as optional, and must not mistake the
`@` before a `@LOCAL@` variable name for it.

`@<n>@` thunks really are thunks: the one above is 8 bytes of code that does `addiu a0, a0, -12`
and jumps, i.e. adjusts `this` for a secondary base. `@STRING@` symbols point at ordinary string
data - usually the `__FILE__` an assertion expanded to.

## Other decorations

Two more show up inside otherwise ordinary names. Neither needs decoding, but both are worth
recognising:

- `<name>$<digits><file>` - a class or variable with internal linkage, tagged with where it was
  declared: `ClutScreen$11229hl_cplayer_effect_renderer_cpp`. These make for very long symbols,
  since the tag repeats everywhere the type appears.
- `<len>@unnamed@<file>@` - used as a qualifier for file-scope statics, i.e. an unnamed namespace.
  `ARWMENU_MODEL_NAME__34@unnamed@hl_editor_arrow_menu_cpp@` is a data symbol in one.

## The hidden destructor parameter

**The mangling describes the source-level signature, not the generated one.** `__dt__3SonFv`
demangles to `Son::~Son()`, and that is the right answer for a demangler - but the function the
compiler emitted takes a second argument, and anything that turns a demangled name into a
function signature (a decompiler, a debugger's parameter view) will be wrong about it.

CodeWarrior emits one destructor per class rather than the two or three the Itanium ABI clones,
and distinguishes the cases with a flag. On PSP that flag is a **`short` in `a1`**, and the
destructor **returns `this` in `v0`**. So the real signature is:

```c
Son *__dt__3SonFv(Son *this, short flag);
```

Decompiled from a real one, the body is:

```c
Son *__dt__3SonFv(Son *this, short flag) {
    if (this) {                       // destructors are null-safe
        ... restore vtable pointers, destroy members ...
        __dt__3DadFv(this + 12, 0);   // base subobjects get 0
        __dt__3MomFv(this, 0);
        if (flag > 0)                 // "blez" - only a positive flag frees
            __dl__FPv(this);          // operator delete
    }
    return this;
}
```

The values that actually get passed, counted over the call sites in a retail binary:

| Flag | Meaning | Frequency |
| --- | --- | --- |
| `-1` | Destroy a complete object without freeing it - a stack object, a member, an explicit `~T()` call | Most calls |
| `1` | Destroy and free - what `delete` compiles to | A handful |
| `0` | Destroy a base-class subobject | A handful |

`0` and `-1` are a caller-side distinction: the destructors examined here only test the sign, so
both simply mean "don't free", but the compiler still passes `0` specifically when destroying a
base subobject and `-1` everywhere else. Treat `0` as "this call is destroying me as somebody's
base" when reading a call site, even though the callee can't tell.

Constructors in these binaries take only `this` (and also return it).

Note that the vtable slot holds this same function, flag and all, which is how CodeWarrior gets
away without separate deleting and non-deleting destructors: a virtual `delete` passes 1 through
the vtable.

## Hazards

- **A truncated name is unrecoverable.** Some symbol tables cap names (127 characters is a common
  limit), and templates blow past that easily. Once a name is cut, the length prefixes no longer
  match what's left and nothing after the cut can be trusted - better to reject the symbol than
  to print a plausible-looking guess.
- **Not everything with a `__` is mangled.** C code linked into the same binary uses `__` as a
  word separator freely. Requiring a class qualifier before accepting a partial parse is what
  keeps `I3dClut__FlushCache` from turning into `I3dClut(long, ...)`.
- **`e` is a real parameter, not a parse failure.** `Printf__Q26shFont4FontFiiPCce` ends in
  varargs; `(int, int, const char *, ...)` is the correct answer, not a partial one.
