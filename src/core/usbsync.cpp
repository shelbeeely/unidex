#include "usbsync.h"
#include "ota.h"
#include <Arduino.h>
#include <esp_rom_crc.h>
#include "clock.h"
#include "power.h"
#include "storage.h"
#include "../apps/notes/usb.h"
#include "../apps/pet/pet.h"
#include "../apps/pet/held.h"
#include "../apps/badge/badge_file.h"
#include "devtools.h"
#include <unidex_version.h>
#include "board.h"
#include "display.h"
#if UNIDEX_X4
#include "../apps/house/usb.h"
#include "../apps/house/secrets.h"
#endif

// Protocol (text lines, Mac -> device):
//   ?                      -> "roomiepet 1" on the X4, "unidex 1" on the Waveshare board: neither's tools work
//                             with the other
//   V                      -> "OK V <firmware version>" (from git, tools/version.py)
//   T <unix seconds>       -> sets the clock, "OK T"
//   C                      -> "OK C <clock chip registers and system time>" (diagnostics)
//   N ...                  -> Notes settings, tests and note download (src/apps/notes/usb.h)
//   P ...                  -> the Pet's look and name (src/apps/pet/pet.h)
//   I ...                  -> things a Pet can hold, on the X4 (src/apps/pet/held.h)
//   S                      -> SD card test: "OK S ...", "F <path> <bytes>" / "D <dir>" lines, "OK S end"
//   E <count> <crc32>      then <count> lines "YYYY-MM-DD,HH:MM,HH:MM,title,location"
//                          -> "OK E <crc>" or "ERR"; crc32 (zlib) covers each line plus '\n'
// Badge upload (tools/badge-maker.html over Web Serial):
//   L                      -> "F <name>" per badge, then "OK L"
//   B <name> <bytes> <crc32> -> "OK B" or "ERR"; then
//   D <hex>                -> "K" per line (max 64 bytes; the ack is the flow control), and after the
//                          last byte "OK F <name>" or "ERR". Written to a temp file, then renamed.
// Xteink X4 only (the web app, site/x4, over USB or the X4's hotspot):
//   G                      -> "OK G sleep=<s> invert=<0|1> time=<unix, 0 if unset> version=<v>" (the X4 adds
//                             "updates=<0|1>": take firmware updates from other X4s, ota.h)
//   G SET sleep <10|20|30|60> / G SET invert <0|1> / G SET updates <0|1> -> "OK G SET" or "ERR"
//   W GET                  -> "WL <line>" per line of /timetable.csv, then "OK W GET"
//   W <count> <crc32>      then <count> lines (as E): replaces /timetable.csv -> "OK W <crc>" or "ERR"
//   BX <name>              -> deletes a badge: "OK BX" or "ERR"
//   H ...                  -> the household (src/apps/house/usb.cpp)
static const char *EVENTS = "/events.csv", *EVENTS_TMP = "/events.tmp";
static const char *TIMETABLE = "/timetable.csv", *TIMETABLE_TMP = "/timetable.tmp";
static bool timetableUpload;  // the lines coming are for W (the timetable), not E (events)

// Replies go to the USB port, or to a web request's answer while usbRun() runs it.
struct Capture : Print {
  String text;
  size_t write(uint8_t c) override {
    text += (char)c;
    return 1;
  }
  size_t write(const uint8_t *b, size_t n) override {
    text.concat((const char *)b, n);
    return n;
  }
};
static Capture *capture;
Print &usbOut() { return capture ? (Print &)*capture : (Print &)Serial; }
static const char *BADGE_TMP = "/badges/upload.tmp";
static const uint32_t STALL_MS = 3000;  // give up on a transfer that stops halfway

static char line[600];  // fits "N SET <name> <hex>" for a 256-character key
static size_t len;
static int remaining;              // event lines still to come
static uint32_t expectedCrc, crc, lastLineAt, generation;
static size_t written;             // bytes written to the temp file (checked against the card at the end)
RTC_DATA_ATTR static time_t lastSync;
static bool unchanged;             // same crc as the saved file: check it, but don't rewrite flash
static fs::File out;

static char badgeName[32], newBadge[32];  // newBadge: finished upload, not yet shown
static int32_t badgeLeft;          // bytes still to come
static uint32_t badgeCrc, badgeExpected, badgeBytes;
static fs::File badgeOut;

static void abortBadge() {
  badgeLeft = 0;
  if (badgeOut) badgeOut.close();
  storageRemove(BADGE_TMP);
}

static int hexValue(char c) {
  return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
}

