<!-- The design and the batch split for the virtual household, as agreed with the user. Kept for the units building
it; docs/HOME.md is the user-facing document. Two corrections after the foundation was written: the plan's crypto
lives in plan_seal.h (plan_logic.h stays crypto-free so house.wasm can include it), and the furniture enum's bin is
BINS (Arduino defines BIN). -->

# The virtual household (RoomiePet): your real home, mapped, getting messy, with the Pets living in it

## Context

Roommates' Pets already share a household (chores, money, messages), and the home server keeps X4s in step at home
and knows who's home. The household becomes a **place**: a pixel map of the roommates' **actual home** (their rooms,
laid out as they really are, furnished as they like), where mess builds up as chores come due and from everyday life,
doing chores cleans it (with an animation), Pets visit and do things together, furniture comes as gifts, and a tidy
house cheers everyone up. Decisions taken with the user:

| | Decision |
|---|---|
| Rooms | **Your real home, customised**: draw the floor plan room by room (floors, sizes, doors), name and type each room, furnish it, put chores in rooms. Starts from a template so it works before anyone edits |
| Where | A **Home** screen on every X4 (one room at a time), a whole-house view and the **home editor** in the web app, the website demo |
| Mess | **Chores + everyday life**: a chore's room gets messier as it comes due and more once late; a little everyday mess builds up too, cleared by any chore there or a *quick tidy* |
| First version | Pets visit and move around; the cleaning animation; furniture as gifts; the tidy-house bonus |
| Away | Away from the home network an X4 shows **only its own Pet's room**; at home, the whole house |
| Server | A **web GUI** on the home server (the whole household live), and an X4 can be a **household display** (the whole house, everyone's Pets together) |
| Not now | Pets as AI agents: a ROADMAP entry only |

This realises most of ROADMAP Phases 4–6 (House Health, monsters → clutter, the celebration, Harmony, visiting).

## Principles

- **One simulation, everywhere.** Rooms, mess, where each piece of clutter lies and what each Pet is doing are worked
  out from the home plan + the household's records + the time + who's home, by one deterministic C++ module. The X4
  runs it natively; the web app, the website demo and the server GUI run the **same code in `house.wasm`** (as the
  household rules already do: `tools/wasm/house_wasm.cpp`). Every screen shows the same mess in the same places.
- **Scripted, not AI.** Pets follow a daily schedule with seeded randomness.
- **Art from data** (the `tools/items/build.py` pattern): furniture, floors, walls, clutter as Kenney 1-Bit tiles or
  16×16 PNGs listed in text files, built into a C++ header and a JS module.
- **Editing on a big screen, living on the X4.** The plan is drawn in the web app (phone or computer); the X4 shows it
  and acts on it (quick tidy, chore done) but isn't a drawing tool.

## The home plan (`src/apps/house/plan_logic.h`, no Arduino; host-tested, and in `house.wasm`)

A list of **rooms**, each one entry, synced like the shared WiFi networks (`secure_logic.h`'s NetList pattern: each
room's newest version wins, removal included), so a plan of any size never touches the 120-record budget.

- **A room:** id, name (≤ 24), type (kitchen, bathroom, living, dining, bedroom, hall, laundry, office, outside,
  other), owner (a bedroom's roommate, else 0), floor (0–3), position and size on a tile grid (up to 32 × 32 a floor;
  one tile ≈ half a metre), up to 4 doors (wall side + offset), up to 20 furniture pieces (piece id, x, y,
  rotation), and up to 8 chores in it (their record keys). About 190 bytes; sealed it fits one radio packet.
- **Sealed for the household and its server:** entries are sealed (AES-256-GCM) with a key made from the household's
  server token (secure_logic.h's `derive`), so roommates' X4s and the household's home server can read and render the
  plan, and nobody listening on the radio learns the layout of someone's home. Checked fields: room id, version,
  editor, removed. Only household key holders (and the server) can add, change or remove rooms.
- **Who may change what:** shared rooms, any roommate; a **bedroom only its owner** (an entry for a bedroom whose
  editor isn't its owner is refused on merge, like `ownerOnly` records).
- **Templates:** studio, 2-bed flat, 3-bed house, 4–5-person house share; with no plan, every X4 shows a template
  sized to the household (a bedroom per member) until someone edits, so it works from day one.
- **Chores in rooms:** listed in the room entry; a chore not in any room goes to a room by keywords on its name (dish →
  kitchen, bins → outside…) and the first room of that type.
- **Sync:** at Sync one room a beat (as networks), through the home server (which can open them, for its GUI), and
  on the X4 in `/house/plan.bin` on the card.
- **Tidying:** a `TIDY` record per room (a = room id, b = day tidied), updated by whoever tidies (newest wins): kept
  out of the room entry so a tidy never races a furniture edit.

## The simulation (`src/apps/house/world_logic.h`, host-tested)

- **Mess per room (0–100):** from its chores (`turnOf`, every-n-days `a`, last done `d`): nothing until half the
  interval has passed, ~40 at the due day, +15 a day late (capped); plus everyday mess: +5 a day since the room was last
  tidied (a chore done there or its TIDY day), capped at 30. Bigger rooms show more pieces for the same level.
- **House Health** = 100 − size-weighted mean mess (the tidy-house bonus and the Pets' mood).
- **Clutter:** seeded (household id, room id, item number) placement on free floor tiles (not under furniture, not in
  doorways); item i always lands in the same place, so cleaning visibly removes things. Kinds follow the room's chores
  (plates, overflowing bin bag, towels, dust bunnies, clothes pile) and everyday mess (cup, sock, paper).
- **Pets' day:** 10-minute slots; a seed (household id, day, slot) picks each home Pet's activity and spot. Spots come
  from the furniture (a bed, sofa and chair seats, table places, a desk): asleep in its own bedroom's bed at night,
  meals at a table together, the sofa in the evening, visiting a roommate's bedroom, pottering, and cleaning with the
  chore's tool for the slot after it did a chore. With 2+ Pets home, group activities are favoured.
- **Who's home:** checked in with the home server in the last 15 minutes, or heard at Sync; this X4's Pet when it's on
  the home network. **Away mode:** the scene is just this Pet's bedroom (a prompt to set one up if there isn't one).

## Art (`catalog/home/` + `tools/home/build.py`)

`furniture.txt` (piece id, name, footprint in tiles, which spots it gives: bed, seat, table, desk; Kenney tile or PNG;
`gift` + rarity for decor that comes as a gift), `floors.txt` (a floor pattern and wall style per room type),
`clutter.txt`. Builds `src/apps/house/home_art.h` and `site/x4/home-art.js` (`--check` in CI). New 16×16 sprites in
Kenney's style for what the pack lacks (dust bunny, crumbs, plate stack, towel, sock, clothes pile, sofa pieces if
needed).

## The home editor (web app, `site/x4/app.js` + `site/x4/plan-editor.js`)

- **Floor plan:** a grid per floor; add a room (type, name, owner for a bedroom), drag to place, drag corners to size,
  click a wall to put a door; floors as tabs; start from a template or from scratch.
- **Furnishing:** open a room → a palette of furniture (everyone's) and your **gift** pieces (only the ones your Pet
  has found), click to place, rotate, remove; the room drawn as the X4 draws it.
- **Chores:** drag a chore into a room (or pick its room in the chore editor).
- Commands (the X4 seals; the web app sends plain room data, encoded by `house.wasm`):
  `PLAN LIST` → `PR <room hex>` per room, `PLAN SET <room hex>` (new or changed: the X4 sets version and editor, checks
  bedroom ownership, seals), `PLAN DEL <id>`, `TIDY <room id>`.

## X4 screens

- **House > Home** (a new screen; the 200×200 canvas doubled): one room at a time, scaled to fit (16 px tiles, 8 px
  for big rooms). A = next room, B = quick tidy or, on a due chore here, done; hold A = back. Header: room name and
  mess; footer: House Health. Away: only your bedroom.
- **Cleaning animation:** marking a chore done plays 3–4 fast frames (`displayFrame`): your Pet with the tool, the
  clutter going, a sparkle; then `displayClean`.
- **Pet screen:** House Health drives mood (≥ 90 happy, < 40 weary) via `houseMood()`.
- **Household display** (full panel): a native drawing path in `src/core/display.cpp` (a GFX canvas over the FreeInk
  800×480 framebuffer, upright 480 × 800, `displayShowNative`). The floor plan of every floor, rooms with their
  furniture and mess, all home Pets at their activity, names, House Health, the time; partial refresh each minute, full
  every 30. Any roommate's X4 while charging (House > Home, hold B), or a **spare X4 joining as the household display**
  at Sync (the key handover's ASK carries a DISPLAY flag; no member record, no rota, no Pet).

## Gifts and the bonus (`src/apps/pet/pet_logic.h`, `pet.cpp`)

- **Furniture as gifts:** gift pieces from `furniture.txt` join `DROPS` (a DECOR layer); owned bits in an 8th word of
  `pet_owned` (the old 7-word block still read). Found pieces unlock in the editor's palette for that roommate. `P
  PARTS` lists them (layer 7).
- **Tidy-house bonus:** `ChoreLuck` gets House Health: ≥ 90 adds to the gift chance, < 40 makes Pets weary.

## Home server web GUI (`server/roomiepet_server.py`)

`GET /`: the whole household live (refresh every 30 s) drawn from the plan + records by `house.wasm` + `home.js`, who's
home and since when, House Health, recent activity, and the server's state (releases, relays, households). It opens
the plan with the token it already has. Password login (`--set-password`, PBKDF2 hash in `households.json`, session
cookie); `/v1/gui/house/<id>` gives the page the records, the opened plan and presence. Read-only in v1 (the editor
can move here later: the server can seal plan edits).

## Website demo and ROADMAP

- `site/roomiepet.*`: a sample home (template + furniture + Pets + some mess) with "do a chore" cleaning a room.
- `docs/ROADMAP.md`: Phases 4–6 point here; add **"Pets with a mind of their own (future)"**: AI-agent Pets.

## Build order (each step builds, tests and is pushed)

1. `plan_logic.h` (rooms, encode, seal/open with the plan key, merge with bedroom ownership, templates) +
   `world_logic.h` (mess, health, clutter, schedule, away) + TIDY records + `tools/worldtest`.
2. Art catalog + `tools/home/build.py` + new sprites.
3. X4: plan storage, Sync gossip, `PLAN`/`TIDY` commands, home server carries the plan; the Home screen, cleaning
   animation, mood/bonus, furniture gifts.
4. `house.wasm` plan + scene exports, `site/x4/home.js`, the web app's Home tab and **editor**.
5. Native display + household display mode (any X4, spare X4 as display).
6. Server GUI + tests; website demo.
7. Docs (`docs/HOME.md` new; HOUSEHOLD, WEBAPP, HOMESERVER, README, ROADMAP), builds, push.

## Critical files

New: `src/apps/house/plan_logic.h`, `world_logic.h`, `world_draw.h`, `plan.cpp`/`plan.h` (X4 storage, sync, USB),
`home_art.h` (generated), `catalog/home/*`, `tools/home/build.py`, `tools/worldtest/world_test.cpp`,
`site/x4/home.js`, `plan-editor.js`, `home-art.js` (generated), `server/gui/index.html`, `docs/HOME.md`.
Changed: `house_logic.h` (TIDY), `house.cpp` (Home screen, display, Sync), `store.cpp` (mood), `secrets.cpp`
(DISPLAY ask, plan key), `home.cpp` + `home_logic.h` (plan in the check-in), `display.{h,cpp}`, `pet_logic.h`/`pet.cpp`,
`tools/wasm/house_wasm.cpp`, `site/x4/app.js`, `tools/wasm/mock_x4.mjs`, `server/roomiepet_server.py`,
`site/roomiepet.*`, `.github/workflows/ci.yml`.
Reused: `NetList`/`seal`/`open`/`derive` (secure_logic.h), `serverToken` (home_logic.h), `turnOf`/`members`/`wearOf`
(house_logic.h), `houseWornOf`/`houseHeardChange` (store.cpp), `items::drawPet` + the chore tool sprites,
`petDraw`, `drawPetWorn` (held.js), `displayFrame`/`displayClean`, `homeAtHome`, the wasm build/test, the item build
script pattern.

## Verification

- `tools/worldtest`: plan encode/seal/open and tamper checks; merge (newest wins, bedrooms owner-only, removals);
  templates; determinism (same inputs → same scene); mess curve; tidy/chore clear the right room; clutter stable and
  never on furniture or doorways; schedule (night in own bed, group meals, cleaning after a chore); away mode.
- `house_wasm_test.mjs`: the wasm scene equals the native one for a fixed plan + records.
- `tools/run_tests.sh`, `server/test_server.py` (plan relayed and opened; GUI login; data endpoint needs a session).
- Builds: `x4`, `x4-dev`, `epaper154`; `tools/home/build.py --check`.
- Playwright against the mock X4 (extended with a plan, chores, presence): draw a room, resize, add a door, furnish,
  move a chore, see the Home view; the server GUI; the website demo. Screenshots reviewed.
- On real X4s (not possible here): the dollhouse's refresh timing, the animation, a spare X4 joining as display.

---

# Batch execution: everything at once, then merged

## What research found (shapes the split)

- Every layer needs the same few pieces: the room plan (`plan_logic.h`), the simulation (`world_logic.h`), the art
  (`home_art.h` / `home-art.js`), the X4's plan store (`plan.h`), and the browser's scene (`house.wasm` + `home.js`).
  Units can't build on each other's unmerged work, so **I (the coordinator) first commit a foundation**: all those
  interfaces, complete in their types and formats, each with a simple working implementation. Every unit then works
  against the foundation only, in parallel, and owns its own files.
- Session rules: work stays on `ccr-06f4bc74-aynn6s`; no other branches are pushed and no PRs are opened (and `gh` isn't
  available). So each worker **commits on its own worktree branch and doesn't push**; I merge every branch into
  `ccr-06f4bc74-aynn6s`, resolve the few shared touch points (a hook line in `usbsync.cpp`, `ci.yml`, generated
  `house.wasm`), rebuild the generated files, run everything, and push once.
- Builds share `~/.platformio`, which holds the X4's framework now. Workers build **only `x4`** (never delete packages or
  build `epaper154`: switching frameworks deletes packages under everyone). I build `epaper154` after the merge.
- Worktrees have an empty `freeink-sdk/` submodule: workers run `git submodule update --init` first.
- Test tools on hand: `tools/run_tests.sh` (host C++ tests, libmbedtls-dev installed), `tools/wasm/build.sh` +
  `node tools/wasm/house_wasm_test.mjs`, `tools/wasm/mock_x4.mjs` (a pretend X4 serving the web app), Playwright at
  `/opt/node22/lib/node_modules/playwright` (Chromium installed), `python3 -m unittest server/test_server.py`.

## Step 0: the foundation (me, before the workers; one commit)

- `house_logic.h`: `TIDY = 11` (a = room id, b = day; anyone may update; newest wins), `LAST_KIND`, wasm/server filters.
- `src/apps/house/plan_logic.h` (complete): `RoomType` {KITCHEN, BATHROOM, LIVING, DINING, BEDROOM, HALL, LAUNDRY,
  OFFICE, OUTSIDE, OTHER}; `Room` {id, name[24], type, floor, x, y, w, h (tiles on a 32×32 grid a floor), owner, doors[4]
  {side, at}, pieces[20] {id, x, y, rot}, chores[8] {origin, seq}}; `encodeRoom`/`decodeRoom` (fixed `ROOM_LEN` = 174
  bytes, id included); `Entry` (id, ver, editor, flags, nonce, sealed room, tag; `ENTRY_LEN` 215) with `planKey(token)`,
  `sealRoom`/`openRoom` (secure_logic.h's seal/open/derive); `PlanList` merge (newest wins; bedrooms only by their owner,
  including taking one over); `roomTypeForChore(text)` keywords; `makeTemplate(memberIds, n, out)`; radio packet
  `RPK` type 4 (house + entry, 224 bytes).
- `src/apps/house/world_logic.h`: `Clutter` {kind, x, y}, `RoomState` {mess, count, clutter[24]}, `PetState` {pet,
  room index, x, y, activity (SLEEP, EAT, SOFA, VISIT, POTTER, CLEAN), tool}, `Scene` {rooms, states[16], pets[8],
  health, away, awayRoom}; `build(Scene&, const Input&)` with `Input` {rooms, count, house, today, minute, home pets,
  me, away, recentDone}; `roomOfChore(...)`. Simple bodies (mess from overdue days only, clutter in a row, Pets on
  the first free floor tile): **Unit 1 replaces the bodies, keeping the signatures**.
- `src/apps/house/home_art.h` + `site/x4/home-art.js`: the format (16×16 one-bit sprites; furniture {id, name, w, h
  tiles, spots bitmask BED/SEAT/TABLE/DESK, gift, rarity, sprite per tile}; clutter {kind, sprite}; a floor pattern
  per room type; wall sprite) with a handful of placeholder pieces. **Unit 2 regenerates both from a real catalog.**
- `src/apps/house/world_draw.h`: `drawRoom(room, state, tile 8|16, pixel)` (walls, doors, floor, furniture, clutter),
  basic. **Unit 3 completes it.**
- `src/apps/house/plan.h` + `plan.cpp`: the X4's API, `planRooms(out, max)` (the template sized to the household for
  now), `planRoom(id)`, `planTidy(id)`, `planSyncStart/planHeard/planTick` (no-ops), `planUsb` (`PLAN LIST`, `PLAN SET`,
  `PLAN DEL`, `TIDY` answered from memory), `planCount/planEncodedEntry/planMergeEntry` (for the home server).
  **Unit 4 makes it real** (card, Sync, sealing, server).
- `tools/wasm/house_wasm.cpp`: `plan_clear`, `plan_put` (a room's plain bytes as hex), `home_set` (pets home),
  `scene(today, minute, me, away)` → JSON {health, away, rooms: [{id, name, type, floor, x, y, w, h, owner, doors,
  pieces, mess, clutter: [{k, x, y}]}], pets: [{pet, room, x, y, activity, tool}]}, `plan_template(n)`; rebuilt
  `house.wasm`.
- `site/x4/home.js`: `drawRoom(ctx, room, scale)`, `drawHouse(ctx, scene, {floor, width})`, `drawScenePets(ctx, scene,
  members, findItem)`: basic. **Unit 7 completes it.**
- A new **Home** app shell `src/apps/home/home_app.cpp` registered in `apps.cpp` (X4 only): shows the first room.
  **Unit 5 builds it out**; Unit 6 hooks the household display into it.
- Payload contract (home_logic.h comment): the check-in request and answer each gain, at the end, `plan u8 + entries`.

## Work units (each owns its files; shared touch points are noted)

| # | Unit | Files | Change |
|---|---|---|---|
| 1 | Simulation | `src/apps/house/world_logic.h`, `tools/worldtest/` | Real mess (chores coming due, late, everyday mess, TIDY), House Health, chore→room (plan list, keywords, first room of the type), seeded clutter (stable, off furniture and doorways, kinds by chore), Pets' 10-minute schedule from furniture spots (sleep, meals, sofa, visits, potter, cleaning after a chore), away mode; thorough host tests |
| 2 | Art catalog | `catalog/home/`, `tools/home/build.py`, `src/apps/house/home_art.h`, `site/x4/home-art.js`, `.github/workflows/ci.yml` (one check line) | furniture.txt / floors.txt / clutter.txt from Kenney tiles + new 16×16 sprites (dust bunny, crumbs, plates, towel, sock, clothes pile, sofa/bed pieces as needed), gift pieces with rarity; `--check`; a contact sheet PNG in `docs/` |
| 3 | X4 renderer | `src/apps/house/world_draw.h`, `tools/worlddraw/` | Rooms at 8/16 px tiles: walls, doors, floor patterns, furniture with rotation, clutter; Pet poses for activities (asleep Zzz, holding the chore tool); whole-plan layout for big canvases (`drawPlan(floor, w, h)`); host render test writing PNGs |
| 4 | X4 plan store + sync | `src/apps/house/plan.cpp`, `plan.h`, `home.cpp`, `home_logic.h` (payload), `secrets.cpp` (plan key accessor) | Card storage (`/house/plan.bin`), seal with the plan key, Sync gossip (one room a beat) + merge rules, `PLAN`/`TIDY` commands, rooms through the home server check-in |
| 5 | X4 Home app | `src/apps/home/home_app.cpp` (+ `home_app.h`), `src/apps/house/store.cpp` (`houseMood`) | Room-by-room view (A next, B quick tidy / chore done, hold A back), away mode (own room only), cleaning animation (`displayFrame` ×3–4 then `displayClean`), House Health on Pet and Home screens, mood |
| 6 | Household display | `src/core/display.{h,cpp}` (native path), `src/apps/home/dollhouse.cpp`, `src/apps/house/secrets.cpp` (DISPLAY ask), `src/apps/house/house.cpp` (Sync "join as display"), `store.cpp` (no member record for a display) | `displayShowNative` on the 480×800 framebuffer; the dollhouse (all rooms of a floor or floors, Pets, names, health, time; partial each minute, full every 30) while charging; a spare X4 joining as the household display |
| 7 | Browser renderer + website demo | `site/x4/home.js`, `site/roomiepet.html`, `site/roomiepet.js`, `site/index.html` | Full canvas renderer matching Unit 3's look; the website's sample home with "do a chore" |
| 8 | Web app Home tab + editor | `site/x4/app.js`, `site/x4/plan-editor.js`, `site/x4/index.html`, `tools/wasm/mock_x4.mjs` | Home tab (whole house, rooms, health, who's home); editor: floors, add/drag/resize rooms, doors, type/name/owner, furniture palette (gift pieces only if owned), drag chores into rooms, templates; mock X4 speaks `PLAN`/`TIDY` |
| 9 | Gifts + bonus | `src/apps/pet/pet_logic.h`, `src/apps/pet/pet.cpp`, `src/apps/pet/usb.cpp`, `tools/pettest/` | Decor gift drops (an 8th owned word, old 7-word block still read), `P PARTS` layer 7, tidy-house bonus in `ChoreLuck`, weary Pets under 40 |
| 10 | Server: plan + GUI | `server/roomiepet_server.py`, `server/gui/`, `server/test_server.py` | Plan entries in check-ins (open with the token-derived plan key, bedroom rule, newest wins), password login (`--set-password`, PBKDF2), `/v1/gui/house/<id>`, the GUI page (whole household via `house.wasm` + `home.js`), tests |
| 11 | Docs + roadmap | `docs/HOME.md` (new), `docs/HOUSEHOLD.md`, `docs/WEBAPP.md`, `docs/HOMESERVER.md`, `README.md`, `docs/ROADMAP.md` | The virtual household explained; command tables; ROADMAP Phases 4–6 rewritten + "Pets with a mind of their own (future)" |

Merge (me, after all report): merge branches in order 1→11; resolve touch points; regenerate `house.wasm`
(`tools/wasm/build.sh`), `home_art.h`/`home-art.js` (`tools/home/build.py`), README images if any; then the full check
(below), `epaper154` build, push.

## End-to-end recipe (per kind of unit)

- **Setup in the worktree:** `git submodule update --init` (for the X4 build). Env for PlatformIO:
  `export SSL_CERT_FILE=/root/.ccr/ca-bundle.crt REQUESTS_CA_BUNDLE=/root/.ccr/ca-bundle.crt`.
- **C++ logic (1, 3, 9 and any header):** `tools/run_tests.sh` all `ok`; the unit's own test prints and exits 0. Unit 3
  also renders PNGs and looks at them (Read the image).
- **Firmware (3, 4, 5, 6, 9):** `pio run -e x4` succeeds. Never build `epaper154`, never delete `~/.platformio` packages.
- **Browser (7, 8, 10):** `tools/wasm/build.sh site/x4/house.wasm && node tools/wasm/house_wasm_test.mjs`; start the mock
  X4 on the unit's own port (`node tools/wasm/mock_x4.mjs 81NN`, NN = unit number; Unit 7 serves `site/` with
  `python3 -m http.server 82NN`); drive it with Playwright
  (`createRequire('/opt/node22/lib/node_modules/playwright/')('playwright')`, `chromium.launch()`), exercise the
  feature, take screenshots and look at them, and require no page errors. Stop the servers by PID afterwards.
- **Server (10):** `python3 -m unittest server/test_server.py`; run the server on port 84NN with a test household and
  open the GUI with Playwright (login, house drawn).
- **Docs (11):** skip e2e (prose); check every command and path named exists in the code or the plan.

## Worker instructions (the shared template)

Each worker gets: the overall goal ("the virtual household: map your real home, mess, Pets living in it; see the
design above"), the full design section above (Context through Verification) and the foundation description, its
unit's row verbatim, these conventions, the e2e recipe, and the block below.

Conventions: comments and docs in the project's plain British prose (short sentences, no jargon, as in the existing
files); match surrounding code style (C++17 headers with no Arduino code in `*_logic.h`, 2-space indent, 120 columns);
`UNIDEX_X4` guards for X4-only code and stubs for the Waveshare board; tests in `tools/<name>test/*_test.cpp` with their
build command on line 2 (`//   c++ ...`) so `tools/run_tests.sh` picks them up; never include model names in commits;
commit messages end with the two attribution lines (`Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>` and
`Claude-Session: https://claude.ai/code/session_01DLWRSD23CV8fU6Wa3yoozp`); touch only your unit's files (and the
noted touch points), never reformat others' code.

```
After you finish implementing the change:
1. **Code review** — Invoke the `Skill` tool with `skill: "code-review"` to find correctness bugs (it reports findings; it does not edit code). Fix any findings it surfaces before continuing.
2. **Run unit tests** — Run the project's test suite (check for package.json scripts, Makefile targets, or common commands like `npm test`, `bun test`, `pytest`, `go test`). If tests fail, fix them.
3. **Test end-to-end** — Follow the e2e test recipe from the coordinator's prompt (below). If the recipe says to skip e2e for this unit, skip it.
4. **Commit and push** — Commit all changes with a clear message, push the branch, and create a PR with `gh pr create`. Use a descriptive title. If `gh` is not available or the push fails, note it in your final message.
5. **Report** — End with a single line: `PR: <url>` so the coordinator can track it. If no PR was created, end with `PR: none — <reason>`.
```

Added for this session (it overrides step 4): commit on your worktree's branch but **do not push and do not open a
PR** (the session may only push `ccr-06f4bc74-aynn6s`, and the coordinator merges every unit); end with
`PR: none — committed on branch <your branch name> at <short sha>`.
