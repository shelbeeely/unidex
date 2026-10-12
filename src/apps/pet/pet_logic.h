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

// ---- Earned cosmetics (RoomiePet). Doing chores gives a Pet EXP (house/store.cpp: 10 a chore, 15 on time); these
// parts unlock at the EXP shown. Every other part is free from the start (the Bunny body and the Cap among them:
// the bunny lad needs nothing earned).
struct Unlock {
  uint8_t layer, part;
  uint16_t exp;
  const char *what;  // for "Unlocked: ..." (the part's name and layer)
};
// Every style's everyday pieces are free, so nobody has to earn being themselves; the showy ones are goals.
static const Unlock UNLOCKS[] = {
  {EXTRA, 5, 30, "Apron"},         {HAT, 10, 40, "Devil horns"},   {HAT, 11, 40, "Halo"},
  {HAT, 7, 60, "Wizard hat"},      {EXTRA, 10, 60, "Pearls"},      {OUTFIT, 7, 70, "Leather jacket"},
  {EXTRA, 12, 80, "Bat wings"},    {HAT, 14, 90, "Flower crown"},  {EXTRA, 6, 100, "Feather duster"},
  {OUTFIT, 6, 110, "Corset"},      {HAT, 12, 120, "Witch hat"},    {HAT, 13, 140, "Veil"},
  {HAT, 8, 150, "Chef hat"},       {EXTRA, 7, 250, "Medal"},
};
static const int UNLOCK_COUNT = sizeof UNLOCKS / sizeof UNLOCKS[0];

// The EXP a part needs (0: free).
inline uint16_t expNeeded(int layer, int part) {
  for (const Unlock &u : UNLOCKS)
    if (u.layer == layer && u.part == part) return u.exp;
  return 0;
}
inline bool unlocked(int layer, int part, uint32_t exp) { return exp >= expNeeded(layer, part); }

// stepPart, skipping parts not earned yet.
inline void stepUnlocked(Look &l, int layer, int step, uint32_t exp) {
  const int n = partsOf(layer).count;
  for (int i = 0; i < n; i++) {
    stepPart(l, layer, step);
    if (unlocked(layer, l.part[layer], exp)) return;
  }
}

// A look with any part not earned yet put back to the default (a look set over USB or the web app).
inline Look onlyUnlocked(Look l, uint32_t exp) {
  for (int i = 0; i < LAYERS; i++)
    if (!unlocked(i, l.part[i], exp)) l.part[i] = DEFAULT_LOOK.part[i];
  return l;
}

// The unlock reached between two EXP totals (the first of them), or nullptr.
inline const Unlock *newlyUnlocked(uint32_t before, uint32_t after) {
  for (const Unlock &u : UNLOCKS)
    if (before < u.exp && after >= u.exp) return &u;
  return nullptr;
}

// The next unlock above `exp`, or nullptr when everything is earned.
inline const Unlock *nextUnlock(uint32_t exp) {
  const Unlock *best = nullptr;
  for (const Unlock &u : UNLOCKS)
    if (u.exp > exp && (!best || u.exp < best->exp)) best = &u;
  return best;
}

// Level from EXP: 1 at the start, then one every 50.
inline int levelOf(uint32_t exp) { return 1 + exp / 50; }

// ---- Styles: whole outfits in one go (dress-up's Style row, the web app). One part per layer, -1 = keep what's
// worn. Parts not earned yet are left as they are, so a style fills in as its pieces unlock.
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

inline Look applyStyle(Look l, const Style &s, uint32_t exp) {
  for (int i = 0; i < LAYERS; i++)
    if (s.part[i] >= 0 && s.part[i] < partsOf(i).count && unlocked(i, s.part[i], exp)) l.part[i] = s.part[i];
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
