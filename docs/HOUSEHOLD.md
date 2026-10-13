# The House: a household for roommates' Pets (RoomiePet)

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

## Dress-up, pronouns, pride flags and gifts

A Pet's look has seven layers: Body, Eyes, Mouth, Hat, Accessory, **Hair** and **Outfit**, from a wardrobe of over
120 parts. It's drawn in this order: the body, its outfit, the face, the hair (a fringe can cover an eye), then hats
and accessories, then the pronoun pin.

- **On the X4:** Pet, B. The rows go Outfit, Hair, Hat, Accessory, Body, Eyes, Mouth (B: next, hold B: previous),
  then **Holding** (what it holds: [ITEMS.md](ITEMS.md)), **Pronouns**, **Flag** (your pride flag), **Pin** (B: off, then pronouns, then flag), Name, Random look and
  Done.
- **In the web app:** the **Pet** tab is the Pet designer: a live preview, a row a layer with ◀ and ▶, a strip of
  every piece in the layer you're on (tap one to put it on), Random, the name, your pronouns (a list, or Other... for
  anything up to 12 characters), your pride flag, the pin (none, pronouns or flag), and **Wear it on the X4**. **Load from the X4** puts back what it's wearing.

The parts:

- **Outfits (26):** Skirt, Hoodie, Sweater, Band tee, Stripes, Corset*, Leather*, Flannel, Sundress, Overalls, Tutu, Crop top, Maid dress*, Mesh top, Plaid, Ripped tee, Patch vest, Zip hoodie, Checkers, Cape, Lace dress*, Suit*, Tank top, Blouse, Cardigan, Waistcoat
- **Hair (18):** Fluffy bob, Long, Buzz cut, Mohawk, Undercut, Emo fringe, Pigtails, Split dye, Space buns, Waves, Wolf cut, Soft curls, Bangs, Liberty, Side swoop, Ringlets, Slick back, Ponytail
- **Hats (23):** Bow, Beanie, Crown, Flower, Headphones, Cap, Wizard hat*, Chef hat*, Cat ears, Horns*, Halo*, Witch hat*, Veil*, Wreath*, Bucket hat, Maid band, Clips, Bandana, Scene bow, Top hat, Sweatband, Sun hat, Tiara*
- **Accessories (30):** Blush, Scarf, Bow tie, Star, Apron*, Duster*, Medal*, Choker, Studs, Pearls*, Chain, Bat wings*, Hearts, Ankh, Bandaid, Hair bow, Heart clip, Star clips, Spikes, Safety pin, Nose ring, Wristband, Tear, Cross, Spider*, Dog tags, Shades*, Earrings, Rose*, Carrot
- **Eyes:** Dots, Sparkly, Happy, Sleepy, Wink, Glasses, Eyeliner, Lashes, Hearts, Smudged. **Mouths:** Smile, Open, Cat, Tongue, Small o, Fangs, Lip ring, Lipstick, Smirk. **Bodies:** Blob, Cat, Bear, Bunny, Frog, Robot

\* a gift (below). Everything else is a starter every Pet has, so nobody has to earn being themselves.

### Pronouns

Each roommate can set their pronouns: he/him, she/her, they/them, he/they, she/they, they/he, they/she, any, it/its,
xe/xem, ze/zir, fae/faer or "ask me" on the X4, or anything up to 12 characters (letters, `/`, `-`, spaces and
apostrophes) in the web app. They show:

- beside the Pet's name on its screen ("Pip (she/her)  Lv 2");
- on the Pet's **pronoun pin**, if it wears one: a little badge on its chest with them in capitals (THEY/THEM), on the
  Pet screen, in dress-up, in Meet and on friends' lists, and in the web app and on the website;
- on its name tag when it meets other Pets, and in their friends lists (Meet carries them);
- on the household's **Roommates**, on every roommate's X4 and in the web app (the member card carries them, so they
  reach everyone at the next Sync).

### Pride flags

Rainbow, Progress, Trans, Nonbinary, Bi, Pan, Lesbian, Gay (MLM), Ace, Aro, Aroace, Genderfluid, Genderqueer,
Agender and Intersex (`src/apps/pet/flags.h`). Pick one on the X4 (dress-up's Flag row) or in the web app. It shows:

- before the Pet's name on its screen, and in dress-up;
- on its **flag pin**, if the pin is set to show the flag rather than the pronouns;
- on its name tag when it meets other Pets, and in their friends lists (Meet carries it);
- on the household's **Roommates**, on every roommate's X4 and in the web app (the member card carries it in d).

The X4's screen is black and white, so each colour becomes a texture as dark as it is (white, sparse dots, thin lines,
a checker, diagonal lines, a dense grid, solid), and two stripes next to each other never get the same one; the
intersex flag is its ring on plain paper. The web app and the website show the flags in colour.

### Things to hold and wear

What a Pet holds and the catalog clothes it wears ([ITEMS.md](ITEMS.md)) show on every roommate's X4 and in the web
app too: what it holds rides on the member card (d, above the flag), and its catalog hat, accessory, hair and outfit
on a record of their own (`WEAR`), which, like a $cashtag, only its owner's device can change. A roommate's X4 that
doesn't have one of those items yet shows the wardrobe's part until the item reaches it (it comes at the same Sync).

### Gifts

Nothing is bought or spent. Like loot from a game's NPC, a gift can drop each time your Pet does a chore (on the X4
or in the web app): first a chance, then how rare it is, then a piece your Pet hasn't found yet, never a duplicate.
The Pet says what it found ("Gift! Halo (rare) Dress me up") and the X4 shows it when you mark the chore done.

| What sways the chance | |
|---|---|
| Every chore | 20% |
| Done by its due day | +10% |
| Covering a roommate's turn | +15% |
| Rescuing a late chore | +10% |
| Your first chore today | +10% |
| Each day in a row with a chore, up to 5 | +3% |
| 4 chores in a row without a gift | the 5th always brings one |

Then the rarity: 3% legendary, 22% rare, the rest uncommon. Covering a roommate's turn and rescuing a late chore
each add 3 points to rare (and 1.5 to legendary), and each day of a streak 1 more (up to 7). If none of the rolled
rarity are left, the nearest rarity that has some is given instead.

| Rarity | Gifts |
|---|---|
| Uncommon | Apron, Devil horns, Pearls, Leather jacket, Flower wreath, Feather duster |
| Rare | Halo, Wizard hat, Bat wings, Corset, Witch hat, Chef hat |
| Legendary | Lace veil, Medal |

Chores still give EXP, 10 or 15 on time, but only for the Pet's level (each 50 EXP, on the Pet screen's header): it
is never spent. Gifts not found yet are skipped in dress-up and refused if set over USB or from the web app, which
shows them as "not found yet". Gifts are kept on each X4 (they're that Pet's own) and aren't shared with the
household; roommates do see each other's looks, outfits and all, at Sync and when the Pets meet. The odds are in
`src/apps/pet/pet_logic.h` (`dropChance`, `rollRarity`, `rollDrop`), checked by `tools/pettest`.

The website's Tools page designer still shows the first five layers only: saving a look from it takes off the hair
and outfit (put them back on the X4 or in its web app).

## Home WiFi and the home server

The household key, the WiFi networks roommates share, and the home server that keeps X4s in step while they're
charging at home: [HOMESERVER.md](HOMESERVER.md).

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
