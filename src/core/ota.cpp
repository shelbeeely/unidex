// Firmware updates between X4s over the radio (ota.h, ota_logic.h).
#include "ota.h"
#include <Arduino.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <esp_random.h>
#include <unidex_version.h>
#include "battery.h"
#include "power.h"
#include "storage.h"

#if UNIDEX_X4
// Arduino would keep a new release as soon as it starts; otaBoot keeps it once setup() has got through, so one
// that can't start goes back to the one before (the bootloader's rollback).
extern "C" bool verifyRollbackLater() { return true; }
#endif

#if UNIDEX_X4 && !UNIDEX_DEV
#include "ota_crypto.h"
#include "ota_key.h"
#define OTA_ON OTA_KEY_SET
#else
#define OTA_ON 0
#endif

#if OTA_ON

// What tools/ota/sign.py reads from the image: this build's release number and version.
#define OTA_STR_(x) #x
#define OTA_STR(x) OTA_STR_(x)
extern "C" __attribute__((used)) const char OTA_MARK[] = "RPOTA:" OTA_STR(UNIDEX_OTA_SEQ) ":" UNIDEX_VERSION ";";
static const uint32_t MY_SEQ = UNIDEX_OTA_SEQ;
static const uint32_t OFFER_MS = 2000, WANT_MS = 300, SOURCE_GONE_MS = 8000, VERIFY_GAP_MS = 3000;
static const uint32_t SAVE_EVERY = 64 * 1024;  // progress saved this often (NVS wear)

// NVS: "ota_prog" (trailer + bytes done) while downloading, "ota_ready" (trailer) once checked, "ota_tried" (the
// release number of an update being installed) and "ota_bad" (one that didn't start, never taken again).
struct Progress {
  uint8_t trailer[ota::TRAILER];
  uint32_t done, part;  // part: the partition's address (a USB update in between can change which one is free)
};

static bool mineChecked, shareable;  // this X4's own release: signed, whole, and so offered
static ota::Manifest mine;
static ota::Download *dl;            // a download under way, or nullptr
static const esp_partition_t *target;
static uint8_t source[6], banned[6];
static uint32_t heardSourceAt, wantAt, offerAt, verifiedAt, bannedUntil, savedDone;
static uint8_t verified[ota::TRAILER];  // the last offer whose signature checked out

static bool readPart(const esp_partition_t *p, uint32_t at, uint8_t *buf, size_t n) {
  return esp_partition_read(p, at, buf, n) == ESP_OK;
}

static bool shaOf(const esp_partition_t *p, uint32_t size, uint8_t out[32]) {
  static uint8_t buf[1024];
  ota::Sha256 h;
  for (uint32_t at = 0; at < size; at += sizeof buf) {
    const size_t n = size - at < sizeof buf ? size - at : sizeof buf;
    if (!readPart(p, at, buf, n)) return false;
    h.add(buf, n);
    if ((at & 0x3FFFF) == 0) powerAlive();
  }
  h.done(out);
  return true;
}

// This X4's release, if signed: the trailer after the running image, checked as another X4 would check it.
static void checkMine() {
  mineChecked = true;
  const esp_partition_t *run = esp_ota_get_running_partition();
  const uint32_t len = ota::imageLength([&](uint32_t at, uint8_t *b, size_t n) { return readPart(run, at, b, n); },
                                        run->size - ota::TRAILER);
  uint8_t t[ota::TRAILER], sha[32];
  shareable = len && readPart(run, len, t, sizeof t) && ota::parseTrailer(t, mine) && mine.seq == MY_SEQ &&
              mine.size == len && ota::verify(mine, OTA_PUBLIC_KEY) && shaOf(run, len, sha) &&
              memcmp(sha, mine.sha, 32) == 0;
}

static void clearDownload(bool forget) {
  delete dl;
  dl = nullptr;
  if (forget) storageRemoveKey("ota_prog");
}

static void saveProgress() {
  Progress p;
  ota::packTrailer(dl->m, p.trailer);
  p.done = dl->done;
  p.part = target->address;
  storagePutBytes("ota_prog", &p, sizeof p);
  savedDone = dl->done;
}

static bool readyManifest(ota::Manifest &m) {
  uint8_t t[ota::TRAILER];
  return storageGetBytes("ota_ready", t, sizeof t) == sizeof t && ota::parseTrailer(t, m) && m.seq > MY_SEQ;
}

