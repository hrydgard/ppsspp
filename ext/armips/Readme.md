# ARMIPS assembler v0.9
* Author: Kingcom
* Source: https://github.com/Kingcom/armips
* Automated builds: http://buildbot.orphis.net/armips

# 1. Introduction

Note: This file is still incomplete, some information is missing or may be outdated.

## 1.1 Usage

The assembler is called from the command line. There is both an x86 and an x86-64 version. Depending on the version, the usage is as follows:

```
armips code.asm [optional parameters]
armips64 code.asm [optional parameters]
```

`code.asm` is the main file of your assembly code, which can open and include other files.
The following optional command line parameters are supported:

#### `-temp <filename>`
Specifies the output name for temporary assembly data. Example output:
```
; 1 file  included
; test.asm

00000000 .open "SLPM_870.50",0x8000F800  ; test.asm line 1
8000F800 .org 0x800362DC                 ; test.asm line 5
800362DC   jal     0x801EBA3C            ; test.asm line 7
800362E0 .Close                          ; test.asm line 9
```

#### `-sym <filename>`
Specifies the output name for symbol data in the sym format. This format is supported by the debuggers in NO$PSX and NO$GBA. Example output:
```
00000000 0
80000000 .dbl:0010
80000010 main
8000002C subroutine
80240000 newblock
```

#### `-sym2 <filename>`
Specifies the output name for symbol data in the sym2 format. This format is supported by the debuggers in PCSX2 and PPSSPP. Example output:
```
00000000 0
80000000 .dbl:0010
80000010 Main
8000002C Subroutine,0000001C
80240000 NewBlock,00000014
```

#### `-erroronwarning`
Specifies that any warnings shall be treated like errors, preventing assembling. This has the same effect as the `.erroronwarning` directive.

#### `-equ <name> <replacement>`
Equivalent to using `name equ replacement` in the assembly code.

#### `-strequ <name> <replacement>`
Equivalent to using `name equ "replacement"` in the assembly code.

#### `-root <directory>`
Specifies the working directory to be used during execution.

# 2. Installation

