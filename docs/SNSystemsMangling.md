# SN Systems (SNC / ProDG) C++ symbol mangling (PSP)

A number of PSP titles were built with SN Systems' compiler rather than the SDK's GCC. Its C++
symbols look nothing like the Itanium mangling, and nothing like the cfront-derived one
CodeWarrior uses either - they are much more compact, and encode lengths as letters rather than
digits. PPSSPP demangles them in `Common/Data/Text/Demangle.cpp` (`DemangleSNSystems`).

## How this was worked out

From a single PSP executable that shipped with its symbol table intact: 4,662 mangled symbols
out of 11,338, covering an application, an STL, a JPEG decoder and a sound library. Everything
below is attested there except where marked **(inferred)**, which means the code implements it
by analogy with the codes that are attested and nothing in that binary proves it.

The description is complete enough to decode 4,661 of the 4,662. The one holdout is an STL
symbol whose template argument is a reference to a member of another template.

## Overall shape

```
__0 <kind> <name…> <parameters> [_ <return type>] [<qualifier>]
```

The kind character says what the symbol is, and - importantly - how many name components to
read, since nothing else delimits them:

| Kind | Meaning | Name components |
| --- | --- | --- |
| `f` | Member function | class, then function |
| `F` | Free function | function |
| `o` | Member operator, constructor or destructor | class, then a two-letter code |
| `O` | Global operator | just the two-letter code |
| `d` | Data | class, then variable |

```
__0fLCHeapMemoryFAlloci          CHeapMemory::Alloc(int)
__0FHGetRandv                    GetRand()
__0oLCHeapMemorydtv              CHeapMemory::~CHeapMemory()
__0OnwUi                         operator new(unsigned int)
__0dLCHeapMemoryG__vtbl          CHeapMemory::__vtbl
```

## Names

An identifier is a **letter giving its length**, then that many characters. `A` is 0, so `F` is
5 and `L` is 11; lowercase continues the run, with `a` = 26 up to `z` = 51.

```
LCHeapMemory   ->  L = 11  ->  "CHeapMemory"
FAlloc         ->  F = 5   ->  "Alloc"
bMatrixScaleRotationXZYTrans  ->  b = 27
```

A `5` before a component makes it an enclosing scope, and any number of them can stack up
before the component the kind character called for:

```
__0f5FsoundMCSoundPlayerPGetPlayerObjectUi
       ^ namespace "sound", class "CSoundPlayer", function "GetPlayerObject"
   sound::CSoundPlayer::GetPlayerObject(unsigned int)
```

### Template arguments

`7` opens a template argument list and `_` closes it. Each argument is a type, or `4` and an
integer for a non-type parameter. They nest.

```
6KTFixString748D512_                 TFixString<512>
6FTRect7f_                           TRect<float>
6KTFixVector76KTFixString7400Y_4g_   TFixVector<TFixString<128>, 32>
```

`9<index>A` refers back to one of the enclosing name's template arguments, 1-based, so `9BA` is
the first. The trailing letter appears to be a nesting level and is always `A` in practice.

```
__0FDmax7f_RC9BATB_RC9BA    const float & max<float>(const float &, const float &)
```

### Integers

Non-type template arguments and array bounds use a compact encoding: a run of `0`s, each worth
52, followed by a letter for the remainder.

| Written | Value | |
| --- | --- | --- |
| `Q` | 16 | |
| `g` | 32 | (lowercase, so 26 + 6) |
| `0M` | 64 | 52 + 12 |
| `00Y` | 128 | 104 + 24 |
| `0000w` | 256 | 208 + 48 |

Values that would need too many `0`s switch to an escape: `8`, a length letter, then that many
decimal digits. `8D512` is 512 and `8E1024` is 1024.

### Operators

For kinds `o` and `O` the name ends with a two-letter code instead of a length-prefixed
identifier. `ct` and `dt` are the constructor and destructor - the class name isn't repeated,
so it has to be taken from the qualifier. `nw` and `dl` take an optional trailing `a` for the
array forms.

