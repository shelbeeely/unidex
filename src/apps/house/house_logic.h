#pragma once
// The household that roommates' Pets share: members (one Pet each), chores on a rota, the shopping list, money
// spent for everyone and settled up, regular reminders (bin day, rent) and the Pets' messages. No Arduino code, so
// it can be checked on a computer (tools/housetest).
//
// Everything is a Record. Each device keeps a copy of all of them and devices swap copies when they meet (Sync),
// so there is no server and no one device in charge. A record is known by who made it and their count (origin,
// seq); every change stamps it with a Lamport clock (ver) and who made the change (editor). Merging keeps, for each
// record, the copy with the higher (ver, editor): every device ends up with the same household whichever order the
// copies arrive in, and however often. Deleting is a change too (the DELETED flag), so a deletion spreads like an
// edit.
#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace house {

static const size_t MAX_RECORDS = 120, MAX_MEMBERS = 8, TEXT = 24, NAME = 12;

enum Kind : uint8_t {
  MEMBER = 1,    // a roommate's Pet: text = Pet name, a = Pet look (pet_logic.h's pack)
  CHORE = 2,     // text = chore, a = every how many days, b = due day, c = who did it last (0 = no one), d = that day
  ITEM = 3,      // shopping: text = what, a = 1 once bought
  EXPENSE = 4,   // text = what for, a = who paid, b = pence; shared by everyone who had joined by then (born)
  SETTLE = 5,    // a = who paid, b = pence, c = to whom
  REMINDER = 6,  // text = what, a = 0 weekly (b = weekday, 0 Sunday) or 1 monthly (b = day of the month)
  MESSAGE = 7,   // text = what the Pet says, from origin; the newest few are kept
};
enum Flags : uint8_t { DELETED = 1 };

struct Record {
  uint8_t kind, flags;
  uint16_t seq;     // the origin's count
  uint32_t origin;  // who made it (a member's Pet id)
  uint32_t ver;     // Lamport clock of the last change
  uint32_t editor;  // who made the last change
  uint32_t born;    // Lamport clock when it was made
  uint32_t a, b, c, d;
  char text[TEXT + 1];
};

inline bool sameKey(const Record &x, const Record &y) { return x.origin == y.origin && x.seq == y.seq; }
inline bool newer(const Record &x, const Record &y) {
  return x.ver != y.ver ? x.ver > y.ver : x.editor > y.editor;
}

struct House {
  uint32_t id;     // the household (random, made by whoever started it); 0 = not in one
  uint32_t me;     // this device's Pet id
  uint32_t clock;  // Lamport clock: above every ver seen
  uint16_t nextSeq;
  uint16_t count;
  Record r[MAX_RECORDS];
};

inline void tick(House &h, uint32_t seen) {
  if (seen >= h.clock) h.clock = seen;
  h.clock++;
}

// What merging one record did.
enum Merge { SAME, ADDED, UPDATED, OLDER, FULL };

// Messages: only the newest MAX_MESSAGES are kept (the oldest is dropped to make room).
static const int MAX_MESSAGES = 10;

inline int countKind(const House &h, uint8_t kind) {
  int n = 0;
  for (int i = 0; i < h.count; i++) n += h.r[i].kind == kind;
  return n;
}

inline void removeAt(House &h, int i) {
  memmove(&h.r[i], &h.r[i + 1], (h.count - i - 1) * sizeof(Record));
  h.count--;
}

inline void dropOldestMessage(House &h) {
  int oldest = -1;
  for (int i = 0; i < h.count; i++)
    if (h.r[i].kind == MESSAGE && (oldest < 0 || h.r[i].born < h.r[oldest].born)) oldest = i;
  if (oldest >= 0) removeAt(h, oldest);
}

// Removed records stay (as DELETED) so the removal spreads; when the household is full, the oldest of them go.
inline bool makeRoom(House &h) {
  int oldest = -1;
  for (int i = 0; i < h.count; i++)
    if ((h.r[i].flags & DELETED) && h.r[i].kind != MEMBER && (oldest < 0 || h.r[i].ver < h.r[oldest].ver)) oldest = i;
  if (oldest < 0) return false;
  removeAt(h, oldest);
  return true;
}

