#include "storage.h"
#include <LittleFS.h>  // only for the one-time copy from internal flash
#include <Preferences.h>
#include <nvs_flash.h>
#include "board.h"
#include "devtools.h"
#include "usbsync.h"

#if UNIDEX_X4
// Xteink X4: the FreeInk SDK's SD card manager (SdFat on the panel's SPI bus, CS 12), as an fs::FS (sdkcard.cpp).
#include "sdkcard.h"
static fs::FS &CARD = sdkCard();
static bool cardBegin() { return sdkCardBegin(); }
static void cardEnd() { sdkCardEnd(); }
static uint64_t cardUsed() { return sdkCardUsed(); }
static uint64_t cardTotal() { return sdkCardTotal(); }
static const char *cardKind() { return "SD"; }
#else
#include <SD_MMC.h>
static fs::SDMMCFS &CARD = SD_MMC;
static const int SD_CLK = 39, SD_CMD = 41, SD_D0 = 40;  // Waveshare 04_SD_Card example + schematic
static bool cardBegin() {
  SD_MMC.setPins(SD_CLK, SD_CMD, SD_D0);
  return SD_MMC.begin("/sdcard", true /*1-bit*/, false /*never format: it would erase the card*/);
}
static void cardEnd() { SD_MMC.end(); }
static uint64_t cardUsed() { return SD_MMC.usedBytes(); }
static uint64_t cardTotal() { return SD_MMC.totalBytes(); }
static const char *cardKind() {
  static const char *TYPES[] = {"none", "MMC", "SD", "SDHC", "unknown"};
  const uint8_t type = SD_MMC.cardType();
  return TYPES[type < 4 ? type : 4];
}
#endif

static const char *NVS_NAMESPACE = "unidex";
static bool cardMounted;

// A power cut between the two renames in storageRename leaves only the old copy, under "<path>.old": put it back.
static void recover(const char *path) {
  if (CARD.exists(path)) return;
  const String old = String(path) + ".old";
  if (CARD.exists(old)) CARD.rename(old, path);
}

fs::File storageOpen(const char *path, const char *mode) {
  if (!storageCardMount()) return fs::File();
  if (*mode == 'r') recover(path);
  if (*mode != 'r') {  // FAT won't create a file in a missing folder (e.g. /badges on a new card)
    String dir = String(path).substring(0, String(path).lastIndexOf('/'));
    if (dir.length() && !CARD.exists(dir)) CARD.mkdir(dir);
  }
  return CARD.open(path, mode);
}

bool storageExists(const char *path) {
  if (!storageCardMount()) return false;
  recover(path);
  return CARD.exists(path);
}

// FAT won't rename onto an existing file. The old copy is moved aside first and removed only once the new one is in
// place, so at every moment one of them is there whole.
bool storageRename(const char *from, const char *to) {
  if (!storageCardMount() || !CARD.exists(from)) return false;
  const String old = String(to) + ".old";
  const bool had = CARD.exists(to);
  if (had) {
    if (CARD.exists(old)) CARD.remove(old);  // checked first: removing a missing file logs an error on USB
    if (!CARD.rename(to, old)) return false;
  }
  if (!CARD.rename(from, to)) {
    if (had) CARD.rename(old, to);
    return false;
  }
  if (had) CARD.remove(old);
  return true;
}

bool storageReplace(const char *tmp, const char *to, size_t bytes) {
  bool ok = storageCardMount();
  if (ok) {
    fs::File f = CARD.open(tmp);  // the size the card really has: a full card fails when the file is closed
    ok = f && !f.isDirectory() && f.size() == bytes;
  }
  ok = ok && storageRename(tmp, to);
  if (!ok) storageRemove(tmp);
  return ok;
}

bool storageReadLine(fs::File &f, String &line, size_t max) {
  line = "";
  if (!f || !f.available()) return false;
  for (int c; (c = f.read()) >= 0 && c != '\n';)
    if (line.length() < max) line += (char)c;
  return true;
}

bool storageRemove(const char *path) {
  // Checked first: removing a missing file would log an error onto the USB line.
  return storageCardMount() && CARD.exists(path) && CARD.remove(path);
}

// A read-only open of a namespace that doesn't exist yet (a new or fully erased board) prints an error line on
// the USB port, so the first read makes sure it exists.
static void ensureNamespace() {
  static bool done;
  if (done) return;
  Preferences prefs;
  prefs.begin(NVS_NAMESPACE, false);
  prefs.end();
  done = true;
}

int32_t storageGetInt(const char *key, int32_t fallback) {
  Preferences prefs;
  ensureNamespace();
  prefs.begin(NVS_NAMESPACE, true);
  int32_t v = prefs.getInt(key, fallback);
  prefs.end();
  return v;
}