Attested: `ct` `dt` `as` `eq` `ne` `ls` `nw` `nwa` `dl` `dla`. The rest of the usual set
(`pl` `mi` `ml` `dv` `md`, the compound assignments, comparisons, `cl`, `vc`, `rf`, …) is
implemented by analogy **(inferred)**.

## Types

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
| `w` | `wchar_t` | **(inferred)** |
| `e` | `...` | varargs |
| `P` | pointer to | |
| `R` | reference to | |
| `C` | `const` | |
| `V` | `volatile` | **(inferred)** |
| `U` | `unsigned` | |
| `S` | `signed` | **(inferred)** |
| `A<n>` | array of `n` | |
| `F<params>_<ret>` | function | |
| `M<class>` | pointer to member of `<class>` | |
| `6<name>` | class, struct or enum | |
| `9<index>A` | template argument `<index>` | |

Note that a class type in a parameter is introduced by `6`, while the symbol's own name isn't -
the kind character has already said a name is coming.

Declarators need wrapping rather than suffixing: `PFv_v` is `void (*)()`, `PPA0Ms` is
`short (**)[64]`, and `PM<class>F…_…` is `ret (Class::*)(args)`.

### Parameter back-references

Two forms repeat an earlier parameter of the same function, both indexed 1-based with letters:

| Form | Meaning |
| --- | --- |
| `T<index>` | Same type as parameter `<index>` |
| `N<count><index>` | `<count>` more parameters, each the type of parameter `<index>` |

```
__0FKRadixSortSPUiNCBUi
   RadixSortS(unsigned int *, unsigned int *, unsigned int *, unsigned int)
```

`NCB` is "two more of parameter 1". Note that `T` is also the static marker below, so a `T`
whose following letter isn't a valid parameter index ends the list rather than repeating one.

## Return types and qualifiers

A `_` after the parameters introduces the return type. Only templates carry one - the same rule
as the other manglings, since two instantiations can differ in return type alone.

After that, a trailing `K` marks a const member function. A trailing `T` marks a **static**
member function: it appears only on kinds `f` and `o`, never on a free function (which wouldn't
need marking), and every member `operator new`/`operator delete` in the corpus carries it,
which is exactly the set of member functions that are implicitly static.

One more marker, `__S`, turns up between the name and the parameters on a couple of template
members. What it means is unknown; it doesn't affect the name, so the demangler skips it.

## Compiler-generated symbols

| Form | Meaning |
| --- | --- |
| `__TID_<type>` | Type id - a one-byte object whose *address* is the type's identity |
| `__T_<type>` | Typeinfo record - 20 bytes, pointing at the name and the base class |
| `__sti__<file>_cpp` | Static initializers for a translation unit |
| `<class>::__vtbl` | Vtable, as an ordinary `__0d` data symbol |

`__TID_` and `__T_` are the two halves of the RTTI for one class, and which is which is
inferred from their sizes rather than from anything that names them. Both take a type rather
than a symbol, and - unlike everything above - spell an enclosing namespace as just another
component instead of marking it with a `5`:

```
__TID_DstdIbad_cast    type id for std::bad_cast
__TID_v                type id for void
```

Secondary vtables for multiple inheritance use a different scheme again, with decimal lengths:
`__vtbl___10CRefObject__5FsoundNCSoundRequest`. Only a handful exist and PPSSPP leaves them
alone.

## Hazards

- **A lot of C runtime code links into the same binary**, and much of it starts with a double
  underscore (`__adddf3`, `__malloc_lock`, `__sti`). Rejecting a symbol that doesn't parse
  completely is what keeps those intact.
- **The kind character is the only thing that says where the name ends.** There is no separator
  between the last name component and the first parameter, and uppercase letters are both
  lengths and type codes, so a parser that guesses will read the wrong thing and still consume
  a plausible number of characters.
- **`T` is overloaded** - a parameter back-reference in the middle of the list, and the static
  marker at the end.
