// Checks Pet's parts and saved look on a computer (no board needed), as the X4 (RoomiePet) builds them;
// pet_waveshare_test.cpp runs the same checks as the Waveshare board builds them:
//   c++ -std=c++17 -O2 -DUNIDEX_X4=1 -I src/apps/pet tools/pettest/pet_test.cpp -o /tmp/pet_test && /tmp/pet_test
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
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
    fits &= lp.count >= 1 && lp.count <= 1 << LAYER_BITS[layer];
    for (int i = 0; i < lp.count; i++)
      if (!partOk(lp.parts[i])) {
        printf("       bad part: %s %d (%s)\n", LAYER_NAMES[layer], i, lp.parts[i].name ? lp.parts[i].name : "?");
        partsOk = false;
      }
  }
  check(partsOk && partOk(EYES_CLOSED), "every part: a name of 1-10 characters, 32 rows of 32 '#', 'o' or '.'");
  check(fits, "every layer's parts fit in its bits");
  int bits = 0;
  for (int layer = 0; layer < LAYERS; layer++) bits += LAYER_BITS[layer];
  check(bits <= 32, "a look fits in 32 bits");
  bool waveshareSame = true;
  for (int layer = 0; layer < LAYERS; layer++)
    waveshareSame &= partsOfAll(layer).count >= WAVESHARE_PARTS[layer] &&
                     partsOf(layer).count == (UNIDEX_X4 ? partsOfAll(layer).count : WAVESHARE_PARTS[layer]);
  check(waveshareSame, UNIDEX_X4 ? "the X4 has every part" : "the Waveshare board keeps only the parts it shipped with");
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
    fellBack &= bad.part[layer] == (partsOf(layer).count == 1 << LAYER_BITS[layer] ? partsOf(layer).count - 1
                                                                                   : DEFAULT_LOOK.part[layer]);
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

  // Gifts: drops after chores.
  bool dropsOk = true;
  for (const Drop &d : DROPS) dropsOk &= d.part < partsOf(d.layer).count && d.rarity != STARTER && dropOf(d.layer, d.part) == &d;
  check(dropsOk, "every gift is a real part, listed once, with a rarity");
  const Owned none = {};
  check(owns(none, BODY, 3) && strcmp(BODIES[3].name, "Bunny") == 0 && owns(none, HAT, 6) &&
            strcmp(HATS[6].name, "Cap") == 0 && strcmp(EXTRAS[3].name, "Bow tie") == 0 && owns(none, EXTRA, 3),
        "the bunny lad (Bunny, Cap, Bow tie) is a starter");
  check(!owns(none, HAT, 7) && giftsOwned(none) == 0, "a new Pet has no gifts");
  Look s0 = DEFAULT_LOOK;
  s0.part[HAT] = 6;  // Cap
  stepOwned(s0, HAT, 1, none);
  check(s0.part[HAT] == 9 && strcmp(HATS[9].name, "Cat ears") == 0,
        "stepping on from the Cap with no gifts skips them, to the cat ears");
  Owned wiz = {};
  give(wiz, *dropOf(HAT, 7));
  s0.part[HAT] = 6;
  stepOwned(s0, HAT, 1, wiz);
  check(s0.part[HAT] == 7 && giftsOwned(wiz) == 1, "given the wizard hat, it comes next");
  Look worn = DEFAULT_LOOK;
  worn.part[HAT] = 8, worn.part[EXTRA] = 7, worn.part[BODY] = 3;
  const Look kept = onlyOwned(worn, none);
  check(kept.part[HAT] == DEFAULT_LOOK.part[HAT] && kept.part[EXTRA] == DEFAULT_LOOK.part[EXTRA] && kept.part[BODY] == 3,
        "a look with gifts not found keeps only what's owned");

  ChoreLuck plain = {false, false, false, false, 0, 0}, good = {true, true, true, true, 9, 0};
  check(dropChance(plain) == 20 && dropChance(good) == 80, "a 20% chance, up to 80% for a chore done well");
  ChoreLuck dry = plain;
  dry.dry = PITY - 1;
  check(dropChance(dry) == 100 && rollDrop(dry, none, 99, 50, 0), "the 5th chore without a gift always brings one");
  check(!rollDrop(plain, none, 20, 0, 0) && rollDrop(plain, none, 19, 99, 0), "the chance is the first roll");
  check(rollRarity(plain, 0) == LEGENDARY && rollRarity(plain, 2) == LEGENDARY && rollRarity(plain, 3) == RARE &&
            rollRarity(plain, 24) == RARE && rollRarity(plain, 25) == UNCOMMON,
        "3% legendary, 22% rare, the rest uncommon");
  check(rollRarity(good, 8) == LEGENDARY && rollRarity(good, 43) == RARE && rollRarity(good, 44) == UNCOMMON, "helping out and streaks raise the odds");
  // Roll many: never a duplicate, and in the end everything, then nothing.
  Owned all = {};
  int rolls = 0, legendaries = 0;
  uint32_t lcg = 12345;
  auto rnd = [&] { return lcg = lcg * 1103515245 + 12345, lcg >> 8; };
  bool noDupes = true;
  for (; rolls < 2000 && giftsOwned(all) < DROP_COUNT; rolls++) {
    ChoreLuck c = plain;
    c.dry = PITY - 1;  // always a gift
    if (const Drop *d = rollDrop(c, all, rnd(), rnd(), rnd())) {
      noDupes &= !owns(all, d->layer, d->part);
      legendaries += d->rarity == LEGENDARY;
      give(all, *d);
    }
  }
  check(noDupes && giftsOwned(all) == DROP_COUNT && rolls == DROP_COUNT, "a sure gift each time: no duplicates, all found");
  check(rollDrop(dry, all, 0, 0, 0) == nullptr, "nothing drops once a Pet has everything");
  Owned onlyLegendaryLeft = all;
  onlyLegendaryLeft.layer[HAT] &= ~(1u << 13);
  const Drop *last = rollDrop(dry, onlyLegendaryLeft, 0, 99, 0);
  check(last && last->layer == HAT && last->part == 13, "an uncommon roll with only a legendary left gives that");

  bool dropsInRange = true;
  for (const Drop &d : DROPS) dropsInRange &= d.part < partsOf(d.layer).count;
  check(dropsInRange, "every gift exists in this build");
  bool randomOk = true;
  uint32_t r2 = 7;
  for (int i = 0; i < 500; i++) {
    const Look r = randomOwned([&] { return r2 = r2 * 1664525u + 1013904223u; }, none);
    for (int layer = 0; layer < LAYERS; layer++) randomOk &= owns(none, layer, r.part[layer]);
  }
  check(randomOk, "a random look uses only what the Pet owns");

  // Pronouns and the pin.
  char pr[pronouns::MAX + 1];
  pronouns::clean("  She/Her ", pr);
  check(strcmp(pr, "she/her") == 0, "pronouns are lowercased and trimmed");
  pronouns::clean("they/them!!1  and more", pr);
  check(strcmp(pr, "they/them an") == 0, "anything else is dropped, and it stops at 12 characters");
  bool packsOk = true;
  for (const char *p : pronouns::PRESETS) {
    uint32_t b, c;
    pronouns::pack(p, b, c);
    char back[pronouns::MAX + 1];
    pronouns::unpack(b, c, back);
    packsOk &= strcmp(back, p) == 0;
  }
  uint32_t b12, c12;
  pronouns::pack("abcdefghijkl", b12, c12);
  pronouns::unpack(b12, c12, pr);
  check(packsOk && strcmp(pr, "abcdefghijkl") == 0, "pronouns survive packing into two numbers (the member card)");
  bool glyphsOk = true;
  for (const char *g : pronouns::GLYPHS) glyphsOk &= strlen(g) == 15;
  check(glyphsOk && sizeof pronouns::GLYPHS / sizeof *pronouns::GLYPHS == sizeof pronouns::CHARS - 1,
        "a 3x5 letter for every pronoun character");
  pronouns::pinText("they/them", pr);
  check(strcmp(pr, "THEY/THEM") == 0, "the pin says THEY/THEM");
  int minX = 99, maxX = -1, minY = 99, maxY = -1, inks = 0;
  pronouns::drawPin("they/them", [&](int x, int y, bool ink) {
    minX = x < minX ? x : minX, maxX = x > maxX ? x : maxX, minY = y < minY ? y : minY, maxY = y > maxY ? y : maxY;
    inks += ink;
  });
  check(minX >= 0 && maxX < 64 && minY >= 48 && maxY < 64 && inks > 40, "the pin fits on the chest of the 64x64 grid");
  int any = 0;
  pronouns::drawPin("", [&](int, int, bool) { any++; });
  check(any == 0, "no pronouns, no pin");
  Look pinned = DEFAULT_LOOK;
  pinned.pin = true;
  check(unpack(pack(pinned)).pin == (bool)UNIDEX_X4 && !unpack(pack(DEFAULT_LOOK)).pin,
        UNIDEX_X4 ? "the pin is saved with the look (the top bit)" : "the Waveshare board has no pin");
  // What it holds.
  bool heldOk = partsOf(HELD).count == (UNIDEX_X4 ? 16 : 1) && blank(partsOf(HELD).parts[0]);
  for (int i = 0; i < partsOf(HELD).count; i++) heldOk &= partOk(partsOf(HELD).parts[i]);
  check(heldOk, UNIDEX_X4 ? "16 things to hold (the first is nothing), each a proper part" : "nothing to hold on the Waveshare board");
  check(unpack(pack(DEFAULT_LOOK)).part[HELD] == 0, "what it holds isn't in the saved look");