static void finish() {
  uint8_t sha[32];
  const bool ok = shaOf(target, dl->m.size, sha) && memcmp(sha, dl->m.sha, 32) == 0 &&
                  dl->writeTrailer([](uint32_t s) { return esp_partition_erase_range(target, s, ota::SECTOR) == ESP_OK; },
                                   [](uint32_t at, const uint8_t *b, uint32_t n) {
                                     return esp_partition_write(target, at, b, n) == ESP_OK;
                                   });
  if (ok) {
    uint8_t t[ota::TRAILER];
    ota::packTrailer(dl->m, t);
    storagePutBytes("ota_ready", t, sizeof t);
  } else {  // a source sending the wrong bytes: start again, from someone else for a while
    memcpy(banned, source, 6);
    bannedUntil = millis() + 600000;
  }
  clearDownload(true);
}

static void heardOffer(const LinkPacket &p) {
  ota::Manifest m;
  if (!ota::decodeOffer(p.data, p.len, m) || m.seq <= MY_SEQ || !otaEnabled()) return;
  if ((uint32_t)storageGetInt("ota_bad", 0) == m.seq) return;  // it didn't start here before
  ota::Manifest ready;
  if (readyManifest(ready) && ready.seq >= m.seq) return;
  const uint32_t now = millis();
  if (dl && memcmp(dl->m.sha, m.sha, 32) == 0 && dl->m.seq == m.seq) {  // the one we're getting: keep a source
    if (!memcmp(source, p.mac, 6) || now - heardSourceAt > SOURCE_GONE_MS / 2) memcpy(source, p.mac, 6), heardSourceAt = now;
    return;
  }
  if (dl && dl->m.seq >= m.seq) return;
  if ((int32_t)(now - bannedUntil) < 0 && !memcmp(banned, p.mac, 6)) return;
  if (batteryTooLow()) return;
  target = esp_ota_get_next_update_partition(nullptr);
  if (!target || target == esp_ota_get_running_partition() || m.size + ota::TRAILER > target->size) return;
  // The signature, at most once every few seconds (anyone can send offers; checking one takes a moment).
  uint8_t t[ota::TRAILER];
  ota::packTrailer(m, t);
  if (memcmp(t, verified, sizeof t) != 0) {
    if (now - verifiedAt < VERIFY_GAP_MS) return;
    verifiedAt = now;
    if (!ota::verify(m, OTA_PUBLIC_KEY)) return;
    memcpy(verified, t, sizeof t);
  }
  clearDownload(false);
  storageRemoveKey("ota_ready");  // the partition is about to be written: an older update waiting there is gone
  dl = new ota::Download();
  Progress saved;
  const bool resume = storageGetBytes("ota_prog", &saved, sizeof saved) == sizeof saved &&
                      memcmp(saved.trailer, t, sizeof t) == 0 && saved.part == target->address;
  dl->begin(m, resume ? saved.done : 0);
  savedDone = dl->done;
  memcpy(source, p.mac, 6);
  heardSourceAt = now, wantAt = 0;
}

static void answerWant(const LinkPacket &p) {
  uint8_t sha8[8], count;
  uint32_t offset;
  if (!shareable || !ota::decodeWant(p.data, p.len, sha8, offset, count) || memcmp(sha8, mine.sha, 8) != 0) return;
  const esp_partition_t *run = esp_ota_get_running_partition();
  uint8_t chunk[ota::CHUNK], pkt[250];
  for (int i = 0; i < count; i++) {
    const uint32_t at = offset + i * ota::CHUNK;
    if (at >= mine.size) break;
    const size_t n = mine.size - at < ota::CHUNK ? mine.size - at : ota::CHUNK;
    if (!readPart(run, at, chunk, n) || !linkSend(p.mac, pkt, ota::encodeData(mine.sha, at, chunk, n, pkt))) break;
  }
}

static void takeData(const LinkPacket &p) {
  uint8_t sha8[8];
  uint32_t offset;
  const uint8_t *bytes;
  size_t n;
  if (!dl || memcmp(source, p.mac, 6) || !ota::decodeData(p.data, p.len, sha8, offset, bytes, n) ||
      memcmp(sha8, dl->m.sha, 8) || !dl->data(offset, bytes, n))
    return;
  heardSourceAt = millis();
  if (!dl->windowFull()) return;
  const bool ok = dl->flush([](uint32_t s) { return esp_partition_erase_range(target, s, ota::SECTOR) == ESP_OK; },
                            [](uint32_t at, const uint8_t *b, uint32_t len) {
                              return esp_partition_write(target, at, b, len) == ESP_OK;
                            });
  if (!ok) return clearDownload(true);
  wantAt = 0;  // the next window straight away
  if (dl->finished()) return finish();
  if (dl->done - savedDone >= SAVE_EVERY) saveProgress();
}