static void badgeData(const char *hex) {
  uint8_t buf[64];
  const size_t n = strlen(hex) / 2;
  bool ok = strlen(hex) % 2 == 0 && n > 0 && n <= sizeof buf && (int32_t)n <= badgeLeft;
  for (size_t i = 0; ok && i < n; i++) {
    int hi = hexValue(hex[2 * i]), lo = hexValue(hex[2 * i + 1]);
    ok = hi >= 0 && lo >= 0;
    buf[i] = hi << 4 | lo;
  }
  if (!ok || badgeOut.write(buf, n) != n) {
    abortBadge();
    usbOut().println("ERR");
    return;
  }
  badgeCrc = esp_rom_crc32_le(badgeCrc, buf, n);
  badgeLeft -= n;
  usbOut().println("K");
  if (badgeLeft > 0) return;
  badgeOut.close();
  if (badgeCrc != badgeExpected) {
    storageRemove(BADGE_TMP);
    usbOut().println("ERR");
    return;
  }
  if (!storageReplace(BADGE_TMP, (String("/badges/") + badgeName).c_str(), badgeBytes)) {
    usbOut().println("ERR");
    return;
  }
  strlcpy(newBadge, badgeName, sizeof newBadge);
  usbOut().printf("OK F %s\n", badgeName);
}

static void listBadges() {
  fs::File dir = storageOpen("/badges");
  for (fs::File f = dir ? dir.openNextFile() : fs::File(); f; f = dir.openNextFile()) {
    String n = f.name();
    if (n.endsWith(".bmp") && !n.startsWith(".")) usbOut().printf("F %s\n", n.c_str());
  }
  usbOut().println("OK L");
}

static void finishEvents() {
  if (out) out.close();
  if (timetableUpload) {  // W: the timetable
    timetableUpload = false;
    if (crc != expectedCrc || !storageReplace(TIMETABLE_TMP, TIMETABLE, written)) {
      storageRemove(TIMETABLE_TMP);
      usbOut().println("ERR");
      return;
    }
    generation++;  // the Timetable app loads it again
    usbOut().printf("OK W %lu\n", (unsigned long)crc);
    return;
  }
  if (crc != expectedCrc) {
    storageRemove(EVENTS_TMP);
    usbOut().println("ERR");
    return;
  }
  if (!unchanged) {
    if (!storageReplace(EVENTS_TMP, EVENTS, written)) {  // no card, or it's full: the saved calendar stays
      usbOut().println("ERR");
      return;
    }
    storagePutInt("events_crc", (int32_t)crc);
    generation++;
  }
#if DEBUG
  usbOut().println(unchanged ? "events unchanged" : "events saved");
#endif
  lastSync = time(nullptr);
  usbOut().printf("OK E %lu\n", (unsigned long)crc);
}