inline Merge merge(House &h, const Record &in) {
  tick(h, in.ver);
  for (int i = 0; i < h.count; i++) {
    if (!sameKey(h.r[i], in)) continue;
    if (h.r[i].kind != in.kind) return OLDER;  // a corrupt copy: keep ours
    if (memcmp(&h.r[i], &in, sizeof in) == 0) return SAME;
    if (!newer(in, h.r[i])) return OLDER;
    h.r[i] = in;
    return UPDATED;
  }
  if (in.kind == MESSAGE && countKind(h, MESSAGE) >= MAX_MESSAGES) {
    // Older than every message kept: it would only be dropped again.
    bool older = true;
    for (int i = 0; i < h.count; i++)
      if (h.r[i].kind == MESSAGE && h.r[i].born < in.born) older = false;
    if (older) return OLDER;
    dropOldestMessage(h);
  }
  if (h.count >= MAX_RECORDS && ((in.flags & DELETED) || !makeRoom(h))) return FULL;
  h.r[h.count++] = in;
  return ADDED;
}

// A new record made on this device (text cut to fit). nullptr when the household is full.
inline Record *add(House &h, uint8_t kind, const char *text, uint32_t a = 0, uint32_t b = 0, uint32_t c = 0,
                   uint32_t d = 0) {
  if (kind == MESSAGE && countKind(h, MESSAGE) >= MAX_MESSAGES) dropOldestMessage(h);
  if (h.count >= MAX_RECORDS && !makeRoom(h)) return nullptr;
  tick(h, 0);
  Record &r = h.r[h.count++];
  memset(&r, 0, sizeof r);
  r.kind = kind;
  r.seq = ++h.nextSeq;
  r.origin = r.editor = h.me;
  r.ver = r.born = h.clock;
  r.a = a, r.b = b, r.c = c, r.d = d;
  if (text) strncpy(r.text, text, TEXT);
  return &r;
}

// Stamp a change made on this device to r (call after changing its fields).
inline void touch(House &h, Record &r) {
  tick(h, r.ver);
  r.ver = h.clock;
  r.editor = h.me;
}

inline void remove(House &h, Record &r) {
  r.flags |= DELETED;
  touch(h, r);
}

inline bool live(const Record &r, uint8_t kind) { return r.kind == kind && !(r.flags & DELETED); }

// ---- Members, in the order they joined (born, then id): the rota and the money both go by it.

inline int members(const House &h, const Record **out) {
  int n = 0;
  for (int i = 0; i < h.count && n < (int)MAX_MEMBERS; i++)
    if (live(h.r[i], MEMBER)) out[n++] = &h.r[i];
  for (int i = 1; i < n; i++)  // insertion sort: a handful of members
    for (int j = i; j > 0 && (out[j]->born < out[j - 1]->born ||
                              (out[j]->born == out[j - 1]->born && out[j]->origin < out[j - 1]->origin));
         j--) {
      const Record *t = out[j];
      out[j] = out[j - 1];
      out[j - 1] = t;
    }
  return n;
}

inline const Record *member(const House &h, uint32_t id) {
  for (int i = 0; i < h.count; i++)
    if (h.r[i].kind == MEMBER && h.r[i].origin == id) return &h.r[i];
  return nullptr;
}

inline const char *nameOf(const House &h, uint32_t id) {
  const Record *m = member(h, id);
  return m && m->text[0] ? m->text : "?";
}

// ---- Chores. Days are whole days since 1970 in local time. A chore is due on b; doing it moves the due day on
// by a days from the day it was done, and the turn to the next member after whoever did it.

inline uint32_t turnOf(const House &h, const Record &chore) {
  const Record *m[MAX_MEMBERS];
  const int n = members(h, m);
  if (!n) return 0;
  if (!chore.c) return m[chore.seq % n]->origin;  // never done: spread new chores out
  for (int i = 0; i < n; i++)
    if (m[i]->origin == chore.c) return m[(i + 1) % n]->origin;
  return m[0]->origin;  // whoever did it last has left
}

inline bool overdue(const Record &chore, uint32_t today) { return today > chore.b; }

inline void choreDone(House &h, Record &chore, uint32_t today) {
  chore.c = h.me;
  chore.d = today;
  chore.b = today + (chore.a ? chore.a : 1);
  touch(h, chore);
}

// ---- Money. Who owes whom, in pence: positive = the member is owed that much.