void storagePutInt(const char *key, int32_t value) {
  Preferences prefs;
  prefs.begin(NVS_NAMESPACE, false);
  if (!prefs.isKey(key) || prefs.getInt(key) != value) prefs.putInt(key, value);
  prefs.end();
}

String storageGetString(const char *key, const char *fallback) {
  Preferences prefs;
  ensureNamespace();
  prefs.begin(NVS_NAMESPACE, true);
  // A missing key: no lookup, since getString() prints an error line on the USB port for one
  String v = prefs.isKey(key) ? prefs.getString(key, fallback) : String(fallback);
  prefs.end();
  return v;
}

void storagePutString(const char *key, const char *value) {
  Preferences prefs;
  prefs.begin(NVS_NAMESPACE, false);
  if (!prefs.isKey(key) || prefs.getString(key) != value) prefs.putString(key, value);
  prefs.end();
}

size_t storageGetBytes(const char *key, void *out, size_t len) {
  ensureNamespace();
  Preferences prefs;
  prefs.begin(NVS_NAMESPACE, true);
  size_t n = prefs.isKey(key) ? prefs.getBytesLength(key) : 0;
  if (n) n = n <= len ? prefs.getBytes(key, out, n) : 0;  // too big for `out`: an older or damaged block
  prefs.end();
  return n;
}

void storagePutBytes(const char *key, const void *data, size_t len) {
  Preferences prefs;
  prefs.begin(NVS_NAMESPACE, false);
  prefs.putBytes(key, data, len);
  prefs.end();
}

void storageRemoveKey(const char *key) {
  Preferences prefs;
  prefs.begin(NVS_NAMESPACE, false);
  prefs.remove(key);
  prefs.end();
}

void storageClearKeys() {
  Preferences prefs;
  prefs.begin(NVS_NAMESPACE, false);
  prefs.clear();
  prefs.end();
}

void storageEraseAll() {
  nvs_flash_deinit();
  nvs_flash_erase();  // the whole NVS partition: our two namespaces and the WiFi driver's saved network
}

bool storageUsage(uint64_t &used, uint64_t &total) {
  if (!storageCardMount()) return false;
  used = cardUsed();
  total = cardTotal();
  return true;
}

// Files used to live in internal flash (LittleFS). The first time a card mounts, copy any of them the
// card doesn't have yet (never overwriting), once. Each file is streamed and written in one go.
static void copyFile(const String &path) {
  if (CARD.exists(path) || !LittleFS.exists(path)) return;
  fs::File in = LittleFS.open(path, "r"), out = CARD.open(path, "w");
  uint8_t buf[512];
  for (size_t n; in && out && (n = in.read(buf, sizeof buf)) > 0;) out.write(buf, n);
}

static void copyFromFlashOnce() {
  if (storageGetInt("sd_copied", 0)) return;
  if (LittleFS.begin(false)) {
    copyFile("/timetable.csv");
    copyFile("/dex.csv");
    copyFile("/events.csv");
    if (!CARD.exists("/badges")) CARD.mkdir("/badges");
    fs::File dir = LittleFS.open("/badges");
    for (fs::File f = dir ? dir.openNextFile() : fs::File(); f; f = dir.openNextFile()) {
      String n = f.name();
      if (n.endsWith(".bmp")) copyFile("/badges/" + n);
    }
    LittleFS.end();
  }
  storagePutInt("sd_copied", 1);
}

bool storageCardMount() {
  if (devNoCard()) return false;  // test build switch (X NOCARD 1)
  if (cardMounted) return true;
  cardMounted = cardBegin();
  if (!cardMounted) {
    cardEnd();  // missing, not FAT, or broken
    return false;
  }
  copyFromFlashOnce();
  return true;
}

void storageEnd() {
  if (!cardMounted) return;
  cardEnd();
  cardMounted = false;
}

static void listCard(const char *dir) {
  fs::File d = CARD.open(dir);
  for (fs::File f = d ? d.openNextFile() : fs::File(); f; f = d.openNextFile()) {
    String path = String(dir) + (strcmp(dir, "/") ? "/" : "") + f.name();
    if (f.isDirectory()) {
      usbOut().printf("D %s\n", path.c_str());
      if (strcmp(dir, "/") == 0) listCard(path.c_str());  // one level down is enough (badges/)
    } else {
      usbOut().printf("F %s %u\n", path.c_str(), (unsigned)f.size());
    }
  }
}

void storageCardTest() {
  if (!storageCardMount()) {
    usbOut().println("OK S none (no card, or not FAT32)");
    return;
  }
  usbOut().printf("OK S ok %s %llu MB, %llu MB used\n", cardKind(), cardTotal() / (1024 * 1024),
                cardUsed() / (1024 * 1024));
  listCard("/");
  usbOut().println("OK S end");
}

void storageCardUnmount() {
  if (!cardMounted) return;
  cardEnd();
  cardMounted = false;
}