static void handle(const char *l) {
  if (badgeLeft > 0 && l[0] == 'D' && l[1] == ' ') {
    badgeData(l + 2);
    return;
  }
  if (remaining > 0) {
    crc = esp_rom_crc32_le(crc, (const uint8_t *)l, strlen(l));
    crc = esp_rom_crc32_le(crc, (const uint8_t *)"\n", 1);
    if (out) written += out.println(l);
    if (--remaining == 0) finishEvents();
    return;
  }
#if UNIDEX_DEV
  if (devUsb(l)) return;  // test build: screenshots and virtual buttons
#endif
  if (strcmp(l, "?") == 0) {
    usbOut().println(UNIDEX_X4 ? "roomiepet 1" : "unidex 1");
  } else if (strcmp(l, "V") == 0) {
    usbOut().printf("OK V %s\n", UNIDEX_VERSION);
  } else if (strcmp(l, "C") == 0) {
    char status[96];
    clockStatus(status, sizeof status);
    usbOut().printf("OK C %s\n", status);
  } else if (notesUsb(l)) {
    // Notes setup and note download: "N ..." (src/apps/notes/usb.h)
  } else if (petUsb(l)) {
    // the Pet's look and name: "P ..." (src/apps/pet/pet.h)
  } else if (itemsUsb(l)) {
    // things a Pet can hold, on the X4: "I ..." (src/apps/pet/held.h)
  } else if (strcmp(l, "S") == 0) {
    storageCardTest();
  } else if (strcmp(l, "L") == 0) {
    listBadges();
  } else if (l[0] == 'B' && l[1] == ' ') {
    long bytes = 0;
    unsigned long crc32 = 0;
    char name[40] = "";
    abortBadge();  // a new upload replaces any unfinished one
    bool ok = sscanf(l + 2, "%39s %ld %lu", name, &bytes, &crc32) == 3 && badgeNameOk(name) &&
              bytes > 0 && bytes <= (long)BADGE_MAX_BYTES;
    if (ok) badgeOut = storageOpen(BADGE_TMP, "w");
    if (!ok || !badgeOut) {
      usbOut().println("ERR");
      return;
    }
    strlcpy(badgeName, name, sizeof badgeName);
    badgeLeft = badgeBytes = bytes;
    badgeCrc = 0;
    badgeExpected = crc32;
    usbOut().println("OK B");
  } else if (l[0] == 'T' && l[1] == ' ') {
    clockSet((time_t)strtoll(l + 2, nullptr, 10));
    usbOut().println("OK T");
#if UNIDEX_X4
  } else if (secretsUsb(l)) {
    // the household key and shared WiFi: "KEY ...", "WIFI ..." (src/apps/house/secrets.h)
  } else if (houseUsb(l)) {
    // the household: "H ..." (src/apps/house/usb.h)
  } else if (strcmp(l, "G") == 0) {
    usbOut().printf("OK G sleep=%d invert=%d time=%ld version=%s updates=%d\n", powerSleepSeconds(),
                    display.inverted ? 1 : 0, clockValid() ? (long)time(nullptr) : 0L, UNIDEX_VERSION, otaEnabled());
  } else if (strncmp(l, "G SET ", 6) == 0) {
    char key[12] = "";
    int value = -1;
    if (sscanf(l + 6, "%11s %d", key, &value) == 2 && strcmp(key, "sleep") == 0 &&
        (value == 10 || value == 20 || value == 30 || value == 60)) {
      powerSetSleepSeconds(value);
      usbOut().println("OK G SET");
    } else if (strcmp(key, "invert") == 0 && (value == 0 || value == 1)) {
      displaySetInverted(value);
      usbOut().println("OK G SET");
    } else if (strcmp(key, "updates") == 0 && (value == 0 || value == 1)) {
      otaSetEnabled(value);
      usbOut().println("OK G SET");
    } else {
      usbOut().println("ERR");
    }
  } else if (strcmp(l, "W GET") == 0) {
    fs::File f = storageOpen(TIMETABLE, "r");
    String row;
    while (f && storageReadLine(f, row)) usbOut().printf("WL %s\n", row.c_str());
    if (f) f.close();
    usbOut().println("OK W GET");
  } else if (l[0] == 'W' && l[1] == ' ') {
    char *end;
    remaining = strtol(l + 2, &end, 10);
    expectedCrc = strtoul(end, nullptr, 10);
    crc = 0;
    written = 0;
    timetableUpload = true;
    out = storageOpen(TIMETABLE_TMP, "w");
    if (remaining <= 0 || !out) {
      remaining = 0;
      if (out) out.close();
      timetableUpload = false;
      usbOut().println("ERR");
    }
  } else if (strncmp(l, "BX ", 3) == 0) {
    const bool ok = badgeNameOk(l + 3) && storageRemove((String("/badges/") + (l + 3)).c_str());
    usbOut().println(ok ? "OK BX" : "ERR");
#endif
  } else if (l[0] == 'E' && l[1] == ' ') {
    char *end;
    remaining = strtol(l + 2, &end, 10);
    expectedCrc = strtoul(end, nullptr, 10);
    crc = 0;
    written = 0;
    unchanged = (uint32_t)storageGetInt("events_crc", 0) == expectedCrc && storageExists(EVENTS);
    if (!unchanged) out = storageOpen(EVENTS_TMP, "w");  // one file, written once, then renamed
    if (remaining <= 0) {
      remaining = 0;
      finishEvents();
    }
  }
}

void usbSyncPoll() {
  if (remaining > 0 && millis() - lastLineAt > STALL_MS) {
    remaining = 0;
    if (out) out.close();
    storageRemove(EVENTS_TMP);
  }
  if (badgeLeft > 0 && millis() - lastLineAt > STALL_MS) abortBadge();
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\r') continue;
    if (c != '\n') {
      if (len < sizeof line - 1) line[len++] = c;
      continue;
    }
    line[len] = 0;
    len = 0;
    lastLineAt = millis();
    powerActivity();  // don't fall asleep mid-transfer
    handle(line);
    // Send the reply now: after a burst of output a short line can sit in the USB buffer. Asking whether the
    // host is there nudges the data out by itself; flush() only waits once the host has read from us before
    // (on a fresh connection it would throw the reply away instead).
    if (Serial) Serial.flush();
  }
}

String usbRun(const char *lines) {
  Capture c;
  capture = &c;
  for (const char *at = lines; *at;) {
    const char *nl = strchr(at, '\n');
    const size_t n = nl ? (size_t)(nl - at) : strlen(at);
    char one[sizeof line];
    const size_t k = n < sizeof one - 1 ? n : sizeof one - 1;
    memcpy(one, at, k);
    one[k] = 0;
    if (k && one[k - 1] == '\r') one[k - 1] = 0;
    lastLineAt = millis();
    handle(one);
    at += n + (nl ? 1 : 0);
  }
  capture = nullptr;
  powerActivity();
  return c.text;
}

uint32_t usbSyncGeneration() {
  return generation;
}

time_t usbSyncLastTime() {
  return lastSync;
}

bool usbSyncTakeNewBadge(char *out, size_t len) {
  if (!*newBadge) return false;
  strlcpy(out, newBadge, len);
  *newBadge = 0;
  return true;
}
