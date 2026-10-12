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

### Pay by phone (X4): open, share, and paste Cash App requests

House, **Pay by phone** starts the X4's own WiFi hotspot and shows its QR code. Scan it with your phone's camera:
the phone joins and the page opens by itself (if it doesn't, open any website and it redirects). On the page:

- **Each roommate:** what you owe them (or they owe you), their $cashtag, **Open in Cash App** and **Share**.
- **Share:** phones only open their share sheet from secure (https) pages, and this page comes from the X4 over plain
  http, so Share uses the share sheet where the phone allows it and otherwise offers **Copy link**, **Text it**
  (opens Messages with the link) and **Email**. These work the same on iPhone and Android. Pressing and holding a link
  also brings up the phone's own share option.
- **Paste a Cash App request:** in Cash App, Request, enter the amount and note, then Share link and Copy. Paste it
  into the box (the link alone, or everything Cash App shared: the amount is read from "$15" in it), pick who it's
  for (or Anyone) and save. After the next Sync, your roommates' X4s show that exact request, with your amount and
  note, as the QR code on Money, in place of your $cashtag link. A request stops showing for someone once they've
  settled up with you after it was made. Your requests are listed on the page with **Remove**.

Like $cashtags, a request is only accepted from its owner's own device. While the phone is on the X4's hotspot it
has no internet, so Cash App itself opens once the phone is back on your WiFi or data (it switches back by itself
when you leave the page). The hotspot has a new password each time and stops after 5 minutes unused, or when you
leave the screen (hold A).

## The Pets

- Your Pet **sulks** (eyes down, "Bins out is late...") while one of your chores is late.
- It's **proud** (happy eyes) on a day you've done one.
- It **cheers** when a Sync brings news that a roommate did a chore ("Moss did Dishes! Yay!"), and it **passes on**
  their Pets' messages ("Pip says: Pizza night?"). The news stays until you say hi (A on the Pet) and shows on the
  home screen meanwhile.
- On a reminder's day it says so ("Bin day today!").

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
