# The X4 web app (RoomiePet manager)

Manage a unidex Xteink X4 from a browser: the household (chores, shopping, money, reminders, Pet messages,
roommates), Cash App (your $cashtag, pay or share for each debt, paste a Cash App request), badges, the timetable
and settings. One app (`site/x4/`), two ways to reach the X4:

| | On a phone (or any browser) | On a computer |
|---|---|---|
| How | On the X4: House, **Manage on phone**, scan the QR code | The website's `/x4/` page, X4 plugged in with a USB-C data cable |
| Connection | The X4's own WiFi hotspot (no internet needed) | USB (Web Serial: Chrome or Edge) |
| Served by | The X4 itself (the app is built into its firmware) | The website |

Both send the X4 the same text commands it takes over USB (`src/core/usbsync.cpp`): on the hotspot the page calls
`/api?c=<commands>`, which runs them with `usbRun()` and returns what they printed.

## WebAssembly

The household is edited with `house.wasm`: `src/apps/house/house_logic.h`, the X4's own C++, compiled to
WebAssembly (`tools/wasm/house_wasm.cpp`, built by `tools/wasm/build.sh` with clang alone, no Emscripten). Each
change in the app:

1. reads the household from the X4 (`H DUMP`) into the module,
2. makes the change there as the X4 itself would (as its Pet, on its clock),
3. sends back only the records that changed (`H PUT ...`, then `H SAVE`), which the X4 merges exactly as it merges
   a roommate's at Sync.

So the app and the X4 can't disagree about whose turn a chore is or who owes whom, and the app's rules can't drift
from the device's. The Pet's art comes from `site/x4/pet-art.js`, generated from the firmware's parts by `tools/pet/export.py`, so the
preview matches the X4 exactly. Badge pictures are converted to 1-bit e-ink BMPs in the same module (contrast stretch and
Floyd-Steinberg dithering, as the website's badge maker). That's where WebAssembly earns its keep: the household is
small, so the win there is one shared set of rules rather than speed; image conversion is the heavy lifting.

## Sharing on iPhone and Android

Phones only open their share sheet (and the clipboard) from secure (https) pages. On the website (https) Share opens
the phone's or computer's own share sheet. On the X4's hotspot the page is plain http, so Share offers **Copy link**,
**Text it** (opens Messages with the link) and **Email** instead; pressing and holding a link also brings up the
phone's own share option. **Pay in Cash App** needs the internet: the phone switches back to its own WiFi or data
when you leave the X4's hotspot.

## Commands the app uses

| Command | What |
|---|---|
| `H DUMP`, `H PUT <hex>`, `H SAVE`, `H START`, `H LEAVE` | The household (`src/apps/house/usb.h`) |
| `G`, `G SET sleep <s>`, `G SET invert <0/1>` | Settings |
| `P GET`, `P SET <look> <name hex>`, `P PARTS` | The Pet's name and look (7 hex digits, one per layer); every part with the EXP it needs, the styles, and the Pet's EXP |
| `T <unix>` | Set the clock from the phone or computer |
| `L`, `B ...` + `D ...`, `BX <name>` | List, upload and delete badges |
| `W GET`, `W <count> <crc32>` + lines | Read and replace the timetable |
| `?` | Sent last in each batch over USB: its answer says the X4 has done the rest |

## Trying it without an X4

`node tools/wasm/mock_x4.mjs` serves the app with a pretend X4 behind `/api` (a household of two Pets, badges, a
timetable): open `http://localhost:8044/?device`. `node tools/wasm/house_wasm_test.mjs` checks the WebAssembly
module; CI runs it on the committed file and on a fresh build.

## Security

The hotspot has a new password each time, shown only on the X4's screen, and USB needs the device in hand: anyone
who can reach the app can manage that X4. Records only their owner may change ($cashtags, Cash App requests) are
made as the X4's own Pet, so a web app can't set a roommate's.
