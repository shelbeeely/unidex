# The House: a household for roommates' Pets

Each roommate keeps their own Pet, and the Pets share a household: who does which chore, what to buy, who paid
for what, the bin day and the rent, and messages the Pets pass on. There is no server and no account. Roommates'
devices swap the household when they're held near each other (**Sync**), over the same radio the Pets use to meet.

The House app is on both boards. On the Xteink X4 it comes right after the Pet on the home screen. The household is
kept on the SD card (`/house.dat`), so it needs a card.

## Starting

- **The first roommate:** House, then B to **start a household**. Their Pet is its first member.
- **Everyone else:** House, then hold B to **join**, held near a device that's already in the household. When it
  says "Join Pip's household?", press B.
- Each roommate is shown by their Pet's name (dress-up and the name are in the Pet app), so the screens say things
  like "Biscuit owes Pip $6.50".

## What's in it

| Screen | What it does | Buttons |
|---|---|---|
| **Chores** | Each chore with whose turn it is and when it's due ("you, today", "Moss, 2d", "Pip, 1d late"). | B: done (the turn moves to the next roommate). Last row: add one (pick the chore, then how often). Hold B: remove |
| **Shopping** | What to buy; what's been got goes to the bottom. | B: got it / not got. Last rows: add (from a list of common things), clear what's been got. Hold B: remove |
| **Money** | Who owes whom, in the fewest payments. | B on a line you're in: the Cash App code of whoever is owed (see below), then B again: settled. Last row: "I paid for...": pick what, then the amount; it's shared by everyone who lived there at the time |
| **Reminders** | Bin day, recycling, rent and so on, every week or every month. | Last row: add (what, then when). Hold B: remove |
| **Pet messages** | What the Pets have said ("Pip: Out of milk!"). | Last row: your Pet says something (from a list). Hold B: remove |
| **Roommates** | The Pets in the household, in the order they joined (the rota's order), each with its owner's $cashtag. | B on yourself: set your $cashtag. B on a roommate: their Cash App code. Hold B: remove a roommate who has moved out. Hold B on the Roommates row of the menu: leave the household (this device only) |
| **Sync with roommates** | Swaps the household with every roommate's device in reach, for as long as it's open (it stops by itself after 90 s without a press). | Hold A: back |

Changes made on one device reach the others at their next Sync, directly or through someone else's device. A
change made on two devices before they meet: the later one wins.

## Paying each other (Cash App)

Each roommate sets their own $cashtag once, on their own device: Roommates, B on yourself, then the tag a letter at
a time (A: next letter, hold B: previous, B: next place; B on an empty place at the end saves; all empty removes
it). It reaches the others at the next Sync.

On **Money**, B on "You owe Pip $6.50" shows a QR code for Pip's Cash App link with the amount
(`https://cash.app/$pip/6.50`). Scan it with your phone's camera and Cash App opens to pay Pip. When the line is
someone owing you, it shows your own code, so they can scan it off your screen. B on that screen then marks it
settled. The X4 only shows the code: no payment details or money go through it.

A $cashtag is only accepted from its owner's own device, so nobody else's unidex can change where your
roommates' money goes. The amount on the end of the link is the form in common use but isn't documented by Cash
App: if a phone opens Cash App without the amount, `AMOUNT_IN_LINK` in `src/apps/house/house.cpp` turns it off.

### The web app (X4): manage everything from a phone or computer

The household, Cash App, badges, the timetable and settings can all be managed from a browser
([WEBAPP.md](WEBAPP.md)): on a phone through the X4's own WiFi (House, **Manage on phone**), or on a computer over
USB from the website. Its Money page has **Pay in Cash App** and **Share** for each debt, and a box to **paste a
Cash App request**: in Cash App, Request, the amount and note, Share link, Copy, then paste it (the link alone, or
everything Cash App shared: the amount is read from "$15" in it) and pick who it's for. After the next Sync,
roommates' X4s show that exact request as the QR code on Money in place of your $cashtag link, until they settle up
with you; when they owe you, Share sends it to them. Like $cashtags, a request is only accepted from its owner's own
device.

## The Pets

- Your Pet **sulks** (eyes down, "Bins out is late...") while one of your chores is late.
- It's **proud** (happy eyes) on a day you've done one.
- It **cheers** when a Sync brings news that a roommate did a chore ("Moss did Dishes! Yay!"), and it **passes on**
  their Pets' messages ("Pip says: Pizza night?"). The news stays until you say hi (A on the Pet) and shows on the
  home screen meanwhile.
- On a reminder's day it says so ("Bin day today!").

## Earning cosmetics

Every chore your Pet does earns it EXP: 10, or 15 when it's done by its due day (marked done on the X4 or in the web
app). Each 50 EXP is a level (on the Pet screen's header). EXP unlocks dress-up parts:

| Part | EXP |
|---|---|
| Apron | 30 |
| Wizard hat | 60 |
| Feather duster | 100 |
| Chef hat | 150 |
| Medal | 250 |

Everything else is free from the start, including the **bunny lad**: the Bunny body, the backwards **Cap** (its ears
poke through) and the Bow tie. Parts not earned yet are skipped in dress-up (Pet, B), and refused if set over USB or
from the web app. When one unlocks, the Pet says so ("Unlocked: Wizard hat! Dress me up"). The web app's Settings has
the same dress-up, with locked parts greyed out and a **Bunny lad** button that puts the outfit on in one go.

EXP is kept on each X4 (it's that Pet's own); it isn't shared with the household yet.

## The time

Chores need the date. A device whose clock isn't set takes the time from a roommate's at Sync. The X4 has no clock
chip: its ESP32-C3 keeps the time through deep sleep with its RTC timer, which drifts while it sleeps, and loses it
when the battery is cut or runs flat. So each device also says when its time was last set from the internet, the USB
sync or a clock chip, and Sync corrects a clock that's more than 30 s out from a roommate's that was set more than an
hour more recently.

## How it works

`src/apps/house/house_logic.h` (checked on a computer by `tools/housetest`) holds the rules. Everything is a record
(a member, a chore, an item, an expense, a payment, a reminder, a message), known by who made it and their count.
Every change is stamped with a Lamport clock and who made it, and merging keeps the newest copy of each record, so
every device ends up with the same household whatever order the copies arrive in, and however often. Removing is a
change too, so it spreads. When the household is full (120 records), the oldest removed ones are dropped. Money is
worked out from the expenses and payments each time: an expense is shared by the roommates who had joined by then
(a device joining starts its clock after the household's, so it doesn't share what was spent before it came). The
radio packets are described at the bottom of the file.
