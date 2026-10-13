#pragma once
// Pet's look: one part per layer, saved as one number, and drawn from the 32x32 parts in parts.h. No Arduino code,
// so it can be checked on a computer (tools/pettest).
// The Waveshare build (unidex) packs 4 bits a layer and keeps the parts it always had. The X4 build (RoomiePet) has
// more hats, accessories, hair and outfits, so it packs 3 bits for the body, 4 for eyes and mouth and 5 for the
// rest (31 bits); the top bit says the Pet wears its pronoun pin (pronouns.h).
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "parts.h"
#include "pronouns.h"
#include "flags.h"

#ifndef UNIDEX_X4
#define UNIDEX_X4 0
#endif

namespace pet {

enum Layer { BODY, EYES_LAYER, MOUTH, HAT, EXTRA, HAIR, OUTFIT, LAYERS };
static const char *const LAYER_NAMES[LAYERS] = {"Body", "Eyes", "Mouth", "Hat", UNIDEX_X4 ? "Accessory" : "Extra",
                                                 "Hair", "Outfit"};
// Bits a layer takes in a saved look, and so the most parts a layer can have.
static const uint8_t LAYER_BITS[LAYERS] = {UNIDEX_X4 ? 3 : 4, 4, 4, UNIDEX_X4 ? 5 : 4, UNIDEX_X4 ? 5 : 4,
                                           UNIDEX_X4 ? 5 : 4, UNIDEX_X4 ? 5 : 4};
// The Waveshare build keeps the parts it shipped with: the RoomiePet ones are added after them.
static const uint8_t WAVESHARE_PARTS[LAYERS] = {6, 10, 9, 16, 16, 12, 12};
// The order layers are drawn in: clothes under the face, hair over the eyes (a fringe can cover one), hats over hair.
static const Layer DRAW_ORDER[LAYERS] = {BODY, OUTFIT, EYES_LAYER, MOUTH, HAIR, HAT, EXTRA};
static const int SIZE = 32;  // each part is SIZE x SIZE pixels
static const int HAPPY_EYES = 2;  // EYES[2], "Happy": shown while it hops

template <size_t N> constexpr int countOf(const PetPart (&)[N]) { return N; }

struct LayerParts {
  const PetPart *parts;
  int count;
};

inline LayerParts partsOfAll(int layer) {
  switch (layer) {
    case BODY: return {BODIES, countOf(BODIES)};
    case EYES_LAYER: return {EYES, countOf(EYES)};
    case MOUTH: return {MOUTHS, countOf(MOUTHS)};
    case HAT: return {HATS, countOf(HATS)};
    case HAIR: return {HAIRS, countOf(HAIRS)};
    case OUTFIT: return {OUTFITS, countOf(OUTFITS)};
    default: return {EXTRAS, countOf(EXTRAS)};
  }
}

inline LayerParts partsOf(int layer) {
  LayerParts lp = partsOfAll(layer);
  if (!UNIDEX_X4 && lp.count > WAVESHARE_PARTS[layer]) lp.count = WAVESHARE_PARTS[layer];
  return lp;
}

inline int shiftOf(int layer) {
  int s = 0;
  for (int i = 0; i < layer; i++) s += LAYER_BITS[i];
  return s;
}

struct Look {
  uint8_t part[LAYERS];
  bool pin;  // wears its pronoun pin (the X4 only: the top bit of a saved look)
};

// The look a new Pet starts with: the first body, eyes and mouth, no hat, blush, no hair, no outfit.
static const Look DEFAULT_LOOK = {{0, 0, 0, 0, 1, 0, 0}, false};

static const uint32_t PIN_BIT = 1u << 31;

inline uint32_t pack(const Look &l) {
  uint32_t v = 0;
  for (int i = 0; i < LAYERS; i++) v |= (uint32_t)(l.part[i] & ((1u << LAYER_BITS[i]) - 1)) << shiftOf(i);
  if (UNIDEX_X4 && l.pin) v |= PIN_BIT;
  return v;
}

// A saved number back to a look. A part that doesn't exist (a damaged value, or a part removed in a later
// version) falls back to the default for that layer.
inline Look unpack(uint32_t v) {
  Look l;
  for (int i = 0; i < LAYERS; i++) {
    const uint8_t p = (v >> shiftOf(i)) & ((1u << LAYER_BITS[i]) - 1);
    l.part[i] = p < partsOf(i).count ? p : DEFAULT_LOOK.part[i];
  }
  l.pin = UNIDEX_X4 && (v & PIN_BIT);
  return l;
}

// Option `step` places along in a layer (+1 next, -1 previous), wrapping around.
inline void stepPart(Look &l, int layer, int step) {
  const int n = partsOf(layer).count;
  l.part[layer] = (uint8_t)(((l.part[layer] + step) % n + n) % n);
}

// ---- Gifts (RoomiePet). Nothing is bought or spent: every part is either a starter (every Pet has it from the start,
// including each style's everyday pieces, so nobody has to earn being themselves) or a gift that drops after a
// chore, like loot from a game's NPC: a chance raised by how the chore went, then a rarity, then a part this Pet
// doesn't have yet. A dry spell ends with a guaranteed gift. EXP from chores only sets the Pet's level.
enum Rarity : uint8_t { STARTER, UNCOMMON, RARE, LEGENDARY };
static const char *const RARITY_NAMES[] = {"starter", "uncommon", "rare", "legendary"};
struct Drop {
  uint8_t layer, part, rarity;
  const char *what;  // for "Gift! ..." (the part's name and layer)
};
static const Drop DROPS[] = {
  {EXTRA, 5, UNCOMMON, "Apron"},          {HAT, 10, UNCOMMON, "Devil horns"},  {EXTRA, 10, UNCOMMON, "Pearls"},
  {OUTFIT, 7, UNCOMMON, "Leather jacket"}, {HAT, 14, UNCOMMON, "Flower wreath"}, {EXTRA, 6, UNCOMMON, "Feather duster"},
  {HAT, 11, RARE, "Halo"},                {HAT, 7, RARE, "Wizard hat"},        {EXTRA, 12, RARE, "Bat wings"},
  {OUTFIT, 6, RARE, "Corset"},            {HAT, 12, RARE, "Witch hat"},        {HAT, 8, RARE, "Chef hat"},
  {HAT, 13, LEGENDARY, "Lace veil"},      {EXTRA, 7, LEGENDARY, "Medal"},
#if UNIDEX_X4  // RoomiePet's parts
  {EXTRA, 25, UNCOMMON, "Spider"},        {EXTRA, 29, UNCOMMON, "Rose"},       {EXTRA, 27, UNCOMMON, "Shades"},
  {OUTFIT, 22, UNCOMMON, "Suit"},         {OUTFIT, 13, RARE, "Maid dress"},    {OUTFIT, 21, RARE, "Lace dress"},
  {HAT, 23, LEGENDARY, "Tiara"},
#endif
};
static const int DROP_COUNT = sizeof DROPS / sizeof DROPS[0];

inline const Drop *dropOf(int layer, int part) {
  for (const Drop &d : DROPS)
    if (d.layer == layer && d.part == part) return &d;
  return nullptr;
}
inline uint8_t rarityOf(int layer, int part) {
  const Drop *d = dropOf(layer, part);
  return d ? d->rarity : STARTER;
}

// The gifts a Pet has: one bit per part, per layer (saved in NVS as pet_owned).
struct Owned {
  uint32_t layer[LAYERS];
};
inline bool owns(const Owned &o, int layer, int part) {
  return rarityOf(layer, part) == STARTER || (o.layer[layer] >> part & 1);
}
inline void give(Owned &o, const Drop &d) { o.layer[d.layer] |= 1u << d.part; }
inline int giftsOwned(const Owned &o) {
  int n = 0;
  for (const Drop &d : DROPS) n += owns(o, d.layer, d.part);
  return n;
}

// stepPart, skipping parts this Pet hasn't been given.
inline void stepOwned(Look &l, int layer, int step, const Owned &o) {
  const int n = partsOf(layer).count;
  for (int i = 0; i < n; i++) {
    stepPart(l, layer, step);
    if (owns(o, layer, l.part[layer])) return;
  }
}

// A look with any part not owned put back to the default (a look set over USB or the web app).
inline Look onlyOwned(Look l, const Owned &o) {
  for (int i = 0; i < LAYERS; i++)
    if (!owns(o, i, l.part[i])) l.part[i] = DEFAULT_LOOK.part[i];
  return l;
}

// How a chore went, for the drop: done by its due day, covering a roommate's turn, rescuing a late one, the Pet's
// first chore today, how many days in a row it has done one, and chores since its last gift.
struct ChoreLuck {
  bool onTime, helped, rescued, firstToday;
  uint8_t streak, dry;
};
static const int PITY = 5;  // the 5th chore without a gift always brings one

// The chance of a gift, in percent.
inline int dropChance(const ChoreLuck &c) {
  if (c.dry + 1 >= PITY) return 100;
  int p = 20 + (c.onTime ? 10 : 0) + (c.helped ? 15 : 0) + (c.rescued ? 10 : 0) + (c.firstToday ? 10 : 0) +
          3 * (c.streak < 5 ? c.streak : 5);
  return p > 85 ? 85 : p;
}

// The rarity rolled (r: 0-99): better odds of rare and legendary for helping out, rescuing a late chore and streaks.
inline uint8_t rollRarity(const ChoreLuck &c, uint32_t r) {
  const int boost = (c.helped ? 3 : 0) + (c.rescued ? 3 : 0) + (c.streak < 7 ? c.streak : 7);
  const int legendary = 3 + boost / 2, rare = 22 + boost;
  r %= 100;
  return r < (uint32_t)legendary ? LEGENDARY : r < (uint32_t)(legendary + rare) ? RARE : UNCOMMON;
}

// A chore's drop, from three random numbers (the chance, the rarity, which part): a part this Pet doesn't have yet,
// of the rarity rolled (or the nearest one with any left), or nullptr (no luck this time, or it has everything).
inline const Drop *rollDrop(const ChoreLuck &c, const Owned &o, uint32_t r1, uint32_t r2, uint32_t r3) {
  if ((int)(r1 % 100) >= dropChance(c)) return nullptr;
  const int want = rollRarity(c, r2);
  static const int SEARCH[4][3] = {{0, 0, 0}, {UNCOMMON, RARE, LEGENDARY}, {RARE, UNCOMMON, LEGENDARY},
                                   {LEGENDARY, RARE, UNCOMMON}};
  for (int t : SEARCH[want]) {
    int left = 0;
    for (const Drop &d : DROPS) left += d.rarity == t && !owns(o, d.layer, d.part);
    if (!left) continue;
    int pick = r3 % left;
    for (const Drop &d : DROPS)
      if (d.rarity == t && !owns(o, d.layer, d.part) && pick-- == 0) return &d;
  }
  return nullptr;
}

// Level from EXP: 1 at the start, then one every 50.
inline int levelOf(uint32_t exp) { return 1 + exp / 50; }

// A random look from any random number source (one call per layer).
template <typename Rand> Look randomLook(Rand rand) {
  Look l = DEFAULT_LOOK;
  for (int i = 0; i < LAYERS; i++) l.part[i] = (uint8_t)(rand() % (uint32_t)partsOf(i).count);
  return l;
}

// A random look from what this Pet owns: one call of rand per layer.
template <typename Rand> Look randomOwned(Rand rand, const Owned &o) {
  Look l = DEFAULT_LOOK;
  for (int i = 0; i < LAYERS; i++) {
    const int n = partsOf(i).count;
    int choices = 0;
    for (int p = 0; p < n; p++) choices += owns(o, i, p);
    int pick = (int)(rand() % (uint32_t)choices);  // never 0 choices: part 0 is always owned
    for (int p = 0; p < n; p++)
      if (owns(o, i, p) && pick-- == 0) {
        l.part[i] = (uint8_t)p;
        break;
      }
  }
  return l;
}

// Draws the look, layer by layer: pixel(x, y, ink) for every pixel a layer sets ('#' ink, 'o' paper), with x
// and y from 0 to SIZE - 1. eyesClosed draws EYES_CLOSED instead of the chosen eyes (a blink). instead(layer) may
// draw something else in a layer's place (a catalog piece, items_logic.h) and return true; then the part isn't drawn.
template <typename Pixel, typename Instead> void drawLook(const Look &l, bool eyesClosed, Pixel pixel, Instead instead) {
  for (const Layer layer : DRAW_ORDER) {
    if (instead((int)layer)) continue;
    const PetPart &p = layer == EYES_LAYER && eyesClosed ? EYES_CLOSED : partsOf(layer).parts[l.part[layer]];
    for (int y = 0; y < SIZE; y++)
      for (int x = 0; x < SIZE; x++) {
        const char c = p.rows[y][x];
        if (c == '#') pixel(x, y, true);
        else if (c == 'o') pixel(x, y, false);
      }
  }
}
template <typename Pixel> void drawLook(const Look &l, bool eyesClosed, Pixel pixel) {
  drawLook(l, eyesClosed, pixel, [](int) { return false; });
}

// Naming on the device, a letter at a time: the name is MAX_NAME places, blank (space) where unused. A cycles the
// letter in the current place through NAME_CHARS; B moves on, and finishes on a blank with only blanks after it
// (or on the last place).
static const int MAX_NAME = 12;
static const char NAME_CHARS[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-";

inline char stepChar(char c, int step) {
  const int n = sizeof NAME_CHARS - 1;
  const char *at = c ? strchr(NAME_CHARS, c) : nullptr;
  const int i = at ? (int)(at - NAME_CHARS) : 0;  // anything else counts as a blank
  return NAME_CHARS[((i + step) % n + n) % n];
}

inline bool nameDoneAt(const char *places, int pos) {
  if (pos >= MAX_NAME - 1) return true;
  for (int i = pos; i < MAX_NAME; i++)
    if (places[i] != ' ') return false;
  return true;
}

// The places as a name: no blanks at either end.
inline void nameFromPlaces(const char *places, char *out) {
  int start = 0, end = MAX_NAME;
  while (start < end && places[start] == ' ') start++;
  while (end > start && places[end - 1] == ' ') end--;
  memcpy(out, places + start, end - start);
  out[end - start] = 0;
}

// A name into places: printable letters kept, padded with blanks.
inline void placesFromName(const char *name, char *places) {
  int i = 0;
  for (; name[i] && i < MAX_NAME; i++) places[i] = name[i] >= 0x20 && name[i] <= 0x7E ? name[i] : ' ';
  for (; i < MAX_NAME; i++) places[i] = ' ';
  places[MAX_NAME] = 0;
}

}  // namespace pet