bool otaHeard(const LinkPacket &p) {
  const uint8_t t = ota::typeOf(p.data, p.len);
  if (!t) return false;
  if (t == ota::OFFER) heardOffer(p);
  else if (t == ota::WANT) answerWant(p);
  else if (t == ota::DATA) takeData(p);
  return true;
}

void otaTick() {
  if (!linkOn()) return;
  if (!mineChecked) checkMine();
  const uint32_t now = millis();
  if (shareable && (int32_t)(now - offerAt) >= 0) {
    uint8_t pkt[250];
    linkBroadcast(pkt, ota::encodeOffer(mine, pkt));
    offerAt = now + OFFER_MS - 200 + esp_random() % 400;
  }
  if (!dl) return;
  if (now - heardSourceAt > SOURCE_GONE_MS) {  // out of reach: keep what came, carry on next time
    saveProgress();
    return clearDownload(false);
  }
  uint32_t offset;
  uint8_t count;
  if ((int32_t)(now - wantAt) >= 0 && dl->want(offset, count)) {
    uint8_t pkt[32];
    linkSend(source, pkt, ota::encodeWant(dl->m.sha, offset, count, pkt));
    wantAt = now + WANT_MS;
  }
}

void otaInstallIfReady() {
  ota::Manifest m;
  if (!readyManifest(m) || (uint32_t)storageGetInt("ota_bad", 0) == m.seq) return;
  const esp_partition_t *next = esp_ota_get_next_update_partition(nullptr);
  uint8_t sha[32], t[ota::TRAILER];
  ota::Manifest there;
  // Once more, just before: the trailer and every byte as checked (nothing has written there since, but this is
  // the last chance not to boot something else).
  if (!next || !readPart(next, m.size, t, sizeof t) || !ota::parseTrailer(t, there) || there.seq != m.seq ||
      !ota::verify(there, OTA_PUBLIC_KEY) || !shaOf(next, m.size, sha) || memcmp(sha, m.sha, 32) != 0 ||
      esp_ota_set_boot_partition(next) != ESP_OK) {
    storageRemoveKey("ota_ready");
    return;
  }
  storagePutInt("ota_tried", m.seq);
  esp_restart();  // not a sleep: the new firmware starts clean (memory kept through sleep is the old one's)
}

void otaBoot() {
  esp_ota_mark_app_valid_cancel_rollback();  // it got through setup(): keep it
  (void)*(volatile const char *)OTA_MARK;     // keeps the marker in the image (the linker drops what's unused)
  const uint32_t tried = storageGetInt("ota_tried", 0);
  if (tried) {
    if (tried > MY_SEQ) storagePutInt("ota_bad", tried);  // back on the old one: that update didn't start
    storageRemoveKey("ota_tried");
    storageRemoveKey("ota_ready");
  }
  // An update this release already is (or is newer than): nothing to install or to carry on getting.
  ota::Manifest m;
  uint8_t t[ota::TRAILER];
  if (storageGetBytes("ota_ready", t, sizeof t) && !readyManifest(m)) storageRemoveKey("ota_ready");
  Progress p;
  if (storageGetBytes("ota_prog", &p, sizeof p) == sizeof p && (!ota::parseTrailer(p.trailer, m) || m.seq <= MY_SEQ))
    storageRemoveKey("ota_prog");
}

bool otaEnabled() { return storageGetInt("ota_auto", 1) != 0; }
void otaSetEnabled(bool on) { storagePutInt("ota_auto", on); }

void otaStatus(char *out, size_t len) {
  ota::Manifest m;
  if (readyManifest(m)) snprintf(out, len, "%s ready", m.version);
  else if (dl) snprintf(out, len, "getting %s %d%%", dl->m.version, (int)((uint64_t)dl->done * 100 / dl->m.size));
  else snprintf(out, len, "%s", otaEnabled() ? "on" : "off");
}

#else  // no radio updates here

void otaBoot() {
#if UNIDEX_X4
  esp_ota_mark_app_valid_cancel_rollback();
#endif
}
bool otaHeard(const LinkPacket &p) { return false; }
void otaTick() {}
void otaInstallIfReady() {}
bool otaEnabled() { return false; }
void otaSetEnabled(bool) {}
void otaStatus(char *out, size_t len) { snprintf(out, len, "unavailable"); }

#endif
