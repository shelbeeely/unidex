#pragma once
// Pronouns (RoomiePet): what a Pet's owner goes by. Shown beside the Pet's name (the Pet screen, Meet, the friends
// list, the household's Roommates, the web app) and, if the Pet wears it, on its pronoun pin. No Arduino code, so
// it can be checked on a computer (tools/pettest).
#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace pronouns {

static const int MAX = 12;  // characters
// The choices on the X4 (dress-up's Pronouns row); the web app also takes anything else of up to MAX characters.
static const char *const PRESETS[] = {"",         "he/him",  "she/her", "they/them", "he/they", "she/they",
                                      "they/he",  "they/she", "any",    "it/its",    "xe/xem",  "ze/zir",
                                      "fae/faer", "ask me"};
static const int PRESET_COUNT = sizeof PRESETS / sizeof PRESETS[0];

// The characters pronouns are made of: lowercase letters, '/', '-', a space and an apostrophe.
static const char CHARS[] = "abcdefghijklmnopqrstuvwxyz/- '";

// Pronouns as typed, made fit: lowercased, anything else dropped, no spaces at either end or twice, MAX at most.
inline void clean(const char *in, char *out) {
  int n = 0;
  for (; *in && n < MAX; in++) {
    char c = *in >= 'A' && *in <= 'Z' ? *in - 'A' + 'a' : *in;
    if (!strchr(CHARS, c) || !c) continue;
    if (c == ' ' && (n == 0 || out[n - 1] == ' ')) continue;
    out[n++] = c;
  }
  while (n > 0 && out[n - 1] == ' ') n--;
  out[n] = 0;
}

// Pronouns in two numbers (the household's member card carries them in b and c): 5 bits a character, 6 to a
// number, 0 for none (CHARS index + 1 otherwise).
inline void pack(const char *p, uint32_t &b, uint32_t &c) {
  uint32_t v[2] = {0, 0};
  for (int i = 0; i < MAX && p[i]; i++) {
    const char *at = strchr(CHARS, p[i]);
    if (!at || !p[i]) break;
    v[i / 6] |= (uint32_t)(at - CHARS + 1) << (5 * (i % 6));
  }
  b = v[0], c = v[1];
}
inline void unpack(uint32_t b, uint32_t c, char *out) {
  const uint32_t v[2] = {b, c};
  int n = 0;
  for (int i = 0; i < MAX; i++) {
    const uint32_t k = v[i / 6] >> (5 * (i % 6)) & 31;
    if (!k || k > sizeof CHARS - 1) break;
    out[n++] = CHARS[k - 1];
  }
  out[n] = 0;
}

// ---- The pin: a little badge on the Pet's chest with the pronouns in 3x5 capitals, drawn at twice the Pet's
// resolution (a 64x64 grid over its 32x32), so "THEY/THEM" fits.
static const char *const GLYPHS[] = {  // 5 rows of 3, for each of CHARS in order
  ".#.#.#####.##.#", "##.#.###.#.###.", ".###..#..#...##", "##.#.##.##.###.", "####..##.#..###", "####..##.#..#..",
  ".###..#.##.#.##", "#.##.#####.##.#", "###.#..#..#.###", "..#..#..##.#.#.", "#.##.###.#.##.#", "#..#..#..#..###",
  "#.########.##.#", "##.#.##.##.##.#", ".#.#.##.##.#.#.", "##.#.###.#..#..", ".#.#.##.###..##", "##.#.###.#.##.#",
  ".###...#...###.", "###.#..#..#..#.", "#.##.##.##.####", "#.##.##.##.#.#.", "#.##.########.#", "#.##.#.#.#.##.#",
  "#.##.#.#..#..#.", "###..#.#.#..###", "..#..#.#.#..#..", "......###......", "...............", ".#..#..........",
};

inline const char *glyph(char c) {
  if (c >= 'A' && c <= 'Z') c = c - 'A' + 'a';
  const char *at = c ? strchr(CHARS, c) : nullptr;
  return GLYPHS[at ? at - CHARS : 28];  // anything else: a space
}

static const int PIN_TEXT_MAX = 15;  // letters that fit across the pin's 64 columns

// The pin's text: the pronouns in capitals, or the part before the first '/' when they're too long to fit.
inline void pinText(const char *p, char *out) {
  int n = 0;
  const int len = (int)strlen(p);
  const int cut = len <= PIN_TEXT_MAX ? len : (int)(strchr(p, '/') ? strchr(p, '/') - p : PIN_TEXT_MAX);
  for (; n < cut && n < PIN_TEXT_MAX; n++) out[n] = p[n] >= 'a' && p[n] <= 'z' ? p[n] - 'a' + 'A' : p[n];
  out[n] = 0;
}

// Draws the pin: pixel(x, y, ink) for each of its pixels on the 64x64 grid (x, y from 0 to 63), ink for its edge and
// letters, paper for the rest. Nothing for no pronouns.
template <typename Pixel> void drawPin(const char *p, Pixel pixel) {
  char text[PIN_TEXT_MAX + 1];
  pinText(p, text);
  const int n = (int)strlen(text);
  if (!n) return;
  const int w = n * 4 - 1 + 6, h = 5 + 6, x0 = (64 - w) / 2, y0 = 49;
  for (int y = 0; y < h; y++)
    for (int x = 0; x < w; x++) {
      const bool corner = (x == 0 || x == w - 1) && (y == 0 || y == h - 1);
      if (corner) continue;
      const bool edge = x == 0 || y == 0 || x == w - 1 || y == h - 1;
      bool ink = edge;
      const int tx = x - 3, ty = y - 3;
      if (!edge && tx >= 0 && ty >= 0 && ty < 5 && tx < n * 4 - 1 && tx % 4 != 3) ink = glyph(text[tx / 4])[ty * 3 + tx % 4] == '#';
      pixel(x0 + x, y0 + y, ink);
    }
}

}  // namespace pronouns
