#pragma once
// Pet's look: one part per layer, saved as one number (4 bits a layer), and drawn from the 32x32 parts in
// parts.h. No Arduino code, so it can be checked on a computer (tools/pettest).
// Hair and Outfit (RoomiePet) came later: they take the next 8 bits, so a look saved before them (or sent by a
// device or website that doesn't know them) simply has none, and devices that don't know them ignore them.
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "parts.h"

namespace pet {

enum Layer { BODY, EYES_LAYER, MOUTH, HAT, EXTRA, HAIR, OUTFIT, LAYERS };
static const char *const LAYER_NAMES[LAYERS] = {"Body", "Eyes", "Mouth", "Hat", "Extra", "Hair", "Outfit"};
// The order layers are drawn in: clothes under the face, hair over the eyes (a fringe can cover one), hats over hair.
static const Layer DRAW_ORDER[LAYERS] = {BODY, OUTFIT, EYES_LAYER, MOUTH, HAIR, HAT, EXTRA};
static const int SIZE = 32;  // each part is SIZE x SIZE pixels
static const int HAPPY_EYES = 2;  // EYES[2], "Happy": shown while it hops

template <size_t N> constexpr int countOf(const PetPart (&)[N]) { return N; }

struct LayerParts {
  const PetPart *parts;
  int count;
};

inline LayerParts partsOf(int layer) {
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

struct Look {
  uint8_t part[LAYERS];
};

// The look a new Pet starts with: the first body, eyes and mouth, no hat, blush, no hair, no outfit.
static const Look DEFAULT_LOOK = {{0, 0, 0, 0, 1, 0, 0}};

inline uint32_t pack(const Look &l) {
  uint32_t v = 0;
  for (int i = 0; i < LAYERS; i++) v |= (uint32_t)(l.part[i] & 0x0F) << (4 * i);
  return v;
}

// A saved number back to a look. A part that doesn't exist (a damaged value, or a part removed in a later
// version) falls back to the default for that layer.
inline Look unpack(uint32_t v) {
  Look l;
  for (int i = 0; i < LAYERS; i++) {
    const uint8_t p = (v >> (4 * i)) & 0x0F;
    l.part[i] = p < partsOf(i).count ? p : DEFAULT_LOOK.part[i];
  }
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
  uint16_t layer[LAYERS];
};
inline bool owns(const Owned &o, int layer, int part) {
  return rarityOf(layer, part) == STARTER || (o.layer[layer] >> part & 1);
}
inline void give(Owned &o, const Drop &d) { o.layer[d.layer] |= 1 << d.part; }
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

// ---- Styles: whole outfits in one go (dress-up's Style row, the web app). One part per layer, -1 = keep what's
// worn. Gifts this Pet hasn't found yet are left as they are, so a style fills in as they drop.
struct Style {
  const char *name;
  int8_t part[LAYERS];  // BODY, EYES, MOUTH, HAT, EXTRA, HAIR, OUTFIT
};
static const Style STYLES[] = {
  {"Femboy", {-1, 7, 0, 9, 8, 1, 1}},       // lashes, cat ears, bell choker, fluffy bob, pleated skirt
  {"E-girl", {-1, 6, 5, 0, 13, 8, 5}},      // eyeliner, fangs, cheek hearts, split dye, striped top
  {"Alternative", {-1, 0, 6, 0, 9, 4, 4}},  // lip ring, studded collar, mohawk, band tee
  {"Emo", {-1, 9, 4, 0, 11, 6, 2}},         // smudged eyes, chain, emo fringe, hoodie
  {"Goth", {-1, 6, 7, 13, 14, 2, 6}},       // eyeliner, dark lips, veil, ankh, long hair, corset
  {"Masc", {-1, 0, 8, 15, 11, 3, 7}},       // smirk, bucket hat, chain, buzz cut, leather jacket
  {"Femme", {-1, 7, 7, 14, 10, 10, 9}},     // lashes, lipstick, flower crown, pearls, waves, sundress
  {"Bunny lad", {3, -1, -1, 6, 3, -1, -1}}, // bunny, backwards cap, bow tie
};
static const int STYLE_COUNT = sizeof STYLES / sizeof STYLES[0];

inline Look applyStyle(Look l, const Style &s, const Owned &o) {
  for (int i = 0; i < LAYERS; i++)
    if (s.part[i] >= 0 && s.part[i] < partsOf(i).count && owns(o, i, s.part[i])) l.part[i] = s.part[i];
  return l;
}

// A random look from any random number source (one call per layer).
template <typename Rand> Look randomLook(Rand rand) {
  Look l;
  for (int i = 0; i < LAYERS; i++) l.part[i] = (uint8_t)(rand() % (uint32_t)partsOf(i).count);
  return l;
}

// Draws the look, layer by layer: pixel(x, y, ink) for every pixel a layer sets ('#' ink, 'o' paper), with x
// and y from 0 to SIZE - 1. eyesClosed draws EYES_CLOSED instead of the chosen eyes (a blink).
template <typename Pixel> void drawLook(const Look &l, bool eyesClosed, Pixel pixel) {
  for (const Layer layer : DRAW_ORDER) {
    const PetPart &p = layer == EYES_LAYER && eyesClosed ? EYES_CLOSED : partsOf(layer).parts[l.part[layer]];
    for (int y = 0; y < SIZE; y++)
      for (int x = 0; x < SIZE; x++) {
        const char c = p.rows[y][x];
        if (c == '#') pixel(x, y, true);
        else if (c == 'o') pixel(x, y, false);
      }
  }
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
