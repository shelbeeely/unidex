// house.wasm: the X4's household rules (src/apps/house/house_logic.h, the very same code) for the web app (site/x4),
// plus the badge converter. Built by tools/wasm/build.sh with clang alone (no Emscripten, no libc beyond libc/).
//
// The web app reads the household from the X4 ("H DUMP"), loads it here, makes changes as that device would (as its
// Pet, on its Lamport clock) and sends back only the records it changed ("H PUT", then "H SAVE"), which the X4 merges
// exactly as it merges a roommate's at Sync. So the web app and the X4 can never disagree about who owes whom or whose
// turn a chore is.
//
// Strings go in and out through io(): JS writes text there (NUL-terminated) before a call, and calls that answer
// write JSON or command lines there and return its length. Records are named by (origin, seq).
#include "house_logic.h"

using namespace house;

#define EXPORT extern "C" __attribute__((visibility("default")))

static House h;
static uint32_t loadedClock;  // records stamped above this were changed here
static char io_[64 * 1024];
static const size_t IMAGE = 200;
static uint8_t image_[IMAGE * IMAGE * 4];  // RGBA in for the badge converter
static uint8_t bmp_[62 + 28 * IMAGE];       // the badge out (200 px rows are 28 bytes in a BMP)

EXPORT char *io() { return io_; }
EXPORT uint8_t *image() { return image_; }
EXPORT uint8_t *bmp() { return bmp_; }

// ---- JSON out

struct Json {
  size_t n = 0;
  void ch(char c) {
    if (n + 1 < sizeof io_) io_[n++] = c;
  }
  void raw(const char *s) {
    while (*s) ch(*s++);
  }
  void str(const char *s) {
    ch('"');
    for (; *s; s++) {
      if (*s == '"' || *s == '\\') ch('\\');
      if ((unsigned char)*s >= 0x20) ch(*s);
    }
    ch('"');
  }
  void num(long v) {
    char b[24];
    snprintf(b, sizeof b, "%ld", v);
    raw(b);
  }
  void key(const char *k) {
    if (n && io_[n - 1] != '{' && io_[n - 1] != '[') ch(',');
    str(k);
    ch(':');
  }
  void item() {
    if (n && io_[n - 1] != '[') ch(',');
  }
  size_t done() {
    io_[n] = 0;
    return n;
  }
};

static int hexValue(char c) {
  return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
}

static Record *find(uint32_t origin, uint32_t seq) {
  for (int i = 0; i < h.count; i++)
    if (h.r[i].origin == origin && h.r[i].seq == seq) return &h.r[i];
  return nullptr;
}

// ---- Loading what "H DUMP" printed

EXPORT void load_begin(uint32_t id, uint32_t me, uint32_t clock, uint32_t nextSeq) {
  memset(&h, 0, sizeof h);
  h.id = id, h.me = me, h.clock = clock, h.nextSeq = nextSeq;
  loadedClock = clock;
}

// One "HR" line's hex (in io). False if it isn't a record.
EXPORT int load_record() {
  uint8_t rec[RECORD_LEN];
  if (strlen(io_) != 2 * RECORD_LEN) return 0;
  for (size_t k = 0; k < RECORD_LEN; k++) {
    const int hi = hexValue(io_[2 * k]), lo = hexValue(io_[2 * k + 1]);
    if (hi < 0 || lo < 0) return 0;
    rec[k] = hi << 4 | lo;
  }
  Record r;
  if (!decodeRecord(rec, r) || h.count >= (int)MAX_RECORDS) return 0;
  h.r[h.count++] = r;  // as the device has it (not merged: no clock change)
  return 1;
}

// ---- The household as JSON, for today (local days since 1970) and its weekday (0 Sunday) and day of the month.

