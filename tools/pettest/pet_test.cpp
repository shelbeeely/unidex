// Checks Pet's parts and saved look on a computer (no board needed):
//   c++ -std=c++17 -O2 -I src/apps/pet tools/pettest/pet_test.cpp -o /tmp/pet_test && /tmp/pet_test
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "pet_logic.h"

using namespace pet;
static int failures;

static void check(bool ok, const char *what) {
  printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
  failures += !ok;
}

static bool partOk(const PetPart &p) {
  if (!p.name || strlen(p.name) == 0 || strlen(p.name) > 10) return false;
  for (int y = 0; y < SIZE; y++) {
    if (!p.rows[y] || strlen(p.rows[y]) != SIZE) return false;
    for (int x = 0; x < SIZE; x++)
      if (!strchr("#o.", p.rows[y][x])) return false;
  }
  return true;
}

static bool blank(const PetPart &p) {
  for (int y = 0; y < SIZE; y++)
    for (int x = 0; x < SIZE; x++)
      if (p.rows[y][x] != '.') return false;
  return true;
}

// The look drawn into a 32x32 grid (1 = ink).
static void render(const Look &l, bool closed, uint8_t out[SIZE][SIZE]) {
  memset(out, 0, SIZE * SIZE);
  drawLook(l, closed, [&](int x, int y, bool ink) { out[y][x] = ink; });
}

