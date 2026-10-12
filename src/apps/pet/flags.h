#pragma once
// Pride flags (RoomiePet): one a Pet's owner can show beside its name (the Pet screen, Meet's name tags, the friends
// list, the household's Roommates) and on its pin instead of the pronouns. Drawn from their colours: in colour in the
// browser (site/x4/pin.js), and on the e-ink in black and white, each colour a texture as dark as it is. No
// Arduino code, so it can be checked on a computer (tools/pettest).
#include <stdint.h>

namespace flags {

enum Shape : uint8_t { STRIPES, PROGRESS, INTERSEX };

struct Flag {
  const char *name;  // at most 12 characters
  Shape shape;
  uint8_t count;           // stripes
  uint32_t colour[7];      // top to bottom, 0xRRGGBB
};

// 0 is no flag. Keep the order: a Pet's choice is saved, and sent to other devices, as its index.
static const Flag FLAGS[] = {
  {"None", STRIPES, 0, {}},
  {"Rainbow", STRIPES, 6, {0xE40303, 0xFF8C00, 0xFFED00, 0x008026, 0x004DFF, 0x750787}},
  {"Progress", PROGRESS, 6, {0xE40303, 0xFF8C00, 0xFFED00, 0x008026, 0x004DFF, 0x750787}},
  {"Trans", STRIPES, 5, {0x5BCEFA, 0xF5A9B8, 0xFFFFFF, 0xF5A9B8, 0x5BCEFA}},
  {"Nonbinary", STRIPES, 4, {0xFCF434, 0xFFFFFF, 0x9C59D1, 0x2C2C2C}},
  {"Bi", STRIPES, 5, {0xD60270, 0xD60270, 0x9B4F96, 0x0038A8, 0x0038A8}},
  {"Pan", STRIPES, 3, {0xFF218C, 0xFFD800, 0x21B1FF}},
  {"Lesbian", STRIPES, 5, {0xD52D00, 0xFF9A56, 0xFFFFFF, 0xD362A4, 0xA30262}},
  {"Gay", STRIPES, 7, {0x078D70, 0x26CEAA, 0x98E8C1, 0xFFFFFF, 0x7BADE2, 0x5049CC, 0x3D1A78}},
  {"Ace", STRIPES, 4, {0x000000, 0xA3A3A3, 0xFFFFFF, 0x800080}},
  {"Aro", STRIPES, 5, {0x3DA542, 0xA7D379, 0xFFFFFF, 0xA9A9A9, 0x000000}},
  {"Aroace", STRIPES, 5, {0xE28C00, 0xECCD00, 0xFFFFFF, 0x62AEDC, 0x203856}},
  {"Genderfluid", STRIPES, 5, {0xFF76A4, 0xFFFFFF, 0xC011D7, 0x000000, 0x2F3CBE}},
  {"Genderqueer", STRIPES, 3, {0xB57EDC, 0xFFFFFF, 0x4A8123}},
  {"Agender", STRIPES, 7, {0x000000, 0xBCC4C7, 0xFFFFFF, 0xB9F484, 0xFFFFFF, 0xBCC4C7, 0x000000}},
  {"Intersex", INTERSEX, 1, {0xFFD800}},
};
static const int COUNT = sizeof FLAGS / sizeof FLAGS[0];

// A Pet's flag byte (saved as pet_flag, sent in Meet's HELLO, on the household member card): the flag's index in
// the low 7 bits, and the top bit when its pin shows the flag rather than the pronouns.
static const uint8_t ON_PIN = 0x80;
inline int indexOf(uint8_t b) { return (b & 0x7F) < COUNT ? b & 0x7F : 0; }

static const uint32_t PROGRESS_CHEVRON[5] = {0xFFFFFF, 0xF5A9B8, 0x5BCEFA, 0x613915, 0x000000};  // tip outwards
static const uint32_t INTERSEX_RING = 0x7902AA;

// The colour at (x, y) of a flag drawn w x h.
inline uint32_t colourAt(int f, int x, int y, int w, int h) {
  const Flag &fl = FLAGS[f];
  if (fl.shape == INTERSEX) {
    // A ring: centre at the middle, its radius a third of the height, about a sixth of the height thick.
    const int dx2 = (2 * x + 1 - w), dy2 = (2 * y + 1 - h);  // twice the distance from the centre
    const int d2 = dx2 * dx2 + dy2 * dy2, r = h * 2 / 3, t = h / 6 > 1 ? h / 6 : 1;
    const int in = (r - t) * (r - t), out = (r + t) * (r + t);
    return d2 >= in && d2 <= out ? INTERSEX_RING : fl.colour[0];
  }
  if (fl.shape == PROGRESS) {
    // The chevron from the left edge: bands of PROGRESS_CHEVRON, the tip (white) at the middle of the left edge.
    const int fromMiddle = 2 * y + 1 - h < 0 ? h - 1 - 2 * y : 2 * y + 1 - h;  // twice the distance from the middle
    const int v = (h - fromMiddle) - 2 * x;                                     // > 0 inside the chevron
    if (v > 0) {
      const int band = 4 - v * 5 / (h + 1);  // the tip, where v is largest, is the innermost band
      return PROGRESS_CHEVRON[band < 0 ? 0 : band];
    }
  }
  const int i = y * fl.count / h;
  return fl.colour[i < fl.count ? i : fl.count - 1];
}

// How dark a colour looks, 0 (white) to 255 (black).
inline int darkness(uint32_t c) {
  const int r = c >> 16 & 255, g = c >> 8 & 255, b = c & 255;
  return 255 - (r * 299 + g * 587 + b * 114) / 1000;
}

// On e-ink each colour becomes a texture as dark as it is: white, sparse dots, thin vertical lines, a checker,
// diagonal lines, a dense grid, solid. Two neighbouring stripes that would get the same texture are told apart: the
// darker of them takes the next texture darker (or the lighter the next lighter), so stripes never blur together.
static const uint8_t TEXTURES = 7;
static const uint8_t TEXTURE_DARKNESS[TEXTURES] = {0, 32, 64, 128, 176, 200, 255};

inline int textureOf(uint32_t c) {  // the texture nearest the colour's darkness
  const int d = darkness(c);
  int best = 0;
  for (int t = 1; t < TEXTURES; t++)
    if ((d > TEXTURE_DARKNESS[t] ? d - TEXTURE_DARKNESS[t] : TEXTURE_DARKNESS[t] - d) <
        (d > TEXTURE_DARKNESS[best] ? d - TEXTURE_DARKNESS[best] : TEXTURE_DARKNESS[best] - d))
      best = t;
  return best;
}
inline bool textureInk(int t, int x, int y) {
  switch (t) {
    case 0: return false;
    case 1: return (x & 1) == 0 && (y & 1) == 0 && ((x >> 1) + y) % 2 == 0;  // one in eight
    case 2: return (x & 3) == 0;                                             // thin vertical lines
    case 3: return ((x + y) & 1) == 0;                                       // a checker
    case 4: return (x + y) % 4 != 0;                                         // diagonal lines
    case 5: return !((x & 1) == 0 && (y & 1) == 0);                          // three in four
    default: return true;
  }
}
// The texture of stripe i of a striped flag, kept apart from the stripe above it.
inline int stripeTexture(int f, int i) {
  const Flag &fl = FLAGS[f];
  int t = textureOf(fl.colour[0]);
  for (int k = 1; k <= i; k++) {
    int next = textureOf(fl.colour[k]);
    if (next == t && fl.colour[k] != fl.colour[k - 1]) {
      const bool darker = darkness(fl.colour[k]) >= darkness(fl.colour[k - 1]);
      next = darker ? (next < TEXTURES - 1 ? next + 1 : next - 1) : (next > 0 ? next - 1 : next + 1);
    }
    t = next;
  }
  return t;
}
inline bool inkAt(int f, int x, int y, int w, int h) {
  const Flag &fl = FLAGS[f];
  const uint32_t c = colourAt(f, x, y, w, h);
  if (fl.shape == STRIPES || fl.shape == PROGRESS) {
    const int i = y * fl.count / h;
    if (c == fl.colour[i < fl.count ? i : fl.count - 1]) return textureInk(stripeTexture(f, i), x, y);
  }
  if (fl.shape == INTERSEX) return c == INTERSEX_RING;  // a solid ring on plain paper reads best
  return textureInk(textureOf(c), x, y);
}

// Draws a flag w x h with a black edge: pixel(x, y, ink) for every pixel, x from 0 to w - 1, y from 0 to h - 1.
template <typename Pixel> void draw(int f, int w, int h, Pixel pixel) {
  if (f <= 0 || f >= COUNT) return;
  for (int y = 0; y < h; y++)
    for (int x = 0; x < w; x++) {
      const bool edge = x == 0 || y == 0 || x == w - 1 || y == h - 1;
      pixel(x, y, edge || inkAt(f, x - 1, y - 1, w - 2, h - 2));
    }
}

// The flag pin on the Pet: a flag on its chest, on pronouns.h's 64x64 grid (where the pronoun pin goes).
template <typename Pixel> void drawPin(int f, Pixel pixel) {
  const int w = 26, h = 14, x0 = (64 - w) / 2, y0 = 48;
  draw(f, w, h, [&](int x, int y, bool ink) { pixel(x0 + x, y0 + y, ink); });
}

}  // namespace flags
