#pragma once
// Things a Pet holds (RoomiePet): a catalog of 16x16 one-colour sprites. The firmware brings the ones in
// catalog/items.txt (built by tools/items/build.py into items.h); an X4 can take more from its web app, without new
// firmware, and passes them on to every X4 it syncs with or meets, one radio packet each. An item is known by an id
// made from its name and picture, so the same item gets the same id wherever it's added. No Arduino code, so it can
// be checked on a computer (tools/itemtest).
#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace items {

static const int NAME = 12;   // characters
static const int SIDE = 16;   // pixels
static const int BYTES = 32;  // 16 rows of 2 bytes, the leftmost pixel in the top bit
static const int MAX_ADDED = 96;

struct Item {
  uint32_t id;  // 24 bits, never 0 (0 is "nothing")
  char name[NAME + 1];
  uint8_t bits[BYTES];
};

inline bool ink(const Item &it, int x, int y) { return it.bits[y * 2 + (x >> 3)] >> (7 - (x & 7)) & 1; }

inline uint32_t crc32(const uint8_t *p, size_t n, uint32_t c = 0) {
  c = ~c;
  while (n--) {
    c ^= *p++;
    for (int k = 0; k < 8; k++) c = c >> 1 ^ (0xEDB88320u & (0u - (c & 1)));
  }
  return ~c;
}

// An item's id: from its name and picture, 24 bits (it rides beside the pride flag on the household member card).
inline uint32_t idOf(const char *name, const uint8_t bits[BYTES]) {
  const uint32_t c = crc32(bits, BYTES, crc32((const uint8_t *)name, strnlen(name, NAME)));
  return (c & 0xFFFFFF) ? c & 0xFFFFFF : 1;
}

// A name fit for an item: 1 to 12 printable characters.
inline bool nameOk(const char *name) {
  const size_t n = strnlen(name, NAME + 1);
  if (n < 1 || n > (size_t)NAME) return false;
  for (size_t i = 0; i < n; i++)
    if (name[i] < 0x20 || name[i] > 0x7E) return false;
  return true;
}

inline Item make(const char *name, const uint8_t bits[BYTES]) {
  Item it = {};
  strncpy(it.name, name, NAME);
  memcpy(it.bits, bits, BYTES);
  it.id = idOf(it.name, it.bits);
  return it;
}

// The items a device knows: the firmware's (built in) and the ones it was given (added), in that order.
struct Catalog {
  const Item *builtin = nullptr;
  int builtinCount = 0;
  Item added[MAX_ADDED];
  int addedCount = 0;

  int count() const { return builtinCount + addedCount; }
  const Item &at(int i) const { return i < builtinCount ? builtin[i] : added[i - builtinCount]; }
  int indexOf(uint32_t id) const {
    if (!id) return -1;
    for (int i = 0; i < count(); i++)
      if (at(i).id == id) return i;
    return -1;
  }
  const Item *find(uint32_t id) const {
    const int i = indexOf(id);
    return i < 0 ? nullptr : &at(i);
  }
  // A new item: true if it was added. False if it's known already, isn't a proper item (its id must match its
  // name and picture), or the catalog is full.
  bool add(const Item &it) {
    if (!nameOk(it.name) || it.id != idOf(it.name, it.bits) || indexOf(it.id) >= 0 || addedCount >= MAX_ADDED)
      return false;
    added[addedCount++] = it;
    return true;
  }
  bool remove(uint32_t id) {  // an added item only
    for (int i = 0; i < addedCount; i++)
      if (added[i].id == id) {
        memmove(&added[i], &added[i + 1], (addedCount - i - 1) * sizeof(Item));
        addedCount--;
        return true;
      }
    return false;
  }
  // The same for two catalogs holding the same items, in any order: devices compare it to see who's missing what.
  uint32_t digest() const {
    uint32_t d = (uint32_t)count() * 0x9E3779B9u;
    for (int i = 0; i < count(); i++) d += at(i).id * 2654435761u;
    return d ? d : 1;
  }
};

// ---- The radio. "RPI", version, type 1, id u32, name length u8 + up to 12 bytes, picture (32 bytes): one item to a
// packet. While a device hears another whose catalog digest differs from its own (both say theirs in their HELLO,
// in Meet and in the household's Sync), it sends its items one a beat, the ones it was given first; each device keeps
// the ones it didn't have. Ids are checked against name and picture, so a damaged packet can't add a broken item.
static const uint8_t MAGIC[3] = {'R', 'P', 'I'};
static const uint8_t VERSION = 1, ITEM = 1;
static const size_t PACKET = 5 + 4 + 1 + NAME + BYTES;

inline size_t encode(const Item &it, uint8_t *out) {
  memcpy(out, MAGIC, 3), out[3] = VERSION, out[4] = ITEM;
  for (int i = 0; i < 4; i++) out[5 + i] = it.id >> (8 * i);
  const size_t n = strnlen(it.name, NAME);
  out[9] = n;
  memset(out + 10, 0, NAME);
  memcpy(out + 10, it.name, n);
  memcpy(out + 10 + NAME, it.bits, BYTES);
  return PACKET;
}
inline bool isItemPacket(const uint8_t *p, size_t len) {
  return len == PACKET && memcmp(p, MAGIC, 3) == 0 && p[3] == VERSION && p[4] == ITEM;
}
inline bool decode(const uint8_t *p, size_t len, Item &it) {
  if (!isItemPacket(p, len) || p[9] < 1 || p[9] > NAME) return false;
  it = {};
  it.id = p[5] | p[6] << 8 | p[7] << 16 | (uint32_t)p[8] << 24;
  memcpy(it.name, p + 10, p[9]);
  memcpy(it.bits, p + 10 + NAME, BYTES);
  return nameOk(it.name) && it.id == idOf(it.name, it.bits);
}

// Which item to send next, going round: the ones it was given first, then the firmware's (which another device has
// unless its firmware is older). `turn` counts up by one each beat.
inline int sendOrder(const Catalog &c, uint32_t turn) {
  if (!c.count()) return -1;
  const int k = (int)(turn % (uint32_t)c.count());
  return k < c.addedCount ? c.builtinCount + k : k - c.addedCount;
}

// Draws an item in a Pet's hand, over the Pet (and under its pin): on its 32x32 grid at x 16-31, y 16-31, with a one-pixel paper edge
// around the ink so it reads against whatever is under it. pixel(x, y, ink) as pet_logic.h's drawLook.
template <typename Pixel> void drawHeld(const Item &it, Pixel pixel) {
  const int X0 = 16, Y0 = 16;
  for (int y = -1; y <= SIDE; y++)
    for (int x = -1; x <= SIDE; x++) {
      const int px = X0 + x, py = Y0 + y;
      if (px < 0 || py < 0 || px >= 32 || py >= 32) continue;
      const bool here = x >= 0 && y >= 0 && x < SIDE && y < SIDE && ink(it, x, y);
      bool near = false;
      for (int dy = -1; dy <= 1 && !near; dy++)
        for (int dx = -1; dx <= 1 && !near; dx++) {
          const int nx = x + dx, ny = y + dy;
          near = nx >= 0 && ny >= 0 && nx < SIDE && ny < SIDE && ink(it, nx, ny);
        }
      if (here) pixel(px, py, true);
      else if (near) pixel(px, py, false);
    }
}

}  // namespace items