int main() {
  bool partsOk = true, fits = true;
  for (int layer = 0; layer < LAYERS; layer++) {
    const LayerParts lp = partsOf(layer);
    fits &= lp.count >= 1 && lp.count <= 16;  // 4 bits a layer
    for (int i = 0; i < lp.count; i++)
      if (!partOk(lp.parts[i])) {
        printf("       bad part: %s %d (%s)\n", LAYER_NAMES[layer], i, lp.parts[i].name ? lp.parts[i].name : "?");
        partsOk = false;
      }
  }
  check(partsOk && partOk(EYES_CLOSED), "every part: a name of 1-10 characters, 32 rows of 32 '#', 'o' or '.'");
  check(fits, "every layer has 1 to 16 parts, so it fits in 4 bits");
  check(blank(HATS[0]) && blank(EXTRAS[0]), "the first hat and extra are 'None' (nothing drawn)");
  check(strcmp(EYES[HAPPY_EYES].name, "Happy") == 0, "HAPPY_EYES points at the Happy eyes");
  check(!blank(BODIES[0]) && !blank(EYES[0]) && !blank(MOUTHS[0]), "the default body, eyes and mouth draw something");

  bool roundTrip = true;
  for (int layer = 0; layer < LAYERS; layer++)
    for (int i = 0; i < partsOf(layer).count; i++) {
      Look l = DEFAULT_LOOK;
      l.part[layer] = i;
      const Look back = unpack(pack(l));
      roundTrip &= memcmp(back.part, l.part, LAYERS) == 0;
    }
  check(roundTrip, "every part survives saving and loading");

  const Look bad = unpack(0xFFFFFFFF);
  bool fellBack = true;
  for (int layer = 0; layer < LAYERS; layer++)
    fellBack &= bad.part[layer] == (partsOf(layer).count == 16 ? 15 : DEFAULT_LOOK.part[layer]);
  check(fellBack, "a damaged saved value falls back to the default part per layer");
  check(memcmp(unpack(pack(DEFAULT_LOOK)).part, DEFAULT_LOOK.part, LAYERS) == 0, "the default look round-trips");

  Look w = DEFAULT_LOOK;
  stepPart(w, HAT, -1);
  const bool wrapBack = w.part[HAT] == partsOf(HAT).count - 1;
  stepPart(w, HAT, 1);
  check(wrapBack && w.part[HAT] == 0, "stepping past either end wraps around");

  uint32_t seed = 12345;
  bool randOk = true;
  for (int i = 0; i < 1000; i++) {
    const Look r = randomLook([&] { return seed = seed * 1664525u + 1013904223u; });
    for (int layer = 0; layer < LAYERS; layer++) randOk &= r.part[layer] < partsOf(layer).count;
  }
  check(randOk, "random looks only use parts that exist");

  // Layers combine: a hat changes the picture, paper ('o') can clear ink below, closed eyes differ from open.
  uint8_t a[SIZE][SIZE], b[SIZE][SIZE];
  Look withHat = DEFAULT_LOOK;
  withHat.part[HAT] = partsOf(HAT).count > 1 ? 1 : 0;
  render(DEFAULT_LOOK, false, a);
  render(withHat, false, b);
  check(memcmp(a, b, sizeof a) != 0, "a hat changes the picture");
  render(DEFAULT_LOOK, true, b);
  check(memcmp(a, b, sizeof a) != 0, "a blink (closed eyes) changes the picture");

  check(stepChar(' ', 1) == 'A' && stepChar('A', -1) == ' ' && stepChar('-', 1) == ' ' && stepChar(' ', -1) == '-',
        "letters cycle both ways and wrap round");
  check(stepChar('!', 1) == 'A', "a character that isn't offered counts as a blank");
  char places[MAX_NAME + 1], name[MAX_NAME + 1];
  placesFromName("Mochi", places);
  check(strlen(places) == MAX_NAME && strncmp(places, "Mochi       ", MAX_NAME) == 0, "a name fills the first places");
  check(!nameDoneAt(places, 0) && !nameDoneAt(places, 4) && nameDoneAt(places, 5),
        "B finishes only on a blank with nothing after it");
  places[2] = ' ';
  nameFromPlaces(places, name);
  check(strcmp(name, "Mo hi") == 0 && !nameDoneAt(places, 2), "a blank in the middle stays, and doesn't finish");
  placesFromName("  Pip  ", places);
  nameFromPlaces(places, name);
  check(strcmp(name, "Pip") == 0, "blanks at either end are dropped");
  placesFromName("ABCDEFGHIJKLMNOP", places);
  check(strlen(places) == MAX_NAME && nameDoneAt(places, MAX_NAME - 1), "at most 12 places; the last one finishes");
  placesFromName("", places);
  nameFromPlaces(places, name);
  check(name[0] == 0 && nameDoneAt(places, 0), "an empty name: B finishes straight away");

  // Earned cosmetics.
  bool unlocksExist = true;
  for (const Unlock &u : UNLOCKS) unlocksExist &= u.part < partsOf(u.layer).count && u.exp > 0;
  check(unlocksExist, "every earnable part exists");
  check(expNeeded(BODY, 3) == 0 && strcmp(BODIES[3].name, "Bunny") == 0 && expNeeded(HAT, 6) == 0 &&
            strcmp(HATS[6].name, "Cap") == 0 && strcmp(EXTRAS[3].name, "Bow tie") == 0 && expNeeded(EXTRA, 3) == 0,
        "the bunny lad (Bunny, Cap, Bow tie) is free");
  check(!unlocked(HAT, 7, 59) && unlocked(HAT, 7, 60), "the wizard hat unlocks at 60 EXP");
  Look s0 = DEFAULT_LOOK;
  s0.part[HAT] = 6;  // Cap
  stepUnlocked(s0, HAT, 1, 0);
  check(s0.part[HAT] == 9 && strcmp(HATS[9].name, "Cat ears") == 0,
        "stepping on from the Cap with no EXP skips the locked hats, to the cat ears");
  s0.part[HAT] = 6;
  stepUnlocked(s0, HAT, 1, 60);
  check(s0.part[HAT] == 7, "with 60 EXP the wizard hat comes next");
  Look worn = DEFAULT_LOOK;
  worn.part[HAT] = 8, worn.part[EXTRA] = 7, worn.part[BODY] = 3;
  const Look kept = onlyUnlocked(worn, 0);
  check(kept.part[HAT] == DEFAULT_LOOK.part[HAT] && kept.part[EXTRA] == DEFAULT_LOOK.part[EXTRA] && kept.part[BODY] == 3,
        "a look with parts not earned keeps only what is");
  check(newlyUnlocked(20, 35) && strcmp(newlyUnlocked(20, 35)->what, "Apron") == 0 && !newlyUnlocked(35, 39),
        "crossing 30 EXP unlocks the apron, once");
  check(nextUnlock(0)->exp == 30 && nextUnlock(250) == nullptr, "the next thing to earn");
  // Styles and the new layers.
  bool stylesOk = true;
  for (const Style &st : STYLES)
    for (int i = 0; i < LAYERS; i++) stylesOk &= st.part[i] < partsOf(i).count;
  check(stylesOk, "every style's parts exist");
  const Look femboy = applyStyle(DEFAULT_LOOK, STYLES[0], 0);
  check(strcmp(HAIRS[femboy.part[HAIR]].name, "Fluffy bob") == 0 && strcmp(OUTFITS[femboy.part[OUTFIT]].name, "Skirt") == 0 &&
            strcmp(HATS[femboy.part[HAT]].name, "Cat ears") == 0,
        "the femboy style: fluffy bob, skirt, cat ears (all free)");
  const Look goth = applyStyle(DEFAULT_LOOK, STYLES[4], 0);
  check(goth.part[OUTFIT] == DEFAULT_LOOK.part[OUTFIT] && strcmp(HAIRS[goth.part[HAIR]].name, "Long") == 0,
        "the goth style without EXP: long hair now, the corset once it's earned");
  check(applyStyle(DEFAULT_LOOK, STYLES[4], 1000).part[OUTFIT] == 6, "and with the EXP, the corset");
  Look old = DEFAULT_LOOK;
  old.part[HAIR] = 5, old.part[OUTFIT] = 9;
  check(unpack(pack(old) & 0xFFFFF).part[HAIR] == 0 && unpack(pack(old)).part[OUTFIT] == 9,
        "a look from before hair and outfits (5 layers) has none; a full one keeps them");
  check(DRAW_ORDER[1] == OUTFIT && DRAW_ORDER[4] == HAIR && DRAW_ORDER[5] == HAT, "clothes under the face, hair under hats");

  check(levelOf(0) == 1 && levelOf(49) == 1 && levelOf(50) == 2, "levels every 50 EXP");

  printf(failures ? "\n%d check(s) failed\n" : "\nall checks passed\n", failures);
  return failures ? 1 : 0;
}
