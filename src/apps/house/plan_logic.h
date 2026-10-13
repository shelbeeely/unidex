#pragma once
// The home plan (RoomiePet): the household's real home, room by room, as roommates draw it in the web app. Each room
// has a place on its floor's tile grid, doors, furniture and the chores done in it. The virtual household
// (world_logic.h) is built on it: mess, clutter and the Pets' day all happen in these rooms. No Arduino code and no
// crypto, so it can be checked on a computer (tools/worldtest) and goes into the web app's house.wasm too. Sealing
// rooms for the radio and the home server is plan_seal.h.
//
// One tile is about half a metre; a floor is up to 32 x 32 tiles. A room is a rectangle on it.
#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace plan {

enum RoomType : uint8_t { KITCHEN, BATHROOM, LIVING, DINING, BEDROOM, HALL, LAUNDRY, OFFICE, OUTSIDE, OTHER, ROOM_TYPES };
static const char *const TYPE_NAMES[ROOM_TYPES] = {"Kitchen", "Bathroom", "Living room", "Dining room", "Bedroom",
                                                   "Hall",    "Laundry",  "Office",      "Outside",     "Room"};

static const int NAME = 24, MAX_DOORS = 4, MAX_PIECES = 20, MAX_CHORES = 8, MAX_ROOMS = 16, GRID = 32, FLOORS = 4;
static const int MIN_SIDE = 3;  // tiles: a wall each side and one to stand on

enum Side : uint8_t { NORTH, EAST, SOUTH, WEST, NO_DOOR = 0xFF };

struct Door {
  uint8_t side;  // Side; NO_DOOR for an unused one
  uint8_t at;    // tiles along that wall from its start (the left end of north/south, the top of east/west)
};
struct Piece {
  uint8_t id;    // home_art.h's furniture id; 0 = an unused slot
  uint8_t x, y;  // its top left, in tiles from the room's top left
  uint8_t rot;   // quarter turns clockwise, 0-3
};
struct ChoreRef {  // a chore done in this room: its record's key (house_logic.h)
  uint32_t origin;
  uint16_t seq;
};

struct Room {
  uint32_t id;  // never 0; the same on every X4
  char name[NAME + 1];
  uint8_t type, floor, x, y, w, h;
  uint32_t owner;  // a bedroom's roommate (their Pet id), else 0
  Door doors[MAX_DOORS];
  Piece pieces[MAX_PIECES];
  ChoreRef chores[MAX_CHORES];
};

// ---- A room as bytes: what the web app sends the X4 (PLAN SET), the X4 lists (PLAN LIST), and plan_seal.h seals.
// id u32, name (24, zero-padded), type, floor, x, y, w, h, owner u32, doors (side, at) x 4, pieces (id, x, y, rot)
// x 20, chores (origin u32, seq u16) x 8. Little-endian.
static const size_t ROOM_LEN = 4 + NAME + 6 + 4 + MAX_DOORS * 2 + MAX_PIECES * 4 + MAX_CHORES * 6;  // 174

inline void put32(uint8_t *p, uint32_t v) {
  for (int i = 0; i < 4; i++) p[i] = v >> (8 * i);
}
inline uint32_t get32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

inline void encodeRoom(const Room &r, uint8_t *o) {
  memset(o, 0, ROOM_LEN);
  put32(o, r.id);
  memcpy(o + 4, r.name, strnlen(r.name, NAME));
  uint8_t *p = o + 4 + NAME;
  p[0] = r.type, p[1] = r.floor, p[2] = r.x, p[3] = r.y, p[4] = r.w, p[5] = r.h;
  put32(p + 6, r.owner);
  p += 10;
  for (int i = 0; i < MAX_DOORS; i++, p += 2) p[0] = r.doors[i].side, p[1] = r.doors[i].at;
  for (int i = 0; i < MAX_PIECES; i++, p += 4)
    p[0] = r.pieces[i].id, p[1] = r.pieces[i].x, p[2] = r.pieces[i].y, p[3] = r.pieces[i].rot;
  for (int i = 0; i < MAX_CHORES; i++, p += 6) put32(p, r.chores[i].origin), p[4] = r.chores[i].seq, p[5] = r.chores[i].seq >> 8;
}

