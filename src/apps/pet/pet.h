#pragma once
#include <Arduino.h>

// Shared by the Pet screen (pet.cpp) and its USB commands (usb.cpp).
uint32_t petLookBits();  // the saved look, as pet_logic.h packs it
String petName();        // "" if none is set
String petPronouns();    // "" if none are set (always on the Waveshare board), e.g. "she/her" (pronouns.h)
uint32_t petHeld();      // what it holds: an item id (held.h), 0 for nothing (always on the Waveshare board)
uint8_t petFlag();       // the pride flag byte (flags.h: its index, and ON_PIN when the pin shows it); 0 for none
// A pride flag w x h at x, y with a white rim (nothing for none).
void petDrawFlag(uint8_t flag, int16_t x, int16_t y, int16_t w, int16_t h);
uint32_t petId();        // this device's Pet: a random number made once (meet.cpp); its id in the household too
// Chores (RoomiePet): each gives the Pet EXP, which only sets its level, and a chance of a gift (pet_logic.h's
// DROPS), like loot: better for a chore done on time, covering a roommate's turn, rescuing a late one, the first one
// today and a streak of days; a dry spell ends with a sure one. petChoreDone returns "Gift! Halo (rare)" and passes
// it on as the Pet's news, or returns nullptr when nothing dropped.
uint32_t petExp();
const char *petChoreDone(bool onTime, bool helped, bool rescued);
// Which gifts this Pet has found (pet_logic.h's Owned, one bit per part a layer), saved in NVS.
void petOwned(uint32_t out[7]);
void petChanged();       // the website saved a new look or name: the Pet screen redraws
enum class PetMood : uint8_t { Normal, Happy, Blink };
// A Pet's look, scale x 32 px, its top left at x0, y0 (parts off the screen are skipped); Happy and Blink swap in
// the happy or closed eyes.
// A look that wears its pin shows the pronouns, or the flag when the flag byte says so (at a scale of 2 or more).
void petDraw(uint32_t look, int scale, int16_t x0, int16_t y0, PetMood mood = PetMood::Normal,
             const char *pronouns = nullptr, uint8_t flag = 0, uint32_t held = 0);  // held: an item id
void petDrawHeart(int16_t cx, int16_t cy, int scale);  // a small heart centred at cx, cy (9 x 8 at scale 1)
// A name tag over a Pet's head, like a game's player name: white text in a black box centred at cx, its bottom
// edge at `bottom`. Nothing for an empty name.
void petDrawTag(const char *name, int16_t cx, int16_t bottom, uint8_t flag = 0);  // with the owner's flag first
// A speech bubble: the text in a white rounded box with a black edge and a small tail pointing down at cx; the
// box is centred at cx but kept on the screen; its tail's tip at `bottom`.
void petDrawBubble(const char *text, int16_t cx, int16_t bottom);

// The website's Tools page (site/pet.js) talks to these over USB. Called from usbsync for every line starting
// with "P "; returns false if it isn't one of them.
//   P GET                        -> "OK P <look, 8 hex digits> <name, hex> <pronouns, hex> <flag, 2 hex digits>
//                                   <held, 6 hex digits>" (the X4; the Waveshare board: 7 digits, nothing after the name)
//   P SET <look hex> <name hex> [<pronouns hex> [<flag hex> [<held id hex>]]]  -> "OK P SET" or "ERR"  (name: up
//                                   to 12 printable characters; held: an item this X4 has, 0 for nothing;
//                                   "-" or nothing = no name; pronouns (the X4): cleaned by pronouns.h, "-" = none;
//                                   gifts not found yet are left at the default)
//   P PARTS                      -> "PP <layer> <index> <rarity> <owned 0/1> <hex name>" per part (rarity 0
//                                   starter, 1 uncommon, 2 rare, 3 legendary),
//                                   "OK P PARTS <EXP> <gifts found> <gifts in all>"
bool petUsb(const char *line);
