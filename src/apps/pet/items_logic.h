#pragma once
// The catalog (RoomiePet): things a Pet holds and clothes it wears, beyond the wardrobe built into parts.h. The firmware
// brings the ones in catalog/items.txt (built by tools/items/build.py into items.h); an X4 can take more from its web
// app, without new firmware, and passes them on to every X4 it syncs with or meets, one radio packet each. An item is
// known by an id made from all of it, so the same item gets the same id wherever it's added. No Arduino code, so it can
// be checked on a computer (tools/itemtest).
//
// Each item has a kind (where it goes) and a shape:
//   ICON   16 x 16, one bit a pixel (32 bytes), like Kenney's 1-Bit Pack: held in the hand (lower right), worn on the
//          head (a hat: its lowest ink sits on the top of the head) or at the neck (an accessory: its highest ink just
//          under the chin), with a paper edge so it reads against the Pet.
//   PIECE  a piece of the Pet's own 32 x 32 grid, like the wardrobe's parts: rows `top` to `top + rows - 1`, two bits a
//          pixel (0 see-through, 1 ink, 2 paper), 8 bytes a row. A hat, an accessory, hair or an outfit drawn to fit.
// A catalog hat, accessory, hair or outfit is worn instead of the wardrobe's part for that layer.
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "pet_logic.h"

