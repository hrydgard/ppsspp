# ZArchive (.zar) games

PPSSPP can run a game straight from a [ZArchive](https://github.com/Exzap/ZArchive) (`.zar`) file.
One archive can hold a game's disc image or images, together with the memory stick content that goes
with it (DLC, game updates), all compressed and with nothing to extract.

This page describes how files must be named and where they must go inside the archive.

## Quick reference

All paths are relative to the root of the archive. Names are matched case-insensitively, so `psp/game`
works as well as `PSP/GAME`.

| Path inside the .zar | What PPSSPP does with it |
| --- | --- |
| `/<anything>.iso`<br>`/<anything>.cso`<br>`/<anything>.chd` | A disc image. Only the **root** of the archive is searched. |
| `/<anything>.pbp` | Also a disc image, if the PBP holds a game disc (PSN games, ISOs converted to PBP), the same as a loose `Valkyrie Chronicles.pbp`. A homebrew PBP here is ignored, see below. |
| `/PSP/...` | Memory stick content. It shows up as `ms0:/PSP/...` while the game runs. |
| `/PSP/GAME/<DISC_ID>/...` | DLC and other per-game data, for example `.EDAT` files. |
| `/PSP/GAME/<DISC_ID>/PBOOT.PBP` | A game update, booted instead of the disc's executable. |
| `/PSP/GAME/<folder>/EBOOT.PBP` | A memory stick game (PSN game, homebrew). Booted **only if the archive has no disc image**. |
| `/PSP/SAVEDATA/...` | **Ignored.** Save data always lives on the real memory stick. |
| Anything else | Ignored. |

## Layouts

### Single-disc game with DLC and an update

```
Game.zar
├── Some Game (USA).iso
└── PSP/
    └── GAME/
        └── ULUS12345/
            ├── PBOOT.PBP
            ├── DLC01.EDAT
            └── DLC02.EDAT
```

Full paths in the archive:

```
Some Game (USA).iso
PSP/GAME/ULUS12345/PBOOT.PBP
PSP/GAME/ULUS12345/DLC01.EDAT
PSP/GAME/ULUS12345/DLC02.EDAT
```

`ULUS12345` stands for the game's disc ID, which you can find in the game's info screen in PPSSPP. A
game looks for its DLC wherever it was installed on a real PSP, which is usually `PSP/GAME/<its disc
ID>/`. Some games use a different folder, so copy whatever the DLC's installer created under `PSP/`.
Every folder under `PSP/` (except `SAVEDATA`) is visible to the game.

An update (`PBOOT.PBP`) is only booted if the `DISC_ID` in its PARAM.SFO matches the disc's.

### Multi-disc game

```
Game.zar
├── Some Game (USA) (Disc 1).iso
├── Some Game (USA) (Disc 2).iso
└── PSP/
    └── GAME/
        └── ULUS12345/
            └── DLC01.EDAT
```

Full paths in the archive:

```
Some Game (USA) (Disc 1).iso
Some Game (USA) (Disc 2).iso
PSP/GAME/ULUS12345/DLC01.EDAT
```

- **Disc 1 is the first disc image in name order, so the names must sort in disc order.** The sort is
  "natural" (numbers compare by value) and case-insensitive. `Disc 2` sorts before `Disc 10`, and
  `Disc 1`, `Disc 2`, `Disc 3` sorts as expected. Naming discs `A.iso` and `B.iso` works too, as long
  as the order is right.
- The first time, disc 1 boots. After that, **PPSSPP boots the disc you used last**, so a game you've
  reached disc 2 in starts from disc 2. This is remembered per archive (in `ppsspp.ini`, section
  `[ZArchiveDiscs]`), and a disc that's no longer in the archive falls back to disc 1.
- To change discs while playing, use **Switch UMD** in the pause menu. It lists the archive's discs
  on its **Discs** tab, which only appears for archives with two or more discs.
- To pick the disc before starting, use **Change disc** on the game's info screen. The game list, the
  info screen and so on then show that disc: its title, icon and disc ID, which can differ between
  discs.
- All discs share the one `PSP/` folder.
- On the game's info screen, the save data size counts the save data of every disc's ID, since a game
  can save under the ID of any of its discs (usually the first). **Delete Save Data** there removes
  all of it.
- Disc images can be ISO, CSO, CHD or PBP, and can be mixed.

### PSN game as a single PBP

```
Game.zar
├── Some PSN Game.pbp
└── PSP/
    └── GAME/
        └── NPUH10000/
            └── DLC01.EDAT
```

Full paths in the archive:

```
Some PSN Game.pbp
PSP/GAME/NPUH10000/DLC01.EDAT
```

The PBP can have any name. It's treated exactly like an ISO in the root, so this also works for
multi-disc games, and its DLC goes in `PSP/GAME/<its disc ID>/` as usual. This only applies to PBPs
that contain a game disc (`NPUMDIMG`). PPSSPP checks the file's contents, not its name.

### PSN game or homebrew in memory stick layout (no disc image)

```
Game.zar
└── PSP/
    └── GAME/
        └── NPUH10000/
            ├── EBOOT.PBP
            └── DLC01.EDAT
```

Full paths in the archive:

```
PSP/GAME/NPUH10000/EBOOT.PBP
PSP/GAME/NPUH10000/DLC01.EDAT
```

- The `EBOOT.PBP` has to be exactly one folder below `PSP/GAME/`. It boots as
  `ms0:/PSP/GAME/NPUH10000/EBOOT.PBP`, the same as when it's installed on the memory stick, so any
  files it loads from its own folder are found.
- Put **one game per archive**. If there are several `PSP/GAME/*/EBOOT.PBP`, the first folder in name
  order boots and the rest are only visible as memory stick content.
- If the archive also has a disc image in its root, the disc boots and the `EBOOT.PBP` is ignored.

## Creating the archive

Use the `zarchive` tool from https://github.com/Exzap/ZArchive, and give it the folder **whose
contents** become the root of the archive:

```
zarchive "Some Game" "Some Game.zar"
```

Here `Some Game/` holds the `.iso` and `PSP/` directly. The folder itself is not stored, only what's
inside it.

> [!WARNING]
> A common mistake is an extra folder level. If the archive root contains `Some Game/Some Game.iso`
> and `Some Game/PSP/...`, PPSSPP finds neither, because disc images and `PSP/` are only looked for in
> the root. Pack the folder that contains the files, not its parent.

The file must end in `.zar` to show up in the game list. PPSSPP also recognizes the format by its
contents, so a `.zar` renamed to some other extension can still be opened with **Load...**, unless the
new extension is `.iso`, `.cso` or `.chd`.

## How the memory stick content behaves

- **The real memory stick wins.** If a file exists both on the memory stick and in the archive, the
  game sees the memory stick's copy.
- **The archive is never modified.** Writes go to the real memory stick. If the game changes a file
  that only exists in the archive, the file is copied to the memory stick first, and from then on that
  copy is used. Folders that only exist in the archive are created on the memory stick when the game
  writes into them.
- The game can't delete files that are in the archive. Renaming one creates the renamed copy on the
  memory stick, but the file under its old name stays visible, because it's still in the archive.
- **Save data is never read from the archive**, so `PSP/SAVEDATA/` in an archive does nothing. Copy
  save data to the memory stick the normal way.
- Savestates work as usual. A savestate only differs from one made without the archive if the game had
  a file from the archive open at that moment.

## Ignored

- Disc images anywhere except the root, such as `/Discs/Disc 1.iso`.
- A homebrew PBP (one that holds a program rather than a disc image) in the root. Homebrew has to
  go in `PSP/GAME/<folder>/EBOOT.PBP`, because it loads its files from that folder.
- Names starting with `._`, which are macOS metadata files.
- Extracted disc folders (`PSP_GAME/...`). Use a disc image instead.
- An archive inside an archive, or a `.zar` inside a `.zip`.
