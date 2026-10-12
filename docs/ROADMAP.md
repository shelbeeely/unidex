# RoomiePet roadmap (Xteink X4)

RoomiePet turns household upkeep into a cooperative multiplayer Tamagotchi. Every roommate has an Xteink X4 on the
fridge, a door or a desk. Each person's Pet lives in a shared virtual House, and the Pets' health, happiness and
evolution follow how well the whole household keeps up with its chores. On top of that it brings in the
roommates' work schedules and makes paying each other back one scan away.

From here on, only the X4 build changes. The Waveshare 1.54" build stays as it is: CI keeps building it, but it
gets no new features. New work goes behind `UNIDEX_X4`, and the X4's hardware goes through the FreeInk SDK
(`freeink-sdk/`, a git submodule).

## Where it stands

| Area | Today | RoomiePet needs |
|---|---|---|
| Hardware | The X4 build compiles: display, buttons, battery, SD and sleep through the FreeInk SDK. **Not yet run on an X4.** | Bring-up on real devices (phase 0) |
| Screen | The 200×200 UI drawn at 2× in the middle of the 480×800 panel | A taller canvas that uses the whole screen (phase 1) |
| Buttons | All seven keys fold into two (arrows = A, Confirm/Power = B, Back = hold A) | Up/Down, Left/Right, Confirm and Back as themselves (phase 1) |
| Household | House app: chores on a rota, shopping, money, reminders, Pet messages, roommates, Sync (`docs/HOUSEHOLD.md`) | Taskdex, House Health, monsters, EXP, nudges, visits |
| Syncing | Only while Sync is open on both devices | Completions reach the house within minutes (phase 1) |
| Pets | Look and name; sulk, proud, cheer and pass on messages | Levels, evolution, the Harmony Aura, unlockable cosmetics |
| Time | Synced from the internet, USB or a roommate; London time zone hard-coded | A time-zone setting (phase 1) |
| Security | Anyone in radio range can join a household; any member can edit any record | Join approval, an encrypted household and records only their owner can change (phase 1; required before Cash App codes) |

## Phase 0: bring-up on real X4s (blocks everything)

Nothing has run on hardware yet. On two (ideally three) X4s, check:

- **Screen:** the image is upright and centred, partial refreshes work, and nothing ghosts after a game.
- **Buttons:** every key reads correctly through the SDK's InputManager. Note which keys can wake the device (Power, Right and Down, per the C3's wake pins).
- **SD card:** it mounts through SDCardManager on the bus shared with the panel, and `/house.dat` survives a power cut.
- **Battery:** the deep-sleep current with the GPIO13 latch held, the battery reading against a meter (calibration), and how much the clock drifts over 24 h asleep.
- **Sync:** two and three devices: join, swap, the clock handover, and the "Clock corrected" path.

**Done when:** a written test log (in `docs/X4.md`) and the fixes from it.

## Phase 1: foundations

1. **X4 key events.** Add `Up`, `Down`, `Left`, `Right`, `Confirm`, `Back` (short/long) to `Event`. Existing apps keep the A/B meaning through a compatibility layer; new screens use the real keys (Up/Down scrolls lists, as the blueprint asks).
2. **The tall canvas.** Move the X4 from a 200×200 logical screen to 240×400 drawn at 2×, so it fills the 480×800 panel and keeps today's fonts and pixel art. The theme's fixed numbers (`HINTS_TOP`, rows) become per-board. Apps that only fit 200×200 stay centred until they're moved over.
3. **Time zone setting.** Replace the hard-coded London zone with a choice in Settings, carried in Sync. Chore days, the calendar and the Harmony Aura's "today" all depend on it.
4. **Household security** (required before Cash App codes and calendars go over the air):
   - **Join approval:** a device asks, and someone already in the household says yes on their own X4.
   - **Household key:** a random key handed over at that approval; every household packet is encrypted and authenticated with it (AES-CCM, mbedTLS is already in the firmware). Devices outside the household see only noise.
   - **Owner-only records:** a member's own records (their member card, Cash App code, calendar, EXP tally) are accepted only when the editor is their owner.
5. **Faster syncing without draining batteries.**
   - **Hub mode:** an X4 on USB power (the fridge one, say) keeps its radio listening and relays.
   - **Listen windows:** a sleeping X4 wakes for about 0.3 s on a shared schedule (every 5 minutes, aligned by the synced clocks). A completion is broadcast in the next window or two, so it reaches the house in a few minutes. The cost is a fraction of a mAh an hour; to be measured in phase 0.
6. **Room to grow.** Move the house file to version 2 (migrating version 1), raise the record limit (≈300 records ≈ 20 KB of RAM, which the C3 can spare), and add a done-log with a monthly roll-up (needed by phases 4 and 5).

## Phase 2: Cash App codes (small, high value)

- Each roommate enters their $cashtag once: a letter picker on the X4, or typed on the website's Tools page over USB. It's an owner-only record shared with the household.
- **Money screen:** on "You owe Pip $6.50", B shows a QR code for Pip's Cash App link. The amount is filled in if Cash App's `cash.app/$tag/amount` links still do that (to be checked); otherwise it's the plain profile link. Scanning it with your phone opens Cash App to pay. The QR code library is already in the firmware.
- **Roommates screen:** each Pet's card shows its owner's code.
- The X4 only shows the code: no payment details, logins or money ever pass through the device.