namespace items {

static const int NAME = 12;  // characters
static const int SIDE = 16;  // an icon's pixels
static const int ICON_BYTES = 32;
static const int MAX_ROWS = 27;  // a piece's rows at most (so an item fits one radio packet)
static const int DATA = MAX_ROWS * 8;
static const int MAX_ADDED = 96;

enum Kind : uint8_t { HAND, HAT, ACCESSORY, HAIR, OUTFIT, KINDS };
static const char *const KIND_NAMES[KINDS] = {"hand", "hat", "accessory", "hair", "outfit"};
enum Shape : uint8_t { ICON, PIECE };

// The wardrobe layer a kind of clothes takes the place of (HAND: none).
inline int layerOf(uint8_t kind) {
  return kind == HAT ? pet::HAT : kind == ACCESSORY ? pet::EXTRA : kind == HAIR ? pet::HAIR : kind == OUTFIT ? pet::OUTFIT : -1;
}

struct Item {
  uint32_t id;  // 24 bits, never 0 (0 is "nothing")
  char name[NAME + 1];
  uint8_t kind, shape, top, rows;  // top and rows: a PIECE's (an ICON's are 0 and 16)
  uint8_t data[DATA];
};

inline int dataLen(const Item &it) { return it.shape == ICON ? ICON_BYTES : it.rows * 8; }

// A proper item: a kind and shape that go together, and a piece within the grid.
inline bool shapeOk(const Item &it) {
  if (it.kind >= KINDS) return false;
  if (it.shape == ICON) return it.kind == HAND || it.kind == HAT || it.kind == ACCESSORY;
  return it.shape == PIECE && it.kind != HAND && it.rows >= 1 && it.rows <= MAX_ROWS && it.top + it.rows <= 32;
}

inline bool iconInk(const Item &it, int x, int y) { return it.data[y * 2 + (x >> 3)] >> (7 - (x & 7)) & 1; }
// A piece's pixel at (x, y) of the Pet's grid: 0 see-through, 1 ink, 2 paper.
inline int pieceAt(const Item &it, int x, int y) {
  if (y < it.top || y >= it.top + it.rows) return 0;
  return it.data[(y - it.top) * 8 + (x >> 2)] >> (6 - 2 * (x & 3)) & 3;
}

inline uint32_t crc32(const uint8_t *p, size_t n, uint32_t c = 0) {
  c = ~c;
  while (n--) {
    c ^= *p++;
    for (int k = 0; k < 8; k++) c = c >> 1 ^ (0xEDB88320u & (0u - (c & 1)));
  }
  return ~c;
}

// An item's id: the CRC-32 of its name, kind, shape, top, rows and picture, kept to 24 bits.
inline uint32_t idOf(const Item &it) {
  const uint8_t head[4] = {it.kind, it.shape, it.top, it.rows};
  uint32_t c = crc32((const uint8_t *)it.name, strnlen(it.name, NAME));
  c = crc32(head, 4, c);
  c = crc32(it.data, dataLen(it), c);
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

inline bool valid(const Item &it) { return nameOk(it.name) && shapeOk(it) && it.id == idOf(it); }

inline Item makeIcon(const char *name, uint8_t kind, const uint8_t bits[ICON_BYTES]) {
  Item it = {};
  strncpy(it.name, name, NAME);
  it.kind = kind, it.shape = ICON, it.top = 0, it.rows = SIDE;
  memcpy(it.data, bits, ICON_BYTES);
  it.id = idOf(it);
  return it;
}
// A piece from rows `top` to `top + rows - 1` (data: 8 bytes a row, as pieceAt reads them).
inline Item makePiece(const char *name, uint8_t kind, uint8_t top, uint8_t rows, const uint8_t *data) {
  Item it = {};
  strncpy(it.name, name, NAME);
  it.kind = kind, it.shape = PIECE, it.top = top, it.rows = rows;
  memcpy(it.data, data, rows <= MAX_ROWS ? rows * 8 : DATA);
  it.id = idOf(it);
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
  // A new item: true if it was added. False if it's known already, isn't a proper item (its id must match the rest
  // of it), or the catalog is full.
  bool add(const Item &it) {
    if (!valid(it) || indexOf(it.id) >= 0 || addedCount >= MAX_ADDED) return false;
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

// ---- The radio. "RPI", version, type 1, id u32, kind, shape, top, rows, name length u8 + 12 bytes, then the picture
// (32 bytes for an icon, 8 a row for a piece): one item to a packet, at most 242 bytes. While a device hears another
// whose catalog digest differs from its own (both say theirs in their HELLO, in Meet and in the household's Sync), it
// sends its items one a beat, the ones it was given first; each device keeps the ones it didn't have. Ids are checked
// against the rest of the item, so a damaged packet can't add a broken one.
static const uint8_t MAGIC[3] = {'R', 'P', 'I'};
static const uint8_t VERSION = 2, ITEM = 1;
static const size_t HEADER = 5 + 4 + 4 + 1 + NAME, PACKET_MAX = HEADER + DATA;

inline size_t encode(const Item &it, uint8_t *out) {
  memcpy(out, MAGIC, 3), out[3] = VERSION, out[4] = ITEM;
  for (int i = 0; i < 4; i++) out[5 + i] = it.id >> (8 * i);
  out[9] = it.kind, out[10] = it.shape, out[11] = it.top, out[12] = it.rows;
  const size_t n = strnlen(it.name, NAME);
  out[13] = n;
  memset(out + 14, 0, NAME);
  memcpy(out + 14, it.name, n);
  memcpy(out + HEADER, it.data, dataLen(it));
  return HEADER + dataLen(it);
}
inline bool isItemPacket(const uint8_t *p, size_t len) {
  return len >= HEADER && len <= PACKET_MAX && memcmp(p, MAGIC, 3) == 0 && p[3] == VERSION && p[4] == ITEM;
}
inline bool decode(const uint8_t *p, size_t len, Item &it) {
  if (!isItemPacket(p, len) || p[13] < 1 || p[13] > NAME) return false;
  it = {};
  it.id = p[5] | p[6] << 8 | p[7] << 16 | (uint32_t)p[8] << 24;
  it.kind = p[9], it.shape = p[10], it.top = p[11], it.rows = p[12];
  memcpy(it.name, p + 14, p[13]);
  if (!shapeOk(it) || len != HEADER + (size_t)dataLen(it)) return false;
  memcpy(it.data, p + HEADER, dataLen(it));
  return valid(it);
}

// Which item to send next, going round: the ones it was given first, then the firmware's (which another device has
// unless its firmware is older). `turn` counts up by one each beat.
inline int sendOrder(const Catalog &c, uint32_t turn) {
  if (!c.count()) return -1;
  const int k = (int)(turn % (uint32_t)c.count());
  return k < c.addedCount ? c.builtinCount + k : k - c.addedCount;
}

// ---- Drawing, on the Pet's 32 x 32 grid: pixel(x, y, ink) as pet_logic.h's drawLook.

// Where an icon goes: its top left on the grid. In the hand: the lower right. A hat: centred, its lowest ink on the
// top of the head (row 9). An accessory: centred, its highest ink just under the chin (row 23).
inline void iconPlace(const Item &it, int &x0, int &y0) {
  int top = SIDE, bottom = -1;
  for (int y = 0; y < SIDE; y++)
    for (int x = 0; x < SIDE; x++)
      if (iconInk(it, x, y)) top = y < top ? y : top, bottom = y;
  if (bottom < 0) top = bottom = 0;
  x0 = it.kind == HAND ? 16 : 8;
  y0 = it.kind == HAND ? 16 : it.kind == HAT ? 9 - bottom : 23 - top;
}

template <typename Pixel> void drawItem(const Item &it, Pixel pixel) {
  if (it.shape == PIECE) {
    for (int y = it.top; y < it.top + it.rows && y < 32; y++)
      for (int x = 0; x < 32; x++) {
        const int c = pieceAt(it, x, y);
        if (c == 1) pixel(x, y, true);
        else if (c == 2) pixel(x, y, false);
      }
    return;
  }
  int X0, Y0;
  iconPlace(it, X0, Y0);
  for (int y = -1; y <= SIDE; y++)
    for (int x = -1; x <= SIDE; x++) {
      const int px = X0 + x, py = Y0 + y;
      if (px < 0 || py < 0 || px >= 32 || py >= 32) continue;
      const bool here = x >= 0 && y >= 0 && x < SIDE && y < SIDE && iconInk(it, x, y);
      bool near = false;
      for (int dy = -1; dy <= 1 && !near; dy++)
        for (int dx = -1; dx <= 1 && !near; dx++) {
          const int nx = x + dx, ny = y + dy;
          near = nx >= 0 && ny >= 0 && nx < SIDE && ny < SIDE && iconInk(it, nx, ny);
        }
      if (here) pixel(px, py, true);
      else if (near) pixel(px, py, false);
    }
}

// What a Pet wears from the catalog, and holds: an item id for each kind (0: the wardrobe's part, or nothing in hand).
struct Worn {
  uint32_t id[KINDS];
};

// The whole Pet: the look, with catalog clothes in place of the wardrobe's parts for their layers, then what it holds.
// find(id) gives the item, or nullptr for one this device doesn't have (the wardrobe's part shows instead).
template <typename Find, typename Pixel>
void drawPet(const pet::Look &look, const Worn &worn, bool eyesClosed, Find find, Pixel pixel) {
  const Item *over[pet::LAYERS] = {};
  for (int k = HAT; k < KINDS; k++) {
    const Item *it = worn.id[k] ? find(worn.id[k]) : nullptr;
    if (it && it->kind == k) over[layerOf(k)] = it;
  }
  pet::drawLook(look, eyesClosed, pixel, [&](int layer) {
    if (!over[layer]) return false;
    drawItem(*over[layer], pixel);
    return true;
  });
  const Item *held = worn.id[HAND] ? find(worn.id[HAND]) : nullptr;
  if (held && held->kind == HAND) drawItem(*held, pixel);
}

}  // namespace items