// A proper room: on the grid, a name of printable characters, doors on its walls, furniture inside it.
inline bool valid(const Room &r) {
  if (!r.id || r.type >= ROOM_TYPES || r.floor >= FLOORS || r.w < MIN_SIDE || r.h < MIN_SIDE || r.x + r.w > GRID ||
      r.y + r.h > GRID)
    return false;
  for (size_t i = 0; i < strnlen(r.name, NAME); i++)
    if (r.name[i] < 0x20 || r.name[i] > 0x7E) return false;
  for (const Door &d : r.doors)
    if (d.side != NO_DOOR && (d.side > WEST || d.at >= ((d.side == NORTH || d.side == SOUTH) ? r.w : r.h))) return false;
  for (const Piece &p : r.pieces)
    if (p.id && (p.x >= r.w || p.y >= r.h || p.rot > 3)) return false;
  return true;
}

inline bool decodeRoom(const uint8_t *o, Room &r) {
  memset(&r, 0, sizeof r);
  r.id = get32(o);
  memcpy(r.name, o + 4, NAME);
  r.name[NAME] = 0;
  const uint8_t *p = o + 4 + NAME;
  r.type = p[0], r.floor = p[1], r.x = p[2], r.y = p[3], r.w = p[4], r.h = p[5];
  r.owner = get32(p + 6);
  p += 10;
  for (int i = 0; i < MAX_DOORS; i++, p += 2) r.doors[i] = {p[0], p[1]};
  for (int i = 0; i < MAX_PIECES; i++, p += 4) r.pieces[i] = {p[0], p[1], p[2], p[3]};
  for (int i = 0; i < MAX_CHORES; i++, p += 6) r.chores[i] = {get32(p), (uint16_t)(p[4] | p[5] << 8)};
  return valid(r);
}

// ---- Chores in rooms: a chore listed in a room is done there; any other goes by its name (dish -> kitchen, bins ->
// outside...) to the first room of that type. OTHER: no clue (the simulation puts it in the living room, or wherever).
inline bool contains(const char *text, const char *word) {
  for (const char *t = text; *t; t++) {
    size_t i = 0;
    while (word[i] && t[i] && (t[i] | 0x20) == word[i]) i++;
    if (!word[i]) return true;
  }
  return false;
}
inline uint8_t roomTypeForChore(const char *text) {
  static const struct {
    const char *word;
    uint8_t type;
  } WORDS[] = {
    {"dish", KITCHEN},     {"kitchen", KITCHEN},   {"cook", KITCHEN},     {"fridge", KITCHEN},  {"oven", KITCHEN},
    {"counter", KITCHEN},  {"hob", KITCHEN},       {"bath", BATHROOM},    {"toilet", BATHROOM}, {"shower", BATHROOM},
    {"loo", BATHROOM},     {"laundry", LAUNDRY},   {"washing", LAUNDRY},  {"iron", LAUNDRY},    {"bin", OUTSIDE},
    {"trash", OUTSIDE},    {"rubbish", OUTSIDE},   {"recycl", OUTSIDE},   {"garden", OUTSIDE},  {"lawn", OUTSIDE},
    {"plant", OUTSIDE},    {"hoover", LIVING},     {"vacuum", LIVING},    {"dust", LIVING},     {"sweep", LIVING},
    {"mop", LIVING},       {"tidy", LIVING},       {"bed", BEDROOM},      {"sheet", BEDROOM},   {"desk", OFFICE},
    {"hall", HALL},        {"stairs", HALL},       {"table", DINING},
  };
  for (const auto &w : WORDS)
    if (contains(text, w.word)) return w.type;
  return OTHER;
}

