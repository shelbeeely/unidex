// The household on the SD card, today's date, and how this device's Pet feels about it.
#include "house.h"
#include <esp_random.h>
#include "../../core/clock.h"
#include "../../core/storage.h"
#include "../pet/pet.h"

using namespace house;

static const char *PATH = "/house.dat", *TMP = "/house.tmp";
static const char MAGIC[4] = {'U', 'D', 'X', 'H'};
static const uint8_t FILE_VERSION = 1;

static House data;  // ~8 KB: loaded from the card after each wake, on first use
static bool loaded;
RTC_DATA_ATTR static char line[40];  // the home screen's line, kept through sleep

House &houseData() { return data; }
const char *houseCurrency() { return "$"; }

void houseFormatMoney(int32_t pence, char *out, size_t len) {
  const uint32_t p = pence < 0 ? -pence : pence;
  snprintf(out, len, "%s%s%lu.%02lu", pence < 0 ? "-" : "", houseCurrency(), (unsigned long)(p / 100),
           (unsigned long)(p % 100));
}

uint32_t houseToday() {
  if (!clockValid()) return 0;
  const struct tm t = clockLocal();
  // Days from the civil date (Howard Hinnant's days_from_civil), in local time so a day turns at midnight here.
  const int y = t.tm_year + 1900 - (t.tm_mon < 2), m = t.tm_mon + 1, d = t.tm_mday;
  const int era = y / 400, yoe = y - era * 400;
  const int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  return era * 146097 + yoe * 365 + yoe / 4 - yoe / 100 + doy - 719468;
}

static void updateLine();

bool houseLoad() {
  if (loaded) return true;
  if (!storageCardMount()) return false;
  memset(&data, 0, sizeof data);
  fs::File f = storageOpen(PATH, "r");
  char magic[4];
  uint8_t version;
  if (f && f.read((uint8_t *)magic, 4) == 4 && memcmp(magic, MAGIC, 4) == 0 && f.read(&version, 1) == 1 &&
      version == FILE_VERSION && f.read((uint8_t *)&data, sizeof data) == sizeof data && data.count <= MAX_RECORDS) {
    // as saved
  } else {
    memset(&data, 0, sizeof data);
  }
  if (f) f.close();
  data.me = petId();
  loaded = true;
  houseUpdateMe();
  updateLine();
  return true;
}

void houseSave() {
  updateLine();
  fs::File f = storageOpen(TMP, "w");
  if (!f) return;
  f.write((const uint8_t *)MAGIC, 4);
  f.write(&FILE_VERSION, 1);
  f.write((const uint8_t *)&data, sizeof data);
  f.close();
  storageReplace(TMP, PATH, 5 + sizeof data);
}

static Record *myMember() {
  for (int i = 0; i < data.count; i++)
    if (data.r[i].kind == MEMBER && data.r[i].origin == data.me) return &data.r[i];
  return nullptr;
}

void houseUpdateMe() {
  if (!data.id) return;
  String name = petName();
  if (!name.length()) name = "Pet";
  const uint32_t look = petLookBits();
  Record *me = myMember();
  if (!me) {
    add(data, MEMBER, name.c_str(), look);
    houseSave();
  } else if (strncmp(me->text, name.c_str(), TEXT) != 0 || me->a != look || (me->flags & DELETED)) {
    strlcpy(me->text, name.c_str(), sizeof me->text);
    me->a = look;
    me->flags &= ~DELETED;
    touch(data, *me);
    houseSave();
  }
}

void houseStart() {
  memset(&data, 0, sizeof data);
  data.me = petId();
  data.id = esp_random() | 1;
  houseUpdateMe();
}

void houseJoin(uint32_t id, uint32_t clock) {
  memset(&data, 0, sizeof data);
  data.me = petId();
  data.id = id;
  tick(data, clock);  // joining comes after everything the household has done so far
  houseUpdateMe();
}

// Only this device forgets it: the others keep this Pet in their rota until they remove it themselves.
void houseLeave() {
  memset(&data, 0, sizeof data);
  data.me = petId();
  houseSave();
  houseNewsSeen();
}

// ---- What this Pet thinks: chores that are this device's turn and late make it sulk; one done today, proud.

HouseMood houseMood() {
  HouseMood mood = {HouseFeeling::Normal, ""};
  if (!houseLoad() || !data.id) return mood;
  const uint32_t today = houseToday();
  if (!today) return mood;
  const Record *late = nullptr;
  bool doneToday = false;
  for (int i = 0; i < data.count; i++) {
    const Record &r = data.r[i];
    if (!live(r, CHORE)) continue;
    if (r.c == data.me && r.d == today) doneToday = true;
    if (overdue(r, today) && turnOf(data, r) == data.me && (!late || r.b < late->b)) late = &r;
  }
  if (late) {
    mood.feeling = HouseFeeling::Sulky;
    snprintf(mood.say, sizeof mood.say, "%s is late...", late->text);
  } else if (doneToday) {
    mood.feeling = HouseFeeling::Proud;
    snprintf(mood.say, sizeof mood.say, "We did our chores!");
  } else {
    const struct tm t = clockLocal();
    for (int i = 0; i < data.count; i++)
      if (live(data.r[i], REMINDER) && remindToday(data.r[i], t.tm_wday, t.tm_mday))
        snprintf(mood.say, sizeof mood.say, "%s today!", data.r[i].text);
  }
  return mood;
}

static void updateLine() {
  if (!data.id) {
    strlcpy(line, "Start or join a household", sizeof line);
    return;
  }
  const uint32_t today = houseToday();
  int mine = 0, lateMine = 0, shopping = 0;
  for (int i = 0; i < data.count; i++) {
    const Record &r = data.r[i];
    if (live(r, ITEM) && !r.a) shopping++;
    if (!live(r, CHORE) || turnOf(data, r) != data.me) continue;
    if (today && overdue(r, today)) lateMine++;
    else if (today && r.b <= today) mine++;
  }
  if (lateMine) snprintf(line, sizeof line, "%d chore%s late", lateMine, lateMine == 1 ? "" : "s");
  else if (mine) snprintf(line, sizeof line, "%d chore%s today", mine, mine == 1 ? "" : "s");
  else if (shopping) snprintf(line, sizeof line, "%d on the shopping list", shopping);
  else strlcpy(line, "All done", sizeof line);
}

void houseStatus(char *out, size_t len) { strlcpy(out, line[0] ? line : "Your household", len); }

// ---- News, kept in NVS until the Pet has passed it on.

void houseAddNews(const char *text) { storagePutString("house_news", text); }

bool houseNews(char *out, size_t len) {
  const String n = storageGetString("house_news");
  if (!n.length()) return false;
  strlcpy(out, n.c_str(), len);
  return true;
}

void houseNewsSeen() { storageRemoveKey("house_news"); }