## Phase 3: shared calendar (work schedules)

- **The calendar links:** each roommate who has a calendar link (an iCal/ICS secret address from Google, Outlook or a work rota) saves it on their own X4 through the website. It sits with the WiFi password and is never sent to other devices.
- **Fetching:** on a schedule (and on demand), that X4 fetches its calendar over WiFi with certificate checks. It reads the file as it arrives, since these files can be hundreds of KB, and keeps the next 14 days: one-off events, plus weekly and daily repeating ones (the common `RRULE`s for shift work), in the right time zones.
- **Sharing:** those events go to the household as owner-only shift records: just "busy" by default, with titles if the owner chooses. Roommates without a link (or without WiFi) still see everyone's.
- **Screens:**
  - a **Week** view: one column per day, each roommate's shifts side by side with your own timetable;
  - **Today** on the House menu ("Pip works 9–5, Moss off");
  - the Pet mentions overlaps ("Nobody's home Thursday evening: bins?").
- **Chores and schedules:** the rota can skip a roommate who's working all day, so a chore isn't due on someone's 12-hour shift.

## Phase 4: Taskdex and House Health

- **Taskdex:** Confirm opens it straight from the Pet screen and the home screen. It's a crisp list of the house's chores with whose turn and when ("Wipe Counters, you, today"). Up/Down scrolls, Confirm marks one done, and hold Confirm adds a chore.
- **Chore weights:** each chore has a weight (Take Out Trash 2, Clean Bathroom 3, Wipe Counters 1). Presets come with weights.
- **House Health bar:** 100% minus each overdue chore's weight × days late (capped), shown on the Pet and House screens. Every device works it out from the same records, so every screen agrees.
- **Monsters:** overdue chores spawn creatures on everyone's Pet screen. Trash chores bring Stinky Trash Monsters; cleaning chores bring Dust Bunnies. More days late means more of them, and finishing the chore clears them away. New pixel art goes in the style of `parts.h`.
- **The celebration:** checking off a task plays a short pixel animation (3–4 fast-refresh frames, about 2 s), then the completion goes out in the next listen window, or right away to a hub.

## Phase 5: pet progression

- **EXP:** each chore done gives its weight in EXP to the Pet that did it, worked out from the shared done-log, so it's the same on every device and can't be inflated locally. On-time completions give a bonus.
- **Levels and evolution:** Pets grow through stages (Hatchling, Housemate, Guardian) at EXP thresholds. Each stage adds art (size, a cape, a crest), so a Guardian looks the part.
- **Harmony Aura:** on a day when every chore due has been done, by everyone, the aura shows on every screen (a sparkle frame around the room) and every Pet gets bonus EXP. Streaks of Harmony days unlock cosmetics: a tiny wizard hat, a clean apron, a feather duster, a crown for a 30-day streak. They're added as new parts in `parts.h`, worn through dress-up.
- **Happiness:** a Pet's happiness follows the House Health and its owner's own record. Sulking, proud and cheering stay as they are, with "weary" for a messy house.

## Phase 6: Neighbors (pet-to-pet)

- **Neighbors menu:** the roommates' Pets, with level, room state and today's shift.
- **Nudges:** pick a roommate's Pet and send a playful interaction:
  - throw a virtual tomato (a splat animation on their screen);
  - a polite reminder bubble ("The dishes miss you");
  - a high-five for a finished chore.

  Nudges are records addressed to one member and delivered like completions. A cooldown (one nudge per pair every few hours) keeps it friendly.
- **Visiting:** look at a roommate's room from the shared records: their Pet, level and outfit, and whether their room is clean or messy (their own overdue chores). It works even while their X4 sleeps. When both devices are awake, the visit becomes live in the Pets' existing two-screen Meet room.

## Order and size

| Phase | Depends on | Rough size |
|---|---|---|
| 0 Bring-up | X4 hardware | Days, mostly testing |
| 1 Foundations | 0 | The largest: input, canvas, time zone, security, syncing, storage |
| 2 Cash App codes | 1.4 (owner-only records) | Small |
| 3 Shared calendar | 1.3, 1.4, WiFi set-up on the website | Medium (streaming ICS, repeats, time zones) |
| 4 Taskdex and House Health | 1.1, 1.2, 1.5 | Medium, plus pixel art |
| 5 Progression | 4, 1.6 (done-log) | Medium, plus pixel art |
| 6 Neighbors | 1.5, 4 | Small to medium |

Phases 2 and 3 come before the game phases because they're concrete, everyday wins. They can swap with 4 if the
game loop matters more first.

## Open questions

1. **Confirm and the Taskdex:** should Confirm open the Taskdex from everywhere, including the home screen (where it opens the selected app today), or only from the Pet screen?
2. **Calendar privacy:** share only "busy" times by default, or titles too? Which services make the two calendar links (Google, Outlook, a work scheduling app)?
3. **Time zone:** which one? Is "$" right for money?
4. **A hub:** will one X4 live on USB power (the fridge), so completions arrive at once?
5. **Naming:** should the device and the app be called RoomiePet (the House app becoming "RoomiePet" or "Home")?
6. **The Waveshare build:** keep it building in CI as is, or drop it?
