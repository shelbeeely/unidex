# Things to hold and wear (RoomiePet)

Beyond the wardrobe built into the firmware, a Pet can **hold** something in its hand (a broom, a mop or a spray for
the chores, food, treasure, a sword, a wand, a game pad...) and **wear** clothes from the catalog: crowns, hats and
ears on its head, necklaces at its neck, and hair and outfits drawn to fit it. A catalog piece of clothing is worn
instead of the wardrobe's part for its layer (a catalog hat instead of the wardrobe's hat, and so on), and shows
wherever the Pet does: its screen, dress-up, Meet, friends lists, the household's Roommates, the web app.

![Everything in the catalog, held or worn by a Pet](roomiepet-items.png)

Most of the pictures are from **Kenney's [1-Bit Pack](https://kenney.nl/assets/1-bit-pack)** (CC0: free for any use;
credit is kind, not required): 1,078 pictures drawn for one-colour screens, which suits the X4's e-ink. The chore
tools Kenney doesn't have (broom, mop, spray, sponge, plunger, duster, laundry basket, bin bag) are drawn in the
same style, in `catalog/sprites/`, as are the beret and the heart jumper.

## Two shapes

- **An icon:** a 16 × 16 one-colour picture, like Kenney's. It goes where its kind says, with a white edge so it reads
  against the Pet: **in the hand** (the Pet's lower right), **on the head** (centred, its lowest ink on the top of the
  head) or **at the neck** (centred, its highest ink just under the chin).
- **A piece:** drawn on the Pet's own 32 × 32 grid, like the wardrobe's parts: each pixel ink, paper (white, covering
  what's under it) or clear (the Pet shows through). A **hat**, an **accessory**, **hair** or an **outfit**, at most 27
  rows tall (so it fits one radio message).

## Adding things, the easy ways

**From the web app** (the X4's **Items** tab, on a phone over its WiFi or on a computer over USB), no new firmware:

- **From Kenney's pack:** the whole sheet is there; tap a picture, say where it goes (in the hand, on the head, at the
  neck), name it, *Add to the X4*.
- **Your own picture:** pick any image; it's cropped to what's drawn, shrunk to 16 × 16 and made black and white
  (transparent or white is paper). Simple shapes with thick lines work best.
- **Draw clothes:** the piece editor shows your Pet faintly under a 32 × 32 grid; tap or drag to draw in ink, paper or
  clear. Start from what your Pet wears (to change a wardrobe part into something new), from scratch, or from a
  picture drawn at 32 × 32 (dark is ink, light is paper, transparent is clear). Pick the kind, name it, add it.

Then dress your Pet in it: the web app's **Pet** tab, or dress-up on the X4 (the Hat, Accessory, Hair and Outfit rows
go through the wardrobe's parts your Pet has, then the catalog's of that kind; the Holding row through the things to
hold).

The X4 keeps what it's given on its SD card (`/pet/items.bin`, up to 96) and **passes it on over the radio**: whenever
it syncs with roommates (House, Sync) or meets other Pets (Pet, hold B), each device says a digest of its catalog in
its hello; while two differ, each sends its items one radio message at a time (up to 242 bytes each, the ones it was
given first), and each keeps the ones it didn't have. So one roommate adds a thing, and the whole house has it after
the next Sync. A Pet wearing or holding something your X4 doesn't have yet shows it as soon as it arrives (until
then, the wardrobe's part).

**In the firmware** (for everyone, from the next release): one line in `catalog/items.txt`, then one command.

```text
Teapot, kenney:770              # a tile of Kenney's pack: its number (the web app's Items tab shows the sheet)
Mop bucket, mopbucket.png       # or a 16 x 16 PNG in catalog/sprites/: an icon
Wizard hat, kenney:131, hat     # where it goes: hand (the default), hat or accessory
Scarf, scarf.png, accessory     # a 32 x 32 PNG drawn on the Pet's grid is a piece: hat, accessory, hair or outfit
```

A 32 × 32 PNG is a piece: black is ink, white is paper, transparent is clear (lay it over a Pet from
`docs/roomiepet-wardrobe.png` to see where things go).

```sh
python3 tools/items/build.py   # writes src/apps/pet/items.h, site/x4/items.js and site/x4/kenney.png
```

CI fails if those are out of date (`--check`).

## How it works

- `src/apps/pet/items_logic.h`: an item (an id, a name of up to 12 characters, its kind, its shape and the picture),
  the catalog (the firmware's, then the ones given), the radio message, and drawing a Pet with what it wears and holds.
  Checked on a computer by `tools/itemtest`.
- An item's **id** is the CRC-32 of its name, kind, shape and picture, kept to 24 bits: the same item added on two X4s
  gets the same id, and a damaged radio message can't add a broken item (the id must match). What a Pet holds and
  wears is saved (`pet_worn`) and sent as five ids (held, hat, accessory, hair, outfit): in Meet's hello, in friends
  lists, and to the household (what it holds on the member card, its clothes on its own `WEAR` record, which only its
  owner's device can change).
- `src/apps/pet/held.cpp`: the X4's catalog on the SD card, the sharing, and the USB commands the web app uses:

| Command | What |
|---|---|
| `I LIST` | Every item: `IT <id> <name hex> <kind> <shape> <top> <rows> <picture hex> <0 firmware / 1 given>`, then `OK I LIST <count> <digest>` |
| `I ADD <name hex> <kind> <shape> <top> <rows> <picture hex>` | Adds one (the X4 works out its id): `OK I ADD <id>` |
| `I DEL <id>` | Removes one it was given (X4s it was passed to keep theirs) |
| `P SET ... <held> <hat> <accessory> <hair> <outfit>` | What the Pet holds and wears (`P GET` says it) |

Kinds: 0 hand, 1 hat, 2 accessory, 3 hair, 4 outfit. Shapes: 0 icon (top 0, rows 16, 32 bytes: two a row, the leftmost
pixel in the top bit; hand, hat or accessory), 1 piece (rows `top` to `top + rows - 1` of the Pet's grid, 8 bytes a
row, two bits a pixel from the left: 0 clear, 1 ink, 2 paper).

The Waveshare board (unidex) has no catalog: it holds and wears only the wardrobe.