EXPORT size_t state(uint32_t today, int wday, int mday) {
  Json j;
  j.ch('{');
  j.key("id"), j.num(h.id);
  j.key("me"), j.num(h.me);
  const Record *m[MAX_MEMBERS];
  const int n = members(h, m);
  int32_t bal[MAX_MEMBERS];
  balances(h, m, n, bal);
  Debt d[MAX_MEMBERS];
  const int nd = settleUp(bal, n, d, MAX_MEMBERS);
  j.key("members"), j.ch('[');
  for (int i = 0; i < n; i++) {
    j.item(), j.ch('{');
    j.key("id"), j.num(m[i]->origin);
    j.key("name"), j.str(m[i]->text);
    char pr[pronouns::MAX + 1];
    pronounsOf(*m[i], pr);
    j.key("pronouns"), j.str(pr);
    j.key("flag"), j.num(m[i]->d & 0xFF);
    j.key("held"), j.num(m[i]->d >> 8 & 0xFF);
    j.key("look"), j.num(m[i]->a);
    j.key("balance"), j.num(bal[i]);
    const char *tag = payTagOf(h, m[i]->origin);
    j.key("tag"), tag ? j.str(tag) : j.raw("null");
    // Their request for this device's Pet to pay (pasted on their phone page), if any.
    if (const Record *req = activeRequest(h, m[i]->origin, h.me)) {
      char url[64];
      requestLinkUrl(req->text, url, sizeof url);
      j.key("request"), j.ch('{'), j.key("url"), j.str(url), j.key("pence"), j.num(req->a), j.ch('}');
    }
    // This device's own request for them to pay (what Share sends them).
    if (m[i]->origin != h.me)
      if (const Record *mine = activeRequest(h, h.me, m[i]->origin)) {
        char url[64];
        requestLinkUrl(mine->text, url, sizeof url);
        j.key("myRequest"), j.ch('{'), j.key("url"), j.str(url), j.key("pence"), j.num(mine->a), j.ch('}');
      }
    j.ch('}');
  }
  j.ch(']');
  j.key("debts"), j.ch('[');
  for (int i = 0; i < nd; i++) {
    j.item(), j.ch('{');
    j.key("from"), j.num(m[d[i].from]->origin);
    j.key("to"), j.num(m[d[i].to]->origin);
    j.key("pence"), j.num(d[i].pence);
    j.ch('}');
  }
  j.ch(']');
  j.key("records"), j.ch('[');
  for (int i = 0; i < h.count; i++) {
    const Record &r = h.r[i];
    if ((r.flags & DELETED) || r.kind == MEMBER || r.kind == PAYTAG) continue;
    if (r.kind == PAYLINK && r.origin != h.me) continue;  // shown through members' "request"
    j.item(), j.ch('{');
    j.key("kind"), j.num(r.kind);
    j.key("origin"), j.num(r.origin);
    j.key("seq"), j.num(r.seq);
    j.key("born"), j.num(r.born);
    j.key("text"), j.str(r.text);
    j.key("a"), j.num(r.a), j.key("b"), j.num(r.b), j.key("c"), j.num(r.c), j.key("d"), j.num(r.d);
    if (r.kind == CHORE) {
      j.key("turn"), j.num(turnOf(h, r));
      j.key("overdue"), j.raw(today && overdue(r, today) ? "true" : "false");
    } else if (r.kind == REMINDER) {
      j.key("today"), j.raw(remindToday(r, wday, mday) ? "true" : "false");
    } else if (r.kind == PAYLINK) {
      char url[64];
      requestLinkUrl(r.text, url, sizeof url);
      j.key("url"), j.str(url);
    }
    j.ch('}');
  }
  j.ch(']');
  j.ch('}');
  return j.done();
}

// ---- Changes, made as this device. Text arguments are in io. Each returns 1 when it changed something.

static const char *text() { return io_; }

EXPORT int add_chore(uint32_t everyDays, uint32_t due) {
  return add(h, CHORE, text(), everyDays ? everyDays : 1, due) != nullptr;
}
EXPORT int chore_done(uint32_t origin, uint32_t seq, uint32_t today) {
  Record *r = find(origin, seq);
  if (!r || !live(*r, CHORE) || !today) return 0;
  choreDone(h, *r, today);
  return 1;
}
// Change a chore's name, how often and next due day.
EXPORT int edit_chore(uint32_t origin, uint32_t seq, uint32_t everyDays, uint32_t due) {
  Record *r = find(origin, seq);
  if (!r || !live(*r, CHORE)) return 0;
  memset(r->text, 0, sizeof r->text);
  strncpy(r->text, text(), TEXT);
  r->a = everyDays ? everyDays : 1;
  r->b = due;
  touch(h, *r);
  return 1;
}
EXPORT int add_item() { return add(h, ITEM, text()) != nullptr; }
EXPORT int toggle_item(uint32_t origin, uint32_t seq) {
  Record *r = find(origin, seq);
  if (!r || !live(*r, ITEM)) return 0;
  r->a = !r->a;
  touch(h, *r);
  return 1;
}
EXPORT int remove_record(uint32_t origin, uint32_t seq) {
  Record *r = find(origin, seq);
  if (!r || (r->flags & DELETED) || (ownerOnly(r->kind) && r->origin != h.me)) return 0;
  remove(h, *r);
  return 1;
}
EXPORT int add_expense(uint32_t pence) { return pence && add(h, EXPENSE, text(), h.me, pence) != nullptr; }
EXPORT int settle(uint32_t from, uint32_t to, uint32_t pence) {
  return pence && from != to && add(h, SETTLE, nullptr, from, pence, to) != nullptr;
}
EXPORT int add_reminder(uint32_t monthly, uint32_t day) { return add(h, REMINDER, text(), monthly ? 1 : 0, day) != nullptr; }
EXPORT int add_message() { return add(h, MESSAGE, text()) != nullptr; }
EXPORT int set_paytag() { return setPayTag(h, text()); }
// A pasted Cash App request (the link, or Cash App's whole share text). pence 0: read "$15" from the text.
EXPORT int add_request(uint32_t pence, uint32_t forWhom) {
  char code[TEXT + 1];
  if (!parseRequestLink(text(), code, sizeof code)) return 0;
  if (!pence) pence = parseAmount(text());
  if (forWhom == h.me || (forWhom && !member(h, forWhom))) forWhom = 0;
  return addRequestLink(h, code, pence, forWhom) != nullptr;
}
EXPORT int valid_cashtag() { return validCashtag(text()); }

