# Memory Scanner

A small "Cheat Engine"-style value search for PSP user memory, made to be usable with touch.

## Enabling

1. Enable cheats (Game settings > System > Enable Cheats).
2. Open the Cheats menu from the pause menu and tick **Memory Scanner**.
3. Back in the game, a **Memory Scanner** button appears at the top center of the screen.

The button and the scanner only work while cheats are enabled, and not in RetroAchievements
hardcore mode.

## What is scanned

**Scan options** > **Search in** picks it, for each new search. What the current search covers
(or a new one would) is shown at the top of the scanner.

- **User memory except code** (default): all of user memory, without the code of the game's
  modules. A value is almost never in code, and changing code by mistake crashes games. Values
  found in memory the game allocated may move later (new level, restart): use a pointer scan
  for those.
- **Globals of one module**: only the static memory of one module (its `.data` and `.bss`, and
  any other memory it reserves up to its end), which never moves, so plain cheats made from it
  keep working. The main module (the game's EBOOT, the first one
  loaded) is picked first; **Module** lists the others the game has loaded, with their size.
  Pick the big one when the EBOOT only loads the real game from a PRX. Much faster, but many
  games keep their values in allocated memory, and then nothing is found: the scanner says so
  and suggests switching to the default. Pointer scans also only start from this module's
  globals.
- **All user memory**: everything, code and read-only data included.

User memory goes from `0x08800000` to the end of emulated RAM. Normally that's 24 MB, but on a
PSP-2000+ model, for games, homebrew or HD remasters that use the extra RAM, it goes up to
`0x0C000000` (56 MB).

Code means the code sections of each module the game loaded as a module. Code a game copies
into memory by itself isn't known, and is searched. Read-only data tables (item stats and such)
are searched, so they can still be found.

**Scan options** > **Game modules** lists every loaded module: its memory, each code section,
and its globals. Some games reserve far more static memory than their `.data` and `.bss`
(Monster Hunter Portable 3rd HD keeps over 20 MB there, between its data and one last code
section), and all of it counts as globals.

If the chosen module moves or is unloaded during a search, the next scan says so, and a new
search is needed.

Values are searched at their natural alignment: 8-bit at every byte, 16-bit every 2 bytes,
32-bit and float every 4 bytes.

## Searching

- **Value type** (8-bit, 16-bit, 32-bit, Float) is picked for a new search, and stays fixed
  until **New search**.
- **Scan type** can be changed before every scan:
  - First scan: Exact value, Not equal to, Bigger than, Smaller than, Value between,
    Unknown initial value.
  - Next scans: all of the above (except Unknown initial value), plus Increased value,
    Decreased value, Increased value by, Decreased value by, Changed value, Unchanged value.
    These compare with the values at the previous scan.
- The value can be changed before every scan. Type it with the on-screen keypad, or use
  **Keyboard** for the system keyboard. **Hex** switches the value to hexadecimal: it adds the
  `0x` prefix, converts what was typed and shows the A-F keys (**Dec** converts back). Typed `0x` or `$` also means
  hex. `-` works for any size (so `-1` finds `0xFF` in 8-bit).
- Floats use a rounded compare: `95` matches 94.5 to 95.5, `95.25` matches 95.245 to 95.255.
  A hex value for a float matches its exact bits.
- Integers are compared unsigned for Bigger/Smaller/Between.
- **Undo** goes back one scan.

Only the addresses from the previous results are checked by a next scan. Results stay in RAM
until a new search, or the game is closed, so you can close the scanner, play, and come back.

The game is always stopped while a scan runs. Tick **Scan options** > **Pause emulation while the scanner is
open** (on by default) to also keep it paused while browsing results. With it off, the game
keeps running behind the scanner.

## Results

- More than 20 results: only the count is shown. Change the value in-game and scan again.
- 20 or fewer: the addresses are listed with their current values, refreshed every second,
  and the value at the last scan when it differs.