inline void balances(const House &h, const Record *const *m, int n, int32_t *balance) {
  for (int i = 0; i < n; i++) balance[i] = 0;
  auto slot = [&](uint32_t id) {
    for (int i = 0; i < n; i++)
      if (m[i]->origin == id) return i;
    return -1;
  };
  for (int i = 0; i < h.count; i++) {
    const Record &r = h.r[i];
    if (live(r, EXPENSE)) {
      const int payer = slot(r.a);
      if (payer < 0) continue;
      int sharers = 0;
      for (int k = 0; k < n; k++) sharers += m[k]->born <= r.born;
      if (!sharers) continue;
      const int32_t share = r.b / sharers;
      balance[payer] += r.b;
      for (int k = 0; k < n; k++)
        if (m[k]->born <= r.born) balance[k] -= share;
      balance[payer] -= r.b - share * sharers;  // the pennies that don't split evenly stay with the payer
    } else if (live(r, SETTLE)) {
      const int from = slot(r.a), to = slot(r.c);
      if (from < 0 || to < 0) continue;
      balance[from] += r.b;
      balance[to] -= r.b;
    }
  }
}

struct Debt {
  uint8_t from, to;  // member slots
  int32_t pence;
};

// The fewest payments that settle everyone up (the biggest debtor pays the biggest creditor, and so on).
inline int settleUp(const int32_t *balance, int n, Debt *out, int maxOut) {
  int32_t b[MAX_MEMBERS];
  for (int i = 0; i < n; i++) b[i] = balance[i];
  int count = 0;
  while (count < maxOut) {
    int lo = 0, hi = 0;
    for (int i = 1; i < n; i++) {
      if (b[i] < b[lo]) lo = i;
      if (b[i] > b[hi]) hi = i;
    }
    if (n == 0 || b[lo] >= 0 || b[hi] <= 0) break;
    const int32_t p = -b[lo] < b[hi] ? -b[lo] : b[hi];
    out[count++] = {(uint8_t)lo, (uint8_t)hi, p};
    b[lo] += p;
    b[hi] -= p;
  }
  return count;
}

// ---- Reminders: is one on `weekday` (0 Sunday) / day of the month `mday`?

inline bool remindToday(const Record &r, int weekday, int mday) {
  return r.a == 0 ? (int)r.b == weekday : (int)r.b == mday;
}

// ---- The radio. Every packet: "UDH", version, type, the household id (u32), then the type's fields.
//   HELLO   (to everyone, about twice a second while Sync is open): sender's Pet id u32, unix time u32 (0 if its clock
//           isn't set), when its time was last set from the internet, USB or a clock chip u32 (unix; a device takes
//           the time from one set more recently, see timeIsBetter), Pet look u32, Lamport clock u32 (a
//           device joining starts after it, so its arrival comes after everything already spent), name length u8 +
//           up to 12 bytes. Household id 0: not in one yet (it can be asked in).
//   RECORDS (to everyone): count u8, then that many records.
// A device only merges records for its own household.

static const uint8_t VERSION = 1;
enum Type : uint8_t { HELLO = 1, RECORDS = 2 };
static const size_t HEADER = 9, RECORD_LEN = 2 + 2 + 4 * 8 + 1 + TEXT, PER_PACKET = 3;
static const size_t HELLO_LEN = HEADER + 4 + 4 + 4 + 4 + 4 + 1 + NAME;

inline void put32(uint8_t *p, uint32_t v) {
  for (int i = 0; i < 4; i++) p[i] = v >> (8 * i);
}
inline uint32_t get32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

inline size_t header(uint8_t *out, Type t, uint32_t houseId) {
  out[0] = 'U', out[1] = 'D', out[2] = 'H', out[3] = VERSION, out[4] = t;
  put32(out + 5, houseId);
  return HEADER;
}

inline uint8_t typeOf(const uint8_t *p, size_t len) {
  if (len < HEADER || p[0] != 'U' || p[1] != 'D' || p[2] != 'H' || p[3] != VERSION) return 0;
  return p[4];
}
inline uint32_t houseOf(const uint8_t *p) { return get32(p + 5); }

struct Hello {
  uint32_t house, id, time, trusted, look, clock;
  char name[NAME + 1];
};

inline size_t encodeHello(const Hello &h, uint8_t *out) {
  header(out, HELLO, h.house);
  put32(out + 9, h.id);
  put32(out + 13, h.time);
  put32(out + 17, h.trusted);
  put32(out + 21, h.look);
  put32(out + 25, h.clock);
  const size_t n = strnlen(h.name, NAME);
  out[29] = n;
  memset(out + 30, 0, NAME);
  memcpy(out + 30, h.name, n);
  return HELLO_LEN;
}