// The commands that send this device the records changed here: "H PUT <hex>" lines, then "H SAVE".
EXPORT size_t changes() {
  size_t n = 0;
  uint8_t rec[RECORD_LEN];
  for (int i = 0; i < h.count; i++) {
    if (h.r[i].ver <= loadedClock) continue;
    encodeRecord(h.r[i], rec);
    if (n + 6 + 2 * RECORD_LEN + 2 >= sizeof io_) break;
    memcpy(io_ + n, "H PUT ", 6), n += 6;
    for (size_t k = 0; k < RECORD_LEN; k++) {
      static const char HEX[] = "0123456789abcdef";
      io_[n++] = HEX[rec[k] >> 4], io_[n++] = HEX[rec[k] & 15];
    }
    io_[n++] = '\n';
  }
  if (n) memcpy(io_ + n, "H SAVE\n", 7), n += 7;
  io_[n] = 0;
  return n;
}
EXPORT int changed_count() {
  int n = 0;
  for (int i = 0; i < h.count; i++) n += h.r[i].ver > loadedClock;
  return n;
}
// After the X4 took them: what it has now is the new starting point.
EXPORT void sent() { loadedClock = h.clock; }

// ---- Badges: 200x200 RGBA in image() -> a 1-bit BMP in bmp() (its length returned), as the website's badge maker
// does it (src/apps/badge/badge_file.h): line art keeps its own black and white, photos get a contrast stretch and
// Floyd-Steinberg dithering. light: added to every grey (-100..100); style: 0 auto, 1 line art, 2 photo.

static float gray_[IMAGE * IMAGE];

EXPORT size_t badge(int light, int invert, int style) {
  const size_t N = IMAGE * IMAGE;
  int mid = 0;
  for (size_t i = 0; i < N; i++) {
    float v = 0.299f * image_[4 * i] + 0.587f * image_[4 * i + 1] + 0.114f * image_[4 * i + 2];
    const float a = image_[4 * i + 3] / 255.0f;  // transparent = white
    v = v * a + 255 * (1 - a) + light;
    v = v < 0 ? 0 : v > 255 ? 255 : v;
    gray_[i] = invert ? 255 - v : v;
    mid += gray_[i] >= 32 && gray_[i] < 224;
  }
  const bool photo = style == 2 || (style == 0 && mid > (int)(0.15f * N));
  if (photo) {  // stretch: like Pillow's autocontrast(cutoff=1)
    int hist[256] = {};
    for (size_t i = 0; i < N; i++) hist[(int)gray_[i]]++;
    const int cut = (int)(N * 0.01f);
    int lo = 0, hi = 255, acc = 0;
    for (; lo < 255; lo++)
      if ((acc += hist[lo]) > cut) break;
    for (acc = 0; hi > 0; hi--)
      if ((acc += hist[hi]) > cut) break;
    if (hi > lo)
      for (size_t i = 0; i < N; i++) {
        const float v = (gray_[i] - lo) * 255 / (hi - lo);
        gray_[i] = v < 0 ? 0 : v > 255 ? 255 : v;
      }
  }
  const size_t stride = 28, offset = 62, pixels = stride * IMAGE, total = offset + pixels;
  memset(bmp_, 0, total);
  auto put16 = [](uint8_t *p, uint32_t v) { p[0] = v, p[1] = v >> 8; };
  auto put32 = [](uint8_t *p, uint32_t v) { p[0] = v, p[1] = v >> 8, p[2] = v >> 16, p[3] = v >> 24; };
  bmp_[0] = 'B', bmp_[1] = 'M';
  put32(bmp_ + 2, total), put32(bmp_ + 10, offset), put32(bmp_ + 14, 40);
  put32(bmp_ + 18, IMAGE), put32(bmp_ + 22, IMAGE), put16(bmp_ + 26, 1), put16(bmp_ + 28, 1);
  put32(bmp_ + 34, pixels), put32(bmp_ + 38, 3780), put32(bmp_ + 42, 3780), put32(bmp_ + 46, 2), put32(bmp_ + 50, 2);
  bmp_[58] = bmp_[59] = bmp_[60] = 255;  // palette: 0 black, 1 white
  for (size_t y = 0; y < IMAGE; y++)
    for (size_t x = 0; x < IMAGE; x++) {
      const size_t i = y * IMAGE + x;
      const float old = gray_[i], nv = old >= 128 ? 255 : 0, err = photo ? old - nv : 0;
      if (nv) bmp_[offset + (IMAGE - 1 - y) * stride + (x >> 3)] |= 0x80 >> (x & 7);  // rows bottom-up
      if (!photo) continue;
      if (x + 1 < IMAGE) gray_[i + 1] += err * 7 / 16;
      if (y + 1 < IMAGE) {
        if (x > 0) gray_[i + IMAGE - 1] += err * 3 / 16;
        gray_[i + IMAGE] += err * 5 / 16;
        if (x + 1 < IMAGE) gray_[i + IMAGE + 1] += err * 1 / 16;
      }
    }
  return total;
}