// ---- Templates: a home sized to the household (a bedroom each), shown until someone draws their own. Its rooms' ids
// are fixed (shared rooms 0x7E0000nn, a bedroom 0x7F + the low 24 bits of its roommate's Pet id), so the same
// template is the same home on every X4, and a roommate's first edit keeps its room's id.
// Furniture ids are home_art.h's standard ones (1-63).
enum StandardPiece : uint8_t {
  BED = 1, SOFA, TABLE, CHAIR, DESK, WARDROBE, BATH, TOILET, SINK, STOVE, FRIDGE, BINS, PLANT, RUG, LAMP, BOOKSHELF, TV,
  WASHER
};

inline Room makeRoom(uint32_t id, const char *name, uint8_t type, uint8_t floor, uint8_t x, uint8_t y, uint8_t w,
                     uint8_t h) {
  Room r;
  memset(&r, 0, sizeof r);
  r.id = id;
  strncpy(r.name, name, NAME);
  r.type = type, r.floor = floor, r.x = x, r.y = y, r.w = w, r.h = h;
  for (Door &d : r.doors) d.side = NO_DOOR;
  return r;
}
inline void addPiece(Room &r, uint8_t id, uint8_t x, uint8_t y, uint8_t rot = 0) {
  for (Piece &p : r.pieces)
    if (!p.id) {
      p = {id, x, y, rot};
      return;
    }
}
inline void addDoor(Room &r, uint8_t side, uint8_t at) {
  for (Door &d : r.doors)
    if (d.side == NO_DOOR) {
      d = {side, at};
      return;
    }
}

// The template for these roommates (up to 8): kitchen, living room and bathroom downstairs with the garden outside,
// a bedroom each upstairs (two rows of up to four). Returns the number of rooms written to out (MAX_ROOMS fits).
inline int makeTemplate(const uint32_t *members, int n, Room *out) {
  int k = 0;
  Room kitchen = makeRoom(0x7E000001, "Kitchen", KITCHEN, 0, 0, 0, 8, 6);
  addPiece(kitchen, STOVE, 1, 1), addPiece(kitchen, FRIDGE, 3, 1), addPiece(kitchen, SINK, 5, 1);
  addPiece(kitchen, TABLE, 3, 3), addPiece(kitchen, CHAIR, 2, 3), addPiece(kitchen, CHAIR, 5, 3), addPiece(kitchen, BINS, 6, 4);
  addDoor(kitchen, EAST, 3), addDoor(kitchen, SOUTH, 2);
  out[k++] = kitchen;
  Room living = makeRoom(0x7E000002, "Living room", LIVING, 0, 8, 0, 10, 8);
  addPiece(living, SOFA, 2, 5), addPiece(living, TV, 3, 1), addPiece(living, RUG, 3, 3), addPiece(living, PLANT, 8, 1);
  addPiece(living, LAMP, 1, 1), addDoor(living, WEST, 3), addDoor(living, SOUTH, 5);
  out[k++] = living;
  Room bath = makeRoom(0x7E000003, "Bathroom", BATHROOM, 0, 0, 6, 6, 5);
  addPiece(bath, BATH, 1, 1), addPiece(bath, TOILET, 4, 1), addPiece(bath, SINK, 4, 3), addDoor(bath, NORTH, 2);
  out[k++] = bath;
  Room garden = makeRoom(0x7E000004, "Garden", OUTSIDE, 0, 6, 8, 12, 5);
  addPiece(garden, BINS, 1, 1), addPiece(garden, BINS, 2, 1), addPiece(garden, PLANT, 8, 2), addDoor(garden, NORTH, 7);
  out[k++] = garden;
  for (int i = 0; i < n && i < 8 && k < MAX_ROOMS; i++) {
    Room bed = makeRoom(0x7F000000 | (members[i] & 0xFFFFFF), "Bedroom", BEDROOM, 1, (i % 4) * 7, (i / 4) * 7, 7, 7);
    bed.owner = members[i];
    addPiece(bed, BED, 1, 1), addPiece(bed, WARDROBE, 4, 1), addPiece(bed, DESK, 4, 4), addPiece(bed, RUG, 1, 4);
    addDoor(bed, i / 4 ? NORTH : SOUTH, 3);
    out[k++] = bed;
  }
  return k;
}

}  // namespace plan
