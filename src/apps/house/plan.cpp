// The household's home plan on the X4 (plan.h). The foundation's simple version: the plan kept in memory only, no
// sealing, no Sync, no home server (tools/worldtest and the web app work against it meanwhile).
#include "plan.h"
#include "house.h"
#include "../../core/usbsync.h"

#if UNIDEX_X4

using house::House;
using house::Record;

static plan::Room rooms[plan::MAX_ROOMS];
static int count;
static bool drawn;  // someone has drawn (or changed) the plan; else it's the template

static void ensure() {
  if (drawn || !houseLoad()) return;
  const Record *m[house::MAX_MEMBERS];
  const int n = members(houseData(), m);
  uint32_t ids[house::MAX_MEMBERS];
  for (int i = 0; i < n; i++) ids[i] = m[i]->origin;
  count = plan::makeTemplate(ids, n, rooms);
}

int planRooms(plan::Room *out, int max) {
  ensure();
  const int n = count < max ? count : max;
  memcpy(out, rooms, n * sizeof(plan::Room));
  return n;
}

bool planRoom(uint32_t id, plan::Room &out) {
  ensure();
  for (int i = 0; i < count; i++)
    if (rooms[i].id == id) return out = rooms[i], true;
  return false;
}

bool planTidy(uint32_t roomId) {
  const uint32_t today = houseToday();
  if (!houseLoad() || !houseData().id || !today || !house::setTidy(houseData(), roomId, today)) return false;
  houseSave();
  return true;
}

void planSyncStart() {}
bool planHeard(const LinkPacket &) { return false; }
void planTick() {}
int planEntryCount() { return 0; }
void planEncodedEntry(int, uint8_t *) {}
bool planMergeEntry(const uint8_t *) { return false; }

static int hexValue(char c) {
  return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
}

bool planUsb(const char *line) {
  Print &out = usbOut();
  if (strcmp(line, "PLAN LIST") == 0) {
    ensure();
    uint8_t b[plan::ROOM_LEN];
    for (int i = 0; i < count; i++) {
      plan::encodeRoom(rooms[i], b);
      out.print("PR ");
      for (uint8_t v : b) out.printf("%02x", v);
      out.print('\n');
    }
    out.printf("OK PLAN LIST %d %d\n", count, drawn);
  } else if (strncmp(line, "PLAN SET ", 9) == 0) {
    const char *hex = line + 9;
    uint8_t b[plan::ROOM_LEN];
    plan::Room r;
    if (strlen(hex) != 2 * plan::ROOM_LEN) return out.println("ERR"), true;
    for (size_t i = 0; i < plan::ROOM_LEN; i++) {
      const int hi = hexValue(hex[2 * i]), lo = hexValue(hex[2 * i + 1]);
      if (hi < 0 || lo < 0) return out.println("ERR"), true;
      b[i] = hi << 4 | lo;
    }
    if (!plan::decodeRoom(b, r)) return out.println("ERR"), true;
    ensure();
    int i = 0;
    while (i < count && rooms[i].id != r.id) i++;
    if (i == count && count == plan::MAX_ROOMS) return out.println("ERR"), true;
    rooms[i] = r;
    if (i == count) count++;
    drawn = true;
    out.println("OK PLAN SET");
  } else if (strncmp(line, "PLAN DEL ", 9) == 0) {
    ensure();
    const uint32_t id = strtoul(line + 9, nullptr, 16);
    int i = 0;
    while (i < count && rooms[i].id != id) i++;
    if (i == count) return out.println("ERR"), true;
    rooms[i] = rooms[--count];
    drawn = true;
    out.println("OK PLAN DEL");
  } else if (strncmp(line, "TIDY ", 5) == 0) {
    planTidy(strtoul(line + 5, nullptr, 16));
    out.println("OK TIDY");
  } else {
    return false;
  }
  return true;
}

#else  // the Waveshare board: no home plan

int planRooms(plan::Room *, int) { return 0; }
bool planRoom(uint32_t, plan::Room &) { return false; }
bool planTidy(uint32_t) { return false; }
void planSyncStart() {}
bool planHeard(const LinkPacket &) { return false; }
void planTick() {}
int planEntryCount() { return 0; }
void planEncodedEntry(int, uint8_t *) {}
bool planMergeEntry(const uint8_t *) { return false; }
bool planUsb(const char *) { return false; }

#endif
