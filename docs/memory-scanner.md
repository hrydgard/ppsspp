# Memory Scanner

A small "Cheat Engine"-style value search for PSP user memory, made to be usable with touch.

## Enabling

1. Enable cheats (Game settings > System > Enable Cheats).
2. Open the Cheats menu from the pause menu and tick **Memory Scanner**.
3. Back in the game, a **Memory Scanner** button appears at the top center of the screen.

The button and the scanner only work while cheats are enabled, and not in RetroAchievements
hardcore mode.

## What is scanned

User memory, from `0x08800000` to the end of emulated RAM. Normally that's 24 MB, but on a
PSP-2000+ model, for games, homebrew or HD remasters that use the extra RAM, it goes up to
`0x0C000000` (56 MB). The range is shown at the top of the scanner.

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
  **Keyboard** for anything else (like hex digits A-F). `0x` (or `$`) means hex, `-` works
  for any size (so `-1` finds `0xFF` in 8-bit).
- Floats use a rounded compare: `95` matches 94.5 to 95.5, `95.25` matches 95.245 to 95.255.
  A hex value for a float matches its exact bits.
- Integers are compared unsigned for Bigger/Smaller/Between.
- **Undo** goes back one scan.

Only the addresses from the previous results are checked by a next scan. Results stay in RAM
until a new search, or the game is closed, so you can close the scanner, play, and come back.

The game is always stopped while a scan runs. Tick **Pause emulation while the scanner is
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
cheat made from them may only work in some situations.

## Memory use

The results take 1 bit per byte of the scanned range (3 MB for 24 MB of RAM), plus a copy of
the range for the increased/decreased compares, plus 1 bit per byte for Undo.
