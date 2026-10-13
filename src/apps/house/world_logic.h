#pragma once
// The virtual household (RoomiePet): the home plan's rooms (plan_logic.h) as they are right now. How messy each is,
// where each piece of clutter lies, the House Health, and what each Pet at home is doing. Worked out the same way on
// every X4, in the web app and on the home server's page (house.wasm) from the plan, the household's records, the date
// and time, and who's home, so every screen shows the same mess in the same places. Pets follow a daily routine with
// seeded randomness: no AI. No Arduino code, so it can be checked on a computer (tools/worldtest).
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "house_logic.h"
#include "plan_logic.h"

namespace world {

static const int MAX_CLUTTER = 24, MAX_PETS = 8;

enum Activity : uint8_t { SLEEP, EAT, SOFA, VISIT, POTTER, CLEAN, ACTIVITIES };
enum ClutterKind : uint8_t { PLATES, CUP, BINBAG, TOWEL, DUST, CRUMBS, CLOTHES, SOCK, PAPER, CLUTTER_KINDS };
// The tool a Pet cleans with: the chore tools in the item catalog (catalog/items.txt), by name.
enum Tool : uint8_t { NO_TOOL, BROOM, MOP, SPRAY, SPONGE, PLUNGER, DUSTER, LAUNDRY_BASKET, BIN_BAG, TOOLS };
static const char *const TOOL_ITEMS[TOOLS] = {"", "Broom", "Mop", "Spray", "Sponge", "Plunger", "Duster", "Laundry",
                                              "Bin bag"};

struct Clutter {
  uint8_t kind;  // ClutterKind
  uint8_t x, y;  // tile, from the room's top left
};
struct RoomState {
  uint8_t mess;   // 0 tidy to 100 a tip
  uint8_t count;  // clutter pieces showing
  Clutter clutter[MAX_CLUTTER];
};
struct PetState {
  uint32_t pet;      // its Pet id (house_logic.h's member origin)
  int8_t room;       // index into the scene's rooms
  uint8_t x, y;      // tile, from the room's top left
  uint8_t activity;  // Activity
  uint8_t tool;      // Tool (CLEAN), else NO_TOOL
};

struct Input {
  const plan::Room *rooms;
  int count;
  const house::House *house;
  uint32_t today;      // local days since 1970 (0: the clock isn't set: no mess from chores coming due)
  int minute;          // local minute of the day, 0-1439
  const uint32_t *home;  // the Pets at home (checked in with the home server lately, or heard at Sync), may include me
  int homeCount;
  uint32_t me;         // this X4's Pet (0: a household display)
  bool away;           // this X4 isn't on the home network: only its Pet's bedroom
};

struct Scene {
  const plan::Room *rooms;
  int count;
  RoomState state[plan::MAX_ROOMS];
  PetState pets[MAX_PETS];
  int petCount;
  uint8_t health;  // House Health, 0-100
  bool away;
  int awayRoom;  // away: the room to show (this Pet's bedroom), -1 if it has none
};

// Which room a chore is done in: the room listing it, else the first room of the type its name suggests, else the
// first living room, else the first room. -1 with no rooms.
inline int roomOfChore(const plan::Room *rooms, int n, const house::Record &chore) {
  for (int i = 0; i < n; i++)
    for (const plan::ChoreRef &c : rooms[i].chores)
      if (c.origin == chore.origin && c.seq == chore.seq) return i;
  const uint8_t types[2] = {plan::roomTypeForChore(chore.text), plan::LIVING};
  for (const uint8_t t : types)
    for (int i = 0; i < n; i++)
      if (rooms[i].type == t) return i;
  return n ? 0 : -1;
}

// Simple first version (the foundation): mess only from late chores, clutter along the bottom wall, Pets standing in
// the first room. tools/worldtest holds the rules the full version keeps.
inline void build(Scene &s, const Input &in) {
  memset(&s, 0, sizeof s);
  s.rooms = in.rooms, s.count = in.count, s.away = in.away, s.awayRoom = -1;
  const house::House &h = *in.house;
  for (int i = 0; i < h.count; i++) {
    const house::Record &c = h.r[i];
    if (!house::live(c, house::CHORE) || !in.today || !house::overdue(c, in.today)) continue;
    const int room = roomOfChore(in.rooms, in.count, c);
    if (room < 0) continue;
    const uint32_t late = in.today - c.b, add = late * 20 > 100 ? 100 : late * 20;
    s.state[room].mess = s.state[room].mess + add > 100 ? 100 : s.state[room].mess + add;
  }
  int total = 0;
  for (int r = 0; r < in.count; r++) {
    RoomState &st = s.state[r];
    const plan::Room &room = in.rooms[r];
    st.count = 0;
    for (int k = 0; k < st.mess / 10 && st.count < MAX_CLUTTER && k < room.w - 2; k++)
      st.clutter[st.count++] = {(uint8_t)(k % CLUTTER_KINDS), (uint8_t)(1 + k), (uint8_t)(room.h - 2)};
    total += st.mess;
    if (room.type == plan::BEDROOM && room.owner == in.me && s.awayRoom < 0) s.awayRoom = r;
  }
  s.health = in.count ? 100 - total / in.count : 100;
  for (int i = 0; i < in.homeCount && s.petCount < MAX_PETS && in.count; i++)
    s.pets[s.petCount++] = {in.home[i], 0, (uint8_t)(1 + (i % (in.rooms[0].w - 2))), 1, POTTER, NO_TOOL};
}

}  // namespace world
