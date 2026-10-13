#pragma once
// The home plan's rooms on their way between X4s and through the home server (plan_logic.h): each room sealed
// (AES-256-GCM) with the plan key, made from the household's server token (home_logic.h's serverToken). So roommates'
// X4s and the household's home server can read and draw the plan, and nobody listening on the radio learns the layout
// of someone's home. Everything about a room but its contents is checked too (its id, version, editor, removed), so
// only the household's X4s and its server can add, change or remove a room. No Arduino code (mbed TLS, as
// secure_logic.h), so it can be checked on a computer (tools/worldtest).
//
// Merging is like the household's records: for each room, the newest version wins (a removal too). A bedroom is its
// owner's alone: a version of a bedroom made by anyone else is refused, and so is anyone else turning someone's
// bedroom into something else or removing it.
#include "plan_logic.h"
#include "secure_logic.h"

namespace plan {

enum EntryFlags : uint8_t { REMOVED = 1 };
static const size_t ENTRY_LEN = 4 + 4 + 4 + 1 + secure::NONCE + ROOM_LEN + secure::TAG;  // 215

struct Entry {
  uint32_t id, ver, editor;  // the room's id; its version (newest wins); the Pet id of whoever made this version
  uint8_t flags;
  uint8_t nonce[secure::NONCE];
  uint8_t sealed[ROOM_LEN];
  uint8_t tag[secure::TAG];
};

inline void planKey(const uint8_t token[secure::KEY], uint8_t out[secure::KEY]) {
  secure::derive(token, "RoomiePet home plan", out);
}

inline void entryAad(uint32_t house, const Entry &e, uint8_t aad[17]) {
  put32(aad, house), put32(aad + 4, e.id), put32(aad + 8, e.ver), put32(aad + 12, e.editor);
  aad[16] = e.flags;
}

// Seals a room into e (whose id, ver, editor and flags are set; e.id must be the room's).
inline bool sealRoom(const uint8_t key[secure::KEY], uint32_t house, Entry &e, const Room &r, secure::Random random) {
  if (r.id != e.id || !valid(r)) return false;
  uint8_t plain[ROOM_LEN], aad[17];
  encodeRoom(r, plain);
  random(e.nonce, secure::NONCE);
  entryAad(house, e, aad);
  return secure::seal(key, e.nonce, aad, sizeof aad, plain, ROOM_LEN, e.sealed, e.tag);
}
// Opens one: false if it isn't this household's (another key), was changed, or isn't a proper room.
inline bool openRoom(const uint8_t key[secure::KEY], uint32_t house, const Entry &e, Room &r) {
  uint8_t plain[ROOM_LEN], aad[17];
  entryAad(house, e, aad);
  return secure::open(key, e.nonce, aad, sizeof aad, e.sealed, ROOM_LEN, e.tag, plain) && decodeRoom(plain, r) &&
         r.id == e.id;
}

inline void encodeEntry(const Entry &e, uint8_t *o) {
  put32(o, e.id), put32(o + 4, e.ver), put32(o + 8, e.editor);
  o[12] = e.flags;
  memcpy(o + 13, e.nonce, secure::NONCE);
  memcpy(o + 13 + secure::NONCE, e.sealed, ROOM_LEN);
  memcpy(o + 13 + secure::NONCE + ROOM_LEN, e.tag, secure::TAG);
}
inline bool decodeEntry(const uint8_t *o, Entry &e) {
  e.id = get32(o), e.ver = get32(o + 4), e.editor = get32(o + 8);
  e.flags = o[12];
  memcpy(e.nonce, o + 13, secure::NONCE);
  memcpy(e.sealed, o + 13 + secure::NONCE, ROOM_LEN);
  memcpy(e.tag, o + 13 + secure::NONCE + ROOM_LEN, secure::TAG);
  return e.id && e.ver && e.flags <= REMOVED;
}

inline bool newerEntry(const Entry &x, const Entry &y) { return x.ver != y.ver ? x.ver > y.ver : x.editor > y.editor; }

// The household's rooms, as sealed entries.
struct PlanList {
  Entry e[MAX_ROOMS];
  int count = 0;

  int find(uint32_t id) const {
    for (int i = 0; i < count; i++)
      if (e[i].id == id) return i;
    return -1;
  }
  uint32_t nextVer() const {
    uint32_t v = 0;
    for (int i = 0; i < count; i++) v = e[i].ver > v ? e[i].ver : v;
    return v + 1;
  }
  // A room (from the radio, the home server, or this X4 itself): true if it changed the list. Refused if it doesn't
  // open, isn't newer, or breaks the bedroom rule; when the list is full, the oldest removed room makes way.
  bool merge(const Entry &in, const uint8_t key[secure::KEY], uint32_t house) {
    Room r;
    if (!openRoom(key, house, in, r)) return false;
    if (r.type == BEDROOM && r.owner && in.editor != r.owner) return false;
    const int i = find(in.id);
    if (i >= 0) {
      if (!newerEntry(in, e[i])) return false;
      Room old;
      if (openRoom(key, house, e[i], old) && !(e[i].flags & REMOVED) && old.type == BEDROOM && old.owner &&
          in.editor != old.owner)
        return false;
      e[i] = in;
      return true;
    }
    if (count == MAX_ROOMS) {
      int oldest = -1;
      for (int k = 0; k < count; k++)
        if ((e[k].flags & REMOVED) && (oldest < 0 || e[k].ver < e[oldest].ver)) oldest = k;
      if (oldest < 0) return false;
      e[oldest] = e[--count];
    }
    e[count++] = in;
    return true;
  }
  // The rooms still in the plan, opened, into out (MAX_ROOMS fits): how many.
  int rooms(const uint8_t key[secure::KEY], uint32_t house, Room *out) const {
    int n = 0;
    for (int i = 0; i < count; i++)
      if (!(e[i].flags & REMOVED) && openRoom(key, house, e[i], out[n])) n++;
    return n;
  }
};

// ---- The radio, at the household's Sync: secure_logic.h's "RPK" packets, type 4: house u32, an entry (224 bytes).
static const uint8_t PLAN_PACKET = 4;
static const size_t PLAN_PACKET_LEN = secure::HEADER + 4 + ENTRY_LEN;

inline size_t encodePlanPacket(uint32_t house, const Entry &e, uint8_t *out) {
  memcpy(out, secure::MAGIC, 3), out[3] = secure::VERSION, out[4] = PLAN_PACKET;
  put32(out + 5, house);
  encodeEntry(e, out + 9);
  return PLAN_PACKET_LEN;
}
inline bool decodePlanPacket(const uint8_t *p, size_t len, uint32_t &house, Entry &e) {
  if (secure::typeOf(p, len) != PLAN_PACKET || len != PLAN_PACKET_LEN) return false;
  house = get32(p + 5);
  return decodeEntry(p + 9, e);
}

}  // namespace plan