Each listed result has a **Freeze** checkbox. Ticking it holds the address at its current
value: it's written back every time cheats run (the cheat refresh interval), so you can test
what an address does without making a cheat first. Frozen values are shown in a **Frozen
values** section above the results, with their own checkboxes and **Unfreeze all**, and stay
frozen through new searches until unfrozen or the game is closed. Like cheats, freezing only
works while cheats are enabled. While the scanner keeps the game paused, freezing still
writes the value right away.

Tap a result to:

- Type a new value and press **Set** to write it to game memory right away. If the address
  is frozen, it's held at the new value from then on.
- Tick **Freeze** to hold the typed value (or the current one, if nothing valid is typed).
- Type a name and press **Add** to save the address and value as a CwCheat code in the game's
  cheat file (`PSP/Cheats/<DISC_ID>.ini`). It's added disabled; turn it on in the Cheats menu.
  The code written is `_L 0x0XXXXXXX` (8-bit), `_L 0x1XXXXXXX` (16-bit) or `_L 0x2XXXXXXX`
  (32-bit and float).

Keep in mind that values found this way can move around (a new level, a reloaded save), so a
cheat made from them may only work in some situations. That's what pointer scans are for.

## Pointer scans

Many PSP games keep a lot of their state at fixed addresses (the game's own static data), and a
plain cheat works fine there. Values in objects the game allocates (per level, per character)
can be somewhere else every time. The game finds them through pointers, and a pointer cheat
(CwCheat code type `0x6`) can follow the same pointers.

1. Find the value with a normal search, tap its address and press **Find pointers to this
   address**.
2. Pick **Max pointer depth** (how many pointers in a row, default 2) and **Max offset** (how
   far the value can be from where a pointer points, default `0x1000`), then **Scan for pointers
   to this address**.
   The scan only keeps chains that start in the static data (`.data`/`.bss`) of the game's
   modules, since those stay at the same place (only the chosen module's, when **Search in** is
   set to **Globals of one module**). Code isn't searched: MIPS jump instructions
   look exactly like pointers.
3. The first scan usually finds many chains, and most of them are only right by chance.
   Change level, or restart the game (loading a savestate doesn't help: it restores the same
   memory layout). Find the value again with a normal search, tap its new address, and use
   **Pointer scan (filter with this address)** > **Filter with this address**. Only the chains
   that now lead to the new address are kept. Repeat until only a few are left.
4. Tap a chain to set the value through it, freeze it, or **Add** it as a pointer cheat. Chains
   can also be frozen straight from the list.

The shortest chains are listed first, and up to 20 are shown even when there are more.
Pointer results stay in RAM until a new pointer scan, **Clear pointer results**, a different
game, or PPSSPP closes. Closing, resetting or restarting the same game keeps them, since that's
how they're filtered. **Pointer scan results** in the main scanner window opens them again, as
does tapping any found address.

Larger depths and offsets find more chains, but take longer, use more RAM, and give more false
ones. If the scan stops at its limit, it says so; use a smaller depth or offset.

The cheat written for `[[0x08912340] + 0x10] + 0x1C`, setting a 32-bit value to 999:

```
_L 0x60112340 0x000003E7   first pointer (relative to 0x08800000), value
_L 0x00020002 0x0000001C   2 = 32-bit (0 = 8-bit, 1 = 16-bit), 2 = lines in this pointer code, last offset
_L 0x20000010 0x00000000   follow one more pointer, at +0x10
```

Things to keep in mind:

- During loading screens and menus a pointer can be null or point at freed memory. If a pointer
  on the way doesn't lead to valid memory, PPSSPP doesn't write at all, but a pointer to memory
  that's been reused for something else can't be told apart from a good one.
- A chain may lead to "the selected unit" instead of "the player". Filtering after a few
  different situations weeds those out.
- Other cheat devices may handle multi-level `0x6` codes differently. Single-level ones are the
  most portable.

## Memory use

The results take 1 bit per byte of the scanned range (3 MB for 24 MB of RAM), plus a copy of
the range for the increased/decreased compares, plus 1 bit per byte for Undo.