## 2.1 Download binary
Download the latest Windows 32-bit binary from the [Automated ARMIPS builds](http://buildbot.orphis.net/armips) site.

## 2.2 Building from source

The latest code is available at the [ARMIPS GitHub repository](https://github.com/Kingcom/armips). Make sure to also initialize and update submodules. This can be accomplished with one command:
```bash
$ git clone --recursive https://github.com/Kingcom/armips.git
```

Build instructions per platform:
* Building on Windows: You will need Visual Studio 2015 (Community Edition is sufficient). Simply open armips.sln, select the desired configuration and platform, and build the solution. Alternatively, you can build using [CMake](https://cmake.org/) with [MSYS2/MinGW](https://msys2.github.io/), but the VS2015 project is the only Windows build officially supported.
* Building on Unix: You will need CMake and a C++11 compliant compiler (recent versions of both gcc and clang have been tested). Create a build directory, invoke CMake from there, and then simply run `make`.
```bash
$ mkdir build && cd build
$ cmake -DCMAKE_BUILD_TYPE=Release ..
$ make
```

# 3. Overview

The assembler includes full support for the MIPS R3000, MIPS R4000, and Allegrex instruction sets, partial support for the EmotionEngine instruction set, as well as complete support for the ARM7 and ARM9 instruction sets, both THUMB and ARM mode. Among the other features of the assembler are:

* a full fledged C-like expression parser. It should behave exactly like in any C/C++ code, including all the weirdness. All immediate values can be specified by an expression, though some directives can't use variable addresses including labels
* you can open several files in a row, but only one output file can be open at any time. You can specify its address in memory to allow overlay support. Any file can cross-reference any other included file
* local, static, and global labels (see [4.3 Labels](#43-labels))
* table support for user defined text encodings (see [4.7 Strings](#47-strings))
* several MIPS macros to make writing code easier and faster (see [5.1 General directives](#51-general-directives))
* user defined macros (see [6.3 User defined macros](#63-user-defined-macros))
* built-in checks for possible load delay problems (see [4.6 Load delay detection](#46-load-delay-detection))
* optional automatic fix for said problems by inserting a nop between the instructions
* output of the assembled code to a text file, with memory addresses and origin (see [1.1 Usage](#11-usage))
* a directive to ensure that data is not bigger than a user defined size (see [4.8 Areas](#48-areas))

# 4. Features

## 4.1 Files

Unlike other assemblers, you don't specify the input/output file as a command line argument. You have to open the file in the source code, and also close it yourself. This was done in order to support overlays, which are very common in PSX and NDS games. Instead of only having one output file, you can have as many as you need - each with its own address in memory. The files can cross-reference each other without any problems, so you can call code from other files that are currently not opened as well.

```
.Open "SLPS_035.71", 0x8000F800
; ...
.Close
.Open "System\0007.dat", 0x800CC000
; ...
.Close
```

## 4.2 Syntax

### Comments
Both `;` and `//` style single-line comments are supported.
`/* */` style block comments are also accepted.

### Statement separator
Statements are separated by newlines or `::` can be used between statements on the same line. For example, to insert four `nop` instructions, this could be written on one line:
```
nop :: nop :: nop :: nop
```

### Statement line spanning
Single statements can continue on to the next line by inserting a `\` at the end of a line. Comments and whitespace can follow. For example:
```
addiu t3, t4, \
 FunctionJumpTable - headersize() + 0x1000 * filesize("blob.bin")
```

## 4.3 Labels

There is support for both local, global and static labels. Local labels are only valid in the area between the previous and the next global label. Specific directives, like `.org`, will also terminate the area. A label is defined by writing a colon after its name. All labels can be used before they are defined.

```
GlobalLabel:       ; This is a global label
@@LocalLabel:      ; This is a local label, it is only
                   ; valid until the next global one
OtherGlobalLabel:  ; this will terminate the area where
                   ; @@LocalLabel can be used
  b   @@LocalLabel ; as a result, this will cause an error
```

Static labels behave like global labels, but are only valid in the very file they were defined. Any included files or files that include it cannot reference it. They can, however, contain another static label with the same name.

```
@StaticLabel:
```

A label name can contain all characters from A-Z, numbers, and underscores. However, it cannot start with a digit. All label names are case insensitive.

Additionally, `.` can be used to reference the current memory address.

## 4.4 equ

The `equ` directive works as a text replacement and is defined as follows:

```
@@StringPointer equ 0x20(r29)
```

There has to be a space before and after `equ`. The assembler will replace any occurrence of `@@StringPointer` with `0x20(r29)`. As it is a local `equ`, it will only do so in the current section, which is terminated by any global label or specific directives. This code:

```
@@StringPointer equ 0x20(r29)

  lw  a0,@@StringPointer
  nop
  sw  a1,@@StringPointer
```

will assemble to this:

```
  lw  a0,0x20(r29)
  nop
  sw  a1,0x20(r29)
```

There can be both global and local `equ` directives, but unlike normal labels, they must be defined before they are used.


## 4.5 Expression parser

A standard expression parser with operator precedence and bracket support has been implemented. It is intended to behave exactly like any C/C++ parser and supports all unary, binary and ternary operators of the C language. Every numeral argument can be given as an expression, including label names. However, some directives do not support variable addresses, so labels cannot be used in expressions for them. The following bases are supported:

* `0xA` and `0Ah` for hexadecimal numbers
* `0o12` and `12o` for octal numbers
* `1010b` and `0b1010` for binary numbers

Everything else is interpreted as a decimal numbers, so a leading zero does not indicate an octal number. Be aware that every number has to actually start with a digit. For example, as `FFh` is a perfectly valid label name, you have to write `0FFh` in this case. Labels, on the other hand, cannot start with a digit.

A few examples:

```
  mov  r0,10+0xA+0Ah+0o12+12o+1010b
  ldr  r1,=ThumbFunction+1
  li   v0,Structure+(3*StructureSize)
```

### Built-in functions

Below is a table of functions built into the assembler that can be used with the expression parser for runtime computation.

| Function | Description |
|----------|-------------|
| `version()` | armips version encoded as int |
| `endianness()` | current endianness as string `"big"` or `"little"` |
| `outputname()` | currently opened output filename |
| `org()` | current memory address (like `.`) |
| `orga()` | current absolute file address |
| `headersize()` | current header size (displacement of memory address against absolute file address) |
| `fileexists(file)` | `1` if `file` exists, `0` otherwise |
| `filesize(file)` | size of `file` in bytes |
| `tostring(val)` | string representation of int or float `val` |
| `tohex(val, optional digits = 8)` | hex string representaion of int `val` |
| `round(val)` | float `val` rounded to nearest int |
| `int(val)` | cast float `val` to int, dropping fractional part |
| `float(val)` | cast int `val` to float |
| `frac(val)` | fractional part of float `val` |
| `abs(val)` | absolute value of int or float `val` |
| `hi(val)` | High half of 32-bit value `val`, adjusted for sign extension of low half (MIPS) |
| `lo(val)` | Sign-extended low half of 32-bit value `val` (MIPS) |
| `strlen(str)` | number of characters in `str` |
| `substr(str, start, count)` | substring of `str` from `start`, length `count` |
| `regex_match(source, regex)` | `1` if `regex` matched entire `source`, `0` otherwise|
| `regex_search(source, regex)` | `1` if `regex` matched subsequence of `source`, `0` otherwise|
| `regex_extract(source, regex, optional index = 0)` | string of `regex` matched in `source` |
| `find(source, substr, optional start = 0)` | lowest index of `substr` in `source` from `start`, else `-1` |
| `rfind(source, substr, optional start = -1)` | highest index of `substr` in `source` from `start`, else `-1` |
| `readbyte(file, optional pos = 0)` | read unsigned 8-bit value from `file` at position `pos` |
| `readu8(file, optional pos = 0)` | read unsigned 8-bit value from `file` at position `pos` |
| `readu16(file, optional pos = 0)` | read unsigned 16-bit value  from `file` at position `pos` |
| `readu32(file, optional pos = 0)` | read unsigned 32-bit value  from `file` at position `pos` |
| `readu64(file, optional pos = 0)` | read unsigned 64-bit value  from `file` at position `pos` |
| `reads8(file, optional pos = 0)` | read signed 8-bit value from `file` at position `pos` |
| `reads16(file, optional pos = 0)` | read signed 16-bit value from `file` at position `pos` |
| `reads32(file, optional pos = 0)` | read signed 32-bit value from `file` at position `pos` |
| `reads64(file, optional pos = 0)` | read signed 64-bit value from `file` at position `pos` |
| `readascii(file, optional start = 0, optional len = 0)` | read ASCII string from `file` at `start` length `len` |
| `isarm()` | `1` if in ARM mode, `0` otherwise |
| `isthumb()` | `1` if in Thumb mode, `0` otherwise |

## 4.6 Load delay detection

This feature is still unfinished and experimental. It works in most cases, though. On certain MIPS platforms (most notably the PlayStation 1), any load is asynchronously delayed by one cycle and the CPU won't stall if you attempt to use it before. Attempts to use it will return the old value on an actual system (emulators usually do not emulate this, which makes spotting these mistakes even more difficult). Therefore, the assembler will attempt to detect when such a case happens. The following code would result in a warning:

```
  lw   a0,0x10(r29)
  lbu  a1,(a0)
```

This code doesn't take the load delay into account and will therefore only work on emulators. The assembler detects it and warns the user. In order to work correctly, the code should look like this:

```
  lw   a0,0x10(r29)
  nop
  lbu  a1,(a0)
```

The assembler can optionally automatically insert a `nop` when it detects such an issue. This can be enabled with the [`.fixloaddelay`](#load-delay) directive.
However, as there is no control flow analysis, there is a chance of false positives. For example, a branch delay slot may cause a warning for the opcode that follows it, even if there is no chance that they will be executed sequentially. The following example illustrates this:

```
  bnez  a0,@@branch1
  nop
  j     @@branch2
  lw    a0,(a1)
@@branch1:
  lbu   a2,(a0)
```

You can fix the false warning by using the [`.resetdelay`](#load-delay) directive before the last instruction.

```
  bnez  a0,@@branch1
  nop
  j     @@branch2
  lw    a0,(a1)
.resetdelay
@@branch1:
  lbu   a2,(a0)
```

## 4.7 Strings

You can write ASCII text by simply using the `.db`/`.ascii` directive. However, you can also write text with custom encodings. In order to do that, you first have to load a table using [`.loadtable <tablefile>`](#load-a-table-specifying-a-custom-encoding), and then use the [`.string`](#write-text-with-custom-encoding) directive to write the text. It behaves exactly like the `.db` instruction (so you can also specify immediate values as arguments), with the exception that it uses the table to encode the text, and appends a termination sequence after the last argument. This has to be specified inside the table, otherwise 0 is used.

```
.loadtable "custom.tbl"
.string "Custom text",0xA,"and more."
```

The first and third argument are encoded according to the table, while the second one is written as-is.

Quotation marks can be escaped by prefixing them with a backslash. Any backlash not followed by a quotation mark is kept as-is. If you want to use a backslash at the end of a string, prefix it by another backlash.
For example, to write a quotation mark followed by a backlash:

```
.ascii "\"\\"
```

## 4.8 Areas

If you overwrite existing data, it is critical that you don't overwrite too much. The area directive will take care of checking if all the data is within a given space. In order to do that, you just have to specify the maximum size allowed.

```
.area 10h
  .word 1,2,3,4,5
.endarea
```

This would cause an error on assembling, because the word directive takes up 20 bytes instead of the 16 that the area is allowed to have. This, on the other hand, would assemble without problems:

```
.org 8000000h
.area 8000020h-.
  .word 1,2,3,4,5
.endarea
```

Here, the area is 32 bytes, which is sufficient for the 20 bytes used by .word.
Optionally, a second parameter can be given. The remaining free size of the area will then be completely filled with bytes of that value.

## 4.9 Symbol files

Functions.

## 4.10 C/C++ importer

You can link object files or static libraries in ELF format. The code and data is relocated to the current output position and all of its symbols are exported. You can in turn use armips symbols inside of your compiled code by declaring them as `extern`. Note: As armips labels are case insensitive, the exported symbols are treated the same way. Be aware of name mangling when trying to reference C++ functions, and consider declaring them as `extern "C"`.

```
.importobj "code.o"
```

You can optionally supply names for constructor and destructor functions. Functions with those names will be generated that call of the global constructors/destructors of the imported files.

```
.importlib "code.a",globalConstructor,globalDestructor
```

# 5. Assembler directives

These commands tell the assembler to do various things like opening the output file or opening another source file.

## 5.1 General directives

### Set the architecture

These directives can be used to set the architecture that the following assembly code should be parsed and output for. The architecture can be changed at any time without affecting the preceding code.

| Directive | System | Architecture | Comment |
| --------- |:-------|:-------------|:--------|
| `.psx` | PlayStation 1 | MIPS R3000 | - |
| `.ps2` | PlayStation 2 | EmotionEngine | - |
| `.psp` | PlayStation Portable | Allegrex | - |
| `.n64` | Nintendo 64 | MIPS R4000 | - |
| `.rsp` | Nintendo 64 | RSP | - |
| `.gba` | GameBoy Advance | ARM7 | Defaults to THUMB mode |
| `.nds` | Nintendo DS | ARM9 | Defaults to ARM mode |
| `.3ds` | Nintendo 3DS | ARM11 | Defaults to ARM mode, incomplete |
| `.arm.big` | - | ARM | Output in big endian |
| `.arm.little` | - | ARM | Output in little endian |

### Open a generic file

```
.open FileName,Offset
.open OldFileName,NewFileName,Offset
```

Opens the specified file for output. If two file names are specified, then the assembler will copy the file specified by the file name to the second path. If relative include is off, all paths are relative to the current working directory. Otherwise the path is relative to the including assembly file. `Offset` specifies the difference between the first byte of the file and its position in memory. So if file position 0x800 is at position 0x80010000 in memory, the header size is 0x80010000-0x800=0x8000F800. It can be changed later with the [`.headersize`](#changing-the-header-size) directive.
Only the changes specified by the assembly code will be inserted, the rest of the file remains untouched.

### Create a new file

```
.create FileName,Offset
.createfile FileName,Offset
```

Creates the specified file for output. If the file already exists, it will be overwritten. If relative include is off, all paths are relative to the current working directory. Otherwise the path is relative to the including assembly file. `Offset` specifies the difference between the first byte of the file and its position in memory. So if file position 0x800 is at position 0x80010000 in memory, the header size is 0x80010000-0x800=0x8000F800. It can be changed later with the [`.headersize`](#changing-the-header-size) directive.

### Close a file

```
.close
.closefile
```

Closes the currently opened output file.

### Set the output position

```
.org RamAddress
.orga FileAddress
```

Sets the output pointer to the specified address. `.org` specifies a memory address, which is automatically converted to the file address for the current output file. `.orga` directly specifies the absolute file address.

### Change the header size

```
.headersize Offset
```

Sets the header size to the given value which is the difference between the file position of a byte and its address in memory. This is used to calculate all addresses up until the next `.headersize` or `.open`/`.create` directive. The current memory address will be updated, but the absolute file offset will remain the same. The header size can be negative so long as the resulting memory address remains positive.

### Include another assembly file

```
.include FileName[,encoding]
```

Opens the file called `FileName` to assemble its content. If relative include is off, all paths are relative to the current working directory. Otherwise the path is relative to the including assembly file. You can include other files up to a depth level of 64. This limit was added to prevent the assembler from getting stuck in an infinite loop due to two files including each other recursively. If the included file has an Unicode Byte Order Mark then the encoding will be automatically detected. If no Byte Order Mark is present it will default to UTF-8. This can be overwritten by manually specifying the file encoding as a second parameter.

The following values  are supported:
* `SJIS`/`Shift-JIS`
* `UTF8`/`UTF-8`
* `UTF16`/`UTF-16`
* `UTF16-BE`/`UTF-16-BE`
* `ASCII`

## Text and data directives

### Align the output position

```
.align num
```

Writes zeros into the output file until the output position is a multiple of `num`. `num` has to be a power of two.

### Fill space with a value

```
.fill length[,value]
defs length[,value]
```

Inserts `length` amount of bytes of `value`. If `value` isn't specified, zeros are inserted. Only the lowest 8 bits of `value` are inserted.

### Skip bytes

```
.skip length
```

Skips `length` amount of bytes without overwriting them.

### Include a binary file

```
.incbin FileName[,start[,size]]
.import FileName[,start[,size]]
```

Inserts the file specified by `FileName` into the currently opened output file. If relative include is off, all paths are relative to the current working directory. Otherwise the path is relative to the including assembly file. Optionally, start can specify the start position in the file from it should be imported, and size can specify the number of bytes to read.

### Write bytes

```
.byte value[,...]
.db value[,...]
.ascii value[,...]
.asciiz value[,...]
dcb value[,...]
```

Inserts the specified sequence of bytes. Each parameter can be any expression that evaluates to an integer or a string. If it evaluates to an integer, only the lowest 8 bits are inserted. If it evaluates to a string, every character is inserted as a byte. `.asciiz` inserts a null terminator after the string, while the others omit it.

### Write halfwords

```
.halfword value[,...]
.dh value[,...]
dcw value[,...]
```

Inserts the specified sequence of 16-bit halfwords. Each parameter can be any expression that evaluates to an integer or a string. If it evaluates to an integer, only the lowest 16 bits are inserted. If it evaluates to a string, every character is inserted as a halfword.


### Write words

```
.word value[,...]
.dw value[,...]
dcd value[,...]
```

Inserts the specified sequence of 32-bit words. Each parameter can be any expression that evaluates to an integer, a string, or a floating point number. If it evaluates to an integer, only the lowest 32 bits are inserted. If it evaluates to a string, every character is inserted as a word. Floats are inserted using an integer representation of the single-precision float's encoding.

### Write doublewords

```
.doubleword value[,...]
.dd value[,...]
dcq value[,...]
```

Inserts the specified sequence of 64-bit doublewords. Each parameter can be any expression that evaluates to an integer, a string, or a floating point number. If it evaluates to a string, every character is inserted as a doubleword. Floats are inserted using an integer representation of the double-precision float's encoding.

### Write floating point numbers

```
.float value[,...]
.double value[,...]
```

`.float` inserts the specified sequence of single-precision floats and `.double` inserts double-precision floats. Each parameter can be any expression that evaluates to an integer or a floating point number. If it evaluates to an integer, it will be converted to a floating point number of that value.

### Load a table specifying a custom encoding

```
.loadtable TableName[,encoding]
.table TableName[,encoding]
```

Loads `TableName` for using it with the `.string` directive. The encoding can be specified in the same way as for `.include`.

The table file format is a line-separated list of key values specified by `hexbyte=string` and optional termination byte sequence by `/hexbytes`

```
02=a
1D=the
2F=you
/FF
```

`FF` will be used as the termination sequence. If it is not given, zero is used instead. Strings are matched using the longest prefix found in the table.


### Write text with custom encoding

```
.string "String"[,...]
.stringn "String"[,...]
.str "String"[,...]
.strn "String"[,...]
```

Inserts the given string using the encoding from the currently loaded table. `.string` and `.str` insert the termination sequence specified by the table after the string, while `.stringn` and `.strn` omit it.

### Write text with Shift-JIS encoding

```
.sjis "String"[,...]
.sjisn "String"[,...]
```

Inserts the given string using the Shift-JIS encoding. `.sjis` inserts a null byte after the string, while `.sjisn` omits it.

## Conditional directives

### Begin a conditional block

```
.if cond
.ifdef identifier
.ifndef idenifier
```

The content of a conditional block will only be used if the condition is met. In the case of `.if`, it is met of `cond` evaluates to non-zero integer. `.ifdef` is met if the given identifier is defined anywhere in the code, and `.ifndef` if it is not.

### Else case of a conditional block

```
.else
.elseif cond
.elseifdef identifier
.elseifndef identifier
```

The else block is used if the condition of the condition of the if block was not met. `.else` unconditionally inserts the content of the else block, while the others start a new if block and work as described before.

### End a conditional block

```
.endif
```

Ends the last open if or else block.

### Define labels

```
.definelabel Label,value
```

Defines `Label` with a given value, creating a symbol for it. This can be used similar to `equ`, but symbols can be used before labels are defined and can be used in conjunction with the `.ifdef/.ifndef` conditionals. These can also be useful for declaring symbols for existing code and data when inserting new code.

### Areas

```
.area SizeEquation[,fill]
.endarea
```

Opens a new area with the maximum size of `SizeEquation`. If the data inside the area is longer than this maximum size, the assembler will output an error and refuse to assemble the code. The area is closed with the `.endarea` directive and if `fill` parameter is provided, the remaining free space in the area will be filled with bytes of that value.

### Messages
```
.warning "Message"
.error "Message"
.notice "Message"
```

Prints the message and sets warning/error flags. Useful with conditionals.

## 5.2 MIPS directives

### Load delay

```
.resetdelay
```

Resets the current load delay status. This can be useful if the instruction after a delay slot access the delayed register, as the assembler can't detect that yet.

```
.fixloaddelay
```

Automatically fixes any load delay problems by inserting a `nop` between the instructions. Best used in combination with `.resetdelay`.

```
.loadelf name[,outputname]
```

Opens the specified ELF file for output. If two file names are specified, then the assembler will copy the first file to the second path. If relative include is off, all paths are relative to the current working directory, so from where the assembler was called. Otherwise the path is relative to the including assembly file. All segments are accessible by their virtual addresses, and all unmapped sections can be accessed by their physical position (through `.orga`).
Currently this is only supported for the PSP architecture, and only for non-relocateable files. The internal structure of the file may be changed during the process, but this should not affect its behavior.

## 5.3 ARM Directives

### Change instruction set

```
.arm
.thumb
```

These directives can be used to select the ARM or THUMB instruction set. `.arm` tells the assembler to use the full 32 bit ARM instruction set, while `.thumb` uses the cut-down 16 bit THUMB instruction set.

### Pools

```
.pool
```

This directive works together with the pseudo opcode `ldr rx,=value`. The immediate is added to the nearest pool, and the instruction is turned into a PC relative load. The range is limited, so you may have to define several pools.
Example:

```
ldr  r0,=0xFFEEDDCC
; ...
.pool
```

`.pool` will automatically align the position to a multiple of 4.

### Debug messages

```
.msg
```

Inserts a no$gba debug message as described by GBATEK.

# 6. Macros

## 6.1 Assembler-defined MIPS macros

There are various macros built into the assembler for ease of use. They are intended to make using some of the assembly simpler and faster.
At the moment, these are all the MIPS macros included:

### Immediate macros

```
li   reg,Immediate
la   reg,Immediate
```

Loads Immediate into the specified register by using a combination of `lui`/`ori`, a simple `addiu`, or a simple `ori`, depending on the value of the Immediate.

### Immediate float macros

```
li.s reg,Immediate
```

Loads float value Immediate into the specified FP register by using a combination of `li` and `mtc1`.

### Memory macros

```
lb   reg,Address
lbu  reg,Address
lh   reg,Address
lhu  reg,Address
lw   reg,Address
lwu  reg,Address
ld   reg,Address
lwc1 reg,Address
lwc2 reg,Address
ldc1 reg,Address
ldc2 reg,Address
```

Loads a byte/halfword/word from the given address into the specified register by using a combination of `lui` and `lb`/`lbu`/`lh`/`lhu`/`lw`/`ld`/`lwc1`/`lwc2`/`ldc1`/`ldc2`.

```
ulh  destreg,imm(sourcereg)
ulh  destreg,(sourcereg)
ulhu destreg,imm(sourcereg)
ulhu destreg,(sourcereg)
ulw  destreg,imm(sourcereg)
ulw  destreg,(sourcereg)
uld  destreg,imm(sourcereg)
uld  destreg,(sourcereg)
```

Loads an unaligned halfword/word/doubleword from the address in sourcereg by using a combination of several `lb`/`lbu` and `ori` or `lwl`/`lwr` or `ldl`/`ldr` instructions.

```
sb   reg,Address
sh   reg,Address
sw   reg,Address
sd   reg,Address
swc1 reg,Address
swc2 reg,Address
sdc1 reg,Address
sdc2 reg,Address
```

Stores a byte/halfword/word/doubleword to the given address by using a combination of `lui` and `sb`/`sh`/`sw`/`sd`/`swc1`/`swc2`/`sdc1`/`sdc2`.

```
ush  destreg,imm(sourcereg)
ush  destreg,(sourcereg)
usw  destreg,imm(sourcereg)
usw  destreg,(sourcereg)
usd  destreg,imm(sourcereg)
usd  destreg,(sourcereg)
```

Stores an unaligned halfword/word/doubleword to the address in sourcereg using a combination of several `sb`/`sbu` and shifts or `swl`/`swr`/`sdl`/`sdr` instructions.

### Branch macros

```
blt   reg1,reg2,Dest
bltu  reg1,reg2,Dest
bgt   reg1,reg2,Dest
bgtu  reg1,reg2,Dest
bge   reg1,reg2,Dest
bgeu  reg1,reg2,Dest
ble   reg1,reg2,Dest
bleu  reg1,reg2,Dest
bltl  reg1,reg2,Dest
bltul reg1,reg2,Dest
bgtl  reg1,reg2,Dest
bgtul reg1,reg2,Dest
bgel  reg1,reg2,Dest
bgeul reg1,reg2,Dest
blel  reg1,reg2,Dest
bleul reg1,reg2,Dest
blt   reg,Imm,Dest
bltu  reg,Imm,Dest
bgt   reg,Imm,Dest
bgtu  reg,Imm,Dest
bge   reg,Imm,Dest
bgeu  reg,Imm,Dest
ble   reg,Imm,Dest
bleu  reg,Imm,Dest
bne   reg,Imm,Dest
beq   reg,Imm,Dest
bltl  reg,Imm,Dest
bltul reg,Imm,Dest
bgtl  reg,Imm,Dest
bgtul reg,Imm,Dest
bgel  reg,Imm,Dest
bgeul reg,Imm,Dest
blel  reg,Imm,Dest
bleul reg,Imm,Dest
bnel  reg,Imm,Dest
beql  reg,Imm,Dest
```

If reg/reg1 is less than/greater than or equal to/equal to/not equal to reg2/Imm, branches to the given address. A combination of `sltu` and `beq`/`bne` or `li`, `sltu` and `beq`/`bne` is used.

### Set macros

```
slt   reg1,reg2,Imm
sltu  reg1,reg2,Imm
sgt   reg1,reg2,Imm
sgtu  reg1,reg2,Imm
sge   reg1,reg2,Imm
sgeu  reg1,reg2,Imm
sle   reg1,reg2,Imm
sleu  reg1,reg2,Imm
sne   reg1,reg2,Imm
seq   reg1,reg2,Imm
sge   reg1,reg2,reg3
sgeu  reg1,reg2,reg3
sle   reg1,reg2,reg3
sleu  reg1,reg2,reg3
sne   reg1,reg2,reg3
seq   reg1,reg2,reg3
```

If reg2 is less than/greater than or equal to/equal to/not equal to reg3/Imm, sets reg1 to `1`, otherwise sets reg1 to `0`. Various combinations of `li`, `slt`/`sltu`/`slti`/`sltiu` and `xor`/`xori` are used.

### Rotate macros

```
rol    reg1,reg2,reg3
ror    reg1,reg2,reg3
rol    reg1,reg2,Imm
ror    reg1,reg2,Imm
```

Rotates reg2 left/right by the value of the lower 5 bits of reg3/Imm and stores the result in reg1. A combination of `sll`, `srl` and `or` is used.

### Absolute value macros

```
abs   reg1,reg2
dabs  reg1,reg2
```

Stores absolute value of word/doubleword in reg2 into reg1 using a combination of `sra`/`dsra32`, `xor`, and `subu`/`dsubu`.

### Upper/lower versions

Additionally, there are upper and lower versions for many two opcode macros. They have the same names and parameters as the normal versions, but `.u` or `.l` is appended at the end of the name.
For example, `li.u` will output the upper half of the `li` macro, and `li.l` will output the lower half. The following macros support this: `li`,`la`,`lb`,`lbu`,`lh`,`lhu`,`lw`,`lwu`,`ld`,`lwc1`,`lwc2`,`ldc1`,`ldc2`,`sb`,`sh`,`sw`,`sd`,`swc1`,`swc2`,`sdc1`,`sdc2`

This can be used when the two halves of the macros need to be used in nonconsecutive positions, for example:

```
li.u  a0,address
jal   function
li.l  a0,address
```

## 6.2 Assembler-defined ARM macros

The assembler will automatically convert the arguments between the following opcodes if possible:

```
mov <-> mvn
bic <-> and
cmp <-> cmn
```

E.g., `mov r0,-1` will be assembled as `mvn r0,0`

Additionally, `ldr rx,=immediate` can be used to load a 32-bit immediate. The assembler will try to convert it into a mov/mvn instruction if possible. Otherwise, it will be stored in the nearest pool (see the .pool directive). `add rx,=immediate` can be used as a PC-relative add and will be assembled as `add rx,r15,(immediate-.-8)`


## 6.3 User defined macros

The assembler allows the creation of custom macros. This is an example macro, a recreation of the builtin MIPS macro `li`:

```
.macro myli,dest,value
   .if value & ~0xFFFF
      ori   dest,r0,value
   .elseif (value & 0xFFFF8000) == 0xFFFF8000
      addiu dest,r0,value & 0xFFFF
   .elseif (value & 0xFFFF) == 0
      lui   dest,value >> 16
   .else
      lui   dest,value >> 16 + (value & 0x8000 != 0)
      addiu dest,dest,value & 0xFFFF
   .endif
.endmacro
```

The macro has to be initiated by a `.macro` directive. The first argument is the macro name, followed by a variable amount of arguments. The code inside the macro can be anything, and it can even call other macros (up to a nesting level of 128 calls). The macro is terminated by a `.endmacro` directive. It is not assembled when it is defined, but other code can call it from then on. All arguments are simple text replacements, so they can be anything from a number to a whole instruction parameter list. The macro is then invoked like this:

```
myli  a0,0xFFEEDDCC
```

In this case, the code will assemble to the following:

```
lui   a0,0xFFEF
addiu a0,a0,0xDDCC
```

Like all the other code, any equs are inserted before they are resolved.

Macros can also contain local labels that are changed to an unique name. Global labels, however, are unaffected by this. The label name is prefixed by the macro name and a counter id. This label:

```
.macro test
  @@MainLoop:
.endmacro
```

will therefore be changed to:

```
@@test_00000000_mainloop
```

Each call of the macro will increase the counter.

# 7. Meta

## 7.1 Change log

* Version 0.9
    * huge rewrite with many enhancements and fixes
    * can now read from UTF8, UTF16, and Shift-JIS files and convert the input correctly
    * several new MIPS pseudo-ops, new COP0 and FPU control register types
    * Nintendo 64 CPU + RSP support
    * PSP support, load ELFs with `.loadelf`
    * able to import and relocate static C/C++ libraries
    * new `-sym2` format for use with PPSSPP and PCSX2
    * new directives: `.sym`, `.stringn`, `.sjis`, `.sjisn`, `.function`, `.endfunction`, `.importlib`, `.loadelf`, `.float`, `.dd`, `.double`
    * removed directives: `.ifarm`, `.ifthumb`, `.radix`
    * added support for floats in data directives
    * added expression functions
    * variable expressions supported in `.org`/`.orga`/`.headersize`
    * new statement syntax with `::` as separator and `\` as line continuation.
* Version 0.7d
    * added automatic optimizations for several ARM opcodes
    * many bugfixes and internal changes
    * added static labels
    * new directives: `.warning`, `.error`, `.notice`, `.relativeinclude`, `.erroronwarning`, `.ifarm`, `.ifthumb`
    * quotation marks can now be escaped in strings using `\"`.
* Version 0.7c
    * Macros can now contain unique local labels
    * `.area` directive added
    * countless bugfixes
    * no$gba debug message support
    * full no$gba sym support
* Version 0.7b
    * ARM/THUMB support
    * fixed break/syscall MIPS opcodes
    * added check if a MIPS instruction is valid inside a delay slot
    * fixed and extended base detection
    * added `.` dummy label to the math parser to get the current memory address
    * added `dcb`/`dcw`/`dcd` directives
* Version 0.5b
    * Initial release

## 7.2 Migration from older versions

There are several changes after version 0.7d that may break compatibility with code written for older versions. These are as follows:

* String literals now require quotation marks, e.g. for file names
* `$XX` is no longer supported for hexadecimal literals

## 7.3 License

MIT Copyright (c) 2009-2017 Kingcom: [LICENSE.txt](LICENSE.txt)