inline bool printable(const char *s, size_t n) {
  for (size_t i = 0; i < n; i++)
    if (s[i] < 0x20 || s[i] > 0x7E) return false;
  return true;
}

inline bool decodeHello(const uint8_t *p, size_t len, Hello &h) {
  if (typeOf(p, len) != HELLO || len != HELLO_LEN || p[29] > NAME || !printable((const char *)p + 30, p[29]))
    return false;
  h.house = houseOf(p);
  h.id = get32(p + 9);
  h.time = get32(p + 13);
  h.trusted = get32(p + 17);
  h.look = get32(p + 21);
  h.clock = get32(p + 25);
  memcpy(h.name, p + 30, p[29]);
  h.name[p[29]] = 0;
  return h.id != 0;
}

// Should this device take a roommate's time? Yes if its own isn't set, or if theirs was set from a good source more
// than an hour more recently and the two differ by more than 30 s (a sleeping ESP32's timer drifts; a minute or
// so either way isn't worth a change).
static const uint32_t VALID_TIME = 1704067200;  // 2024-01-01
inline bool timeIsBetter(uint32_t mine, uint32_t myTrusted, uint32_t theirs, uint32_t theirTrusted) {
  if (theirs < VALID_TIME || !theirTrusted) return false;
  if (mine < VALID_TIME) return true;
  const uint32_t apart = mine > theirs ? mine - theirs : theirs - mine;
  return theirTrusted > myTrusted + 3600 && apart > 30;
}

inline void encodeRecord(const Record &r, uint8_t *o) {
  o[0] = r.kind, o[1] = r.flags;
  o[2] = r.seq, o[3] = r.seq >> 8;
  const uint32_t v[8] = {r.origin, r.ver, r.editor, r.born, r.a, r.b, r.c, r.d};
  for (int i = 0; i < 8; i++) put32(o + 4 + 4 * i, v[i]);
  const size_t n = strnlen(r.text, TEXT);
  o[36] = n;
  memset(o + 37, 0, TEXT);
  memcpy(o + 37, r.text, n);
}

inline bool decodeRecord(const uint8_t *o, Record &r) {
  memset(&r, 0, sizeof r);
  if (o[0] < MEMBER || o[0] > MESSAGE || o[36] > TEXT || !printable((const char *)o + 37, o[36])) return false;
  r.kind = o[0], r.flags = o[1] & DELETED;
  r.seq = o[2] | o[3] << 8;
  uint32_t *v[8] = {&r.origin, &r.ver, &r.editor, &r.born, &r.a, &r.b, &r.c, &r.d};
  for (int i = 0; i < 8; i++) *v[i] = get32(o + 4 + 4 * i);
  memcpy(r.text, o + 37, o[36]);
  return r.origin != 0 && r.seq != 0;
}

// Up to PER_PACKET records from `from` on; returns the packet length (the next start is from + count).
inline size_t encodeRecords(const House &h, int from, uint8_t *out, int &count) {
  header(out, RECORDS, h.id);
  count = 0;
  while (count < (int)PER_PACKET && from + count < h.count) {
    encodeRecord(h.r[from + count], out + HEADER + 1 + count * RECORD_LEN);
    count++;
  }
  out[HEADER] = count;
  return HEADER + 1 + count * RECORD_LEN;
}

// Records in a packet for this household, merged. The callback hears about each one that changed something
// (for the Pets' news: someone else did a chore). Returns how many changed.
template <typename F>
inline int mergePacket(House &h, const uint8_t *p, size_t len, F changed) {
  if (typeOf(p, len) != RECORDS || h.id == 0 || houseOf(p) != h.id || len < HEADER + 1) return 0;
  const int n = p[HEADER];
  if (n > (int)PER_PACKET || len != HEADER + 1 + n * RECORD_LEN) return 0;
  int changes = 0;
  for (int i = 0; i < n; i++) {
    Record r;
    if (!decodeRecord(p + HEADER + 1 + i * RECORD_LEN, r)) continue;
    Record before;
    const Record *old = nullptr;
    for (int k = 0; k < h.count; k++)
      if (sameKey(h.r[k], r)) {
        before = h.r[k];
        old = &before;
      }
    const Merge m = merge(h, r);
    if (m == ADDED || m == UPDATED) {
      changes++;
      changed(r, old);
    }
  }
  return changes;
}

}  // namespace house