#if UNIDEX_X4
  check(strcmp(HELD_PARTS[1].name, "Broom") == 0 && strcmp(HELD_PARTS[9].name, "Wand") == 0 &&
            strcmp(HELD_PARTS[10].name, "Sword") == 0 && strcmp(HELD_PARTS[11].name, "Gem") == 0 &&
            strcmp(HELD_PARTS[14].name, "Lantern") == 0 && rarityOf(HELD, 11) == LEGENDARY && rarityOf(HELD, 1) == STARTER,
        "a broom to start with; the wand, sword, gem and lantern are gifts");
  Look holding = DEFAULT_LOOK;
  holding.part[HELD] = 1;
  uint8_t bare[SIZE][SIZE], withBroom[SIZE][SIZE];
  render(DEFAULT_LOOK, false, bare);
  render(holding, false, withBroom);
  check(memcmp(bare, withBroom, sizeof bare) != 0, "a held broom is drawn");
  Look tooFar = DEFAULT_LOOK;
  tooFar.part[HELD] = 11;  // the gem, a gift
  check(onlyOwned(tooFar, none).part[HELD] == 0 && onlyOwned(tooFar, all).part[HELD] == 11, "a gift is held only once found");
#endif

  // Pride flags.
  bool flagsOk = flags::COUNT <= 128 && flags::FLAGS[0].count == 0;
  for (int f = 1; f < flags::COUNT; f++)
    flagsOk &= strlen(flags::FLAGS[f].name) <= 12 && flags::FLAGS[f].count >= 1 && flags::FLAGS[f].count <= 7;
  check(flagsOk, "every flag: a short name and 1 to 7 colours; they fit in the flag byte");
  check(strcmp(flags::FLAGS[3].name, "Trans") == 0 && flags::colourAt(3, 0, 0, 20, 10) == 0x5BCEFA &&
            flags::colourAt(3, 0, 5, 20, 10) == 0xFFFFFF,
        "the trans flag: light blue at the top, white in the middle");
  check(flags::colourAt(2, 0, 5, 26, 12) == 0xFFFFFF && flags::colourAt(2, 25, 0, 26, 12) == 0xE40303,
        "the progress flag: the chevron's white tip at the left, red at the top right");
  check(flags::colourAt(15, 0, 0, 26, 14) == 0xFFD800 && flags::colourAt(15, 13, 2, 26, 14) == flags::INTERSEX_RING,
        "the intersex flag: yellow with a purple ring");
  check(flags::indexOf(3 | flags::ON_PIN) == 3 && flags::indexOf(127) == 0, "a flag byte: the index, the pin bit");
  int fMinX = 99, fMaxX = -1, fMinY = 99, fMaxY = -1, fInk = 0;
  flags::drawPin(1, [&](int x, int y, bool ink) {
    fMinX = x < fMinX ? x : fMinX, fMaxX = x > fMaxX ? x : fMaxX, fMinY = y < fMinY ? y : fMinY, fMaxY = y > fMaxY ? y : fMaxY;
    fInk += ink;
  });
  check(fMinX >= 0 && fMaxX < 64 && fMinY >= 48 && fMaxY < 64 && fInk > 40, "the flag pin fits on the chest");
  int clashes = 0;
  for (int f = 1; f < flags::COUNT; f++)  // neighbouring stripes look different, or the flag is a blur
    if (flags::FLAGS[f].shape == flags::STRIPES)
      for (int i = 1; i < flags::FLAGS[f].count; i++)
        if (flags::FLAGS[f].colour[i] != flags::FLAGS[f].colour[i - 1] &&
            flags::stripeTexture(f, i) == flags::stripeTexture(f, i - 1))
          clashes++, printf("       %s: stripes %d and %d look the same in black and white\n", flags::FLAGS[f].name, i - 1, i);
  check(clashes == 0, "neighbouring stripes look different in black and white");

#if UNIDEX_X4
  check(strcmp(OUTFITS[22].name, "Suit") == 0 && strcmp(HATS[23].name, "Tiara") == 0 && strcmp(EXTRAS[30].name, "Carrot") == 0 &&
            strcmp(OUTFITS[13].name, "Maid dress") == 0 && strcmp(EXTRAS[25].name, "Spider") == 0,
        "the X4's gifts point at the right parts");
  check(strcmp(LAYER_NAMES[EXTRA], "Accessory") == 0, "the X4 calls extras accessories");
#endif
  check(DRAW_ORDER[1] == OUTFIT && DRAW_ORDER[4] == HAIR && DRAW_ORDER[5] == HAT, "clothes under the face, hair under hats");

  check(levelOf(0) == 1 && levelOf(49) == 1 && levelOf(50) == 2, "levels every 50 EXP");

  printf(failures ? "\n%d check(s) failed\n" : "\nall checks passed\n", failures);
  return failures ? 1 : 0;
}
