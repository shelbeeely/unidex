// The household's secrets on the X4 (secrets.h, secure_logic.h).
#include "secrets.h"
#include "house.h"
#include "../../core/storage.h"
#include "../../core/usbsync.h"

#if UNIDEX_X4
#include <esp_random.h>
#include "secure_logic.h"

using namespace secure;
using house::House;
using house::Record;
using house::member;

static void randomBytes(uint8_t *out, size_t n) { esp_fill_random(out, n); }

// NVS: "house_key" (32 bytes) and "house_keyh" (the household it's for), "house_nets" (the NetList).
static uint8_t key[KEY];
static bool keyLoaded, haveKey;
static NetList *nets;

static void loadKey() {
  if (keyLoaded) return;
  keyLoaded = true;
  houseLoad();
  const uint32_t id = houseData().id;
  haveKey = id && storageGetBytes("house_key", key, KEY) == KEY && (uint32_t)storageGetInt("house_keyh", 0) == id;
}

static NetList &list() {
  if (!nets) {
    nets = new NetList();
    if (storageGetBytes("house_nets", nets, sizeof(NetList)) != sizeof(NetList) || nets->count < 0 ||
        nets->count > MAX_NETWORKS)
      nets->count = 0;
  }
  return *nets;
}
static void saveNets() { storagePutBytes("house_nets", nets, sizeof(NetList)); }

bool secretsHaveKey() {
  loadKey();
  return haveKey;
}
uint32_t secretsFingerprint() { return secretsHaveKey() ? fingerprint(key) : 0; }
bool secretsKey(uint8_t out[32]) {
  if (!secretsHaveKey()) return false;
  memcpy(out, key, KEY);
  return true;
}

static void storeKey(const uint8_t k[KEY]) {
  memcpy(key, k, KEY);
  storagePutBytes("house_key", key, KEY);
  storagePutInt("house_keyh", (int32_t)houseData().id);
  haveKey = keyLoaded = true;
}

void secretsMakeKey() {
  if (!houseData().id) return;
  uint8_t k[KEY];
  randomBytes(k, KEY);
  storeKey(k);
}

void secretsForget() {
  storageRemoveKey("house_key");
  storageRemoveKey("house_keyh");
  storageRemoveKey("house_nets");
  haveKey = false;
  keyLoaded = true;
  if (nets) nets->count = 0;
}

// ---- The Sync screen.

enum Question : uint8_t { NONE, GIVE_IT, TAKE_IT };
static Question question;
static uint32_t asked, lowerFp;  // asked: the roommate the question is about; lowerFp: a key that wins over ours
static int code;
static Ephemeral mine, giving;   // mine: this X4's, asking; giving: made for the GIVE being asked about
static bool mineMade;
static uint8_t askPub[PUB], offered[KEY], givePacket[GIVE_LEN];
static uint32_t askAt, netAt, netTurn, giveAt;
static int giveLeft;
static uint32_t declined[6];
static int declinedCount;

static bool wasDeclined(uint32_t pet) {
  for (int i = 0; i < declinedCount; i++)
    if (declined[i] == pet) return true;
  return false;
}

// Asking for the key: without one, or when roommates seal networks with a key that wins over this X4's (two made
// the same household's key separately: the lower fingerprint wins, so everyone ends up with one).
static bool wantKey() { return houseData().id && (!secretsHaveKey() || lowerFp); }

void secretsSyncStart() {
  question = NONE;
  mineMade = false;
  giveLeft = declinedCount = 0;
  askAt = netAt = 0;
}

void secretsTick() {
  if (!houseData().id) return;
  const uint32_t now = millis();
  uint8_t pkt[250];
  if (wantKey() && question == NONE && (int32_t)(now - askAt) >= 0) {
    if (!mineMade) mineMade = makeEphemeral(mine, randomBytes);
    if (mineMade) linkBroadcast(pkt, encodeAsk(houseData().id, houseData().me, mine.pub, pkt));
    askAt = now + 1800 + esp_random() % 400;
  }
  if (giveLeft > 0 && (int32_t)(now - giveAt) >= 0) {  // a few times: one may be lost
    linkBroadcast(givePacket, GIVE_LEN);
    giveLeft--;
    giveAt = now + 300;
  }
  NetList &l = list();
  if (secretsHaveKey() && l.count && (int32_t)(now - netAt) >= 0) {
    linkBroadcast(pkt, encodeNetPacket(houseData().id, l.e[netTurn++ % l.count], pkt));
    netAt = now + 600 + esp_random() % 200;
  }
}

static bool mergeOpened(const Network &n) {
  char ssid[SSID + 1], pass[PASS + 1];
  if (!secretsHaveKey()) return false;
  if (n.fp != fingerprint(key)) {
    if (n.fp < fingerprint(key) && (!lowerFp || n.fp < lowerFp)) lowerFp = n.fp;
    return false;
  }
  if (!openNetwork(key, houseData().id, n, ssid, pass) || !list().merge(n)) return false;
  saveNets();
  return true;
}

bool secretsHeard(const LinkPacket &p) {
  const uint8_t t = secure::typeOf(p.data, p.len);
  if (!t) return false;
  House &h = houseData();
  if (!h.id) return true;
  uint32_t house, pet;
  if (t == ASK) {
    uint8_t pub[PUB];
    if (!decodeAsk(p.data, p.len, house, pet, pub) || house != h.id || pet == h.me || !secretsHaveKey() || lowerFp ||
        question != NONE || wasDeclined(pet) || !member(h, pet) || !makeEphemeral(giving, randomBytes))
      return true;
    memcpy(askPub, pub, PUB);
    asked = pet;
    code = confirmCode(askPub, giving.pub);
    question = GIVE_IT;
  } else if (t == GIVE) {
    uint8_t pub[PUB], k[KEY];
    if (!wantKey() || question != NONE || !mineMade ||
        !decodeGive(p.data, p.len, h.id, h.me, mine, randomBytes, pet, pub, k) || wasDeclined(pet))
      return true;
    if (secretsHaveKey() && fingerprint(k) >= fingerprint(key)) return true;  // not the one that wins
    memcpy(offered, k, KEY);
    asked = pet;
    code = confirmCode(mine.pub, pub);
    question = TAKE_IT;
  } else if (t == NET) {
    Network n;
    if (decodeNetPacket(p.data, p.len, house, n) && house == h.id) mergeOpened(n);
  }
  return true;
}

bool secretsAsking() { return question != NONE; }

bool secretsQuestion(char *title, size_t titleLen, char *line, size_t lineLen, int &shown) {
  if (question == NONE) return false;
  const Record *m = member(houseData(), asked);
  const char *name = m && m->text[0] ? m->text : "a roommate";
  if (question == GIVE_IT) {
    snprintf(title, titleLen, "Give %s the key?", name);
    snprintf(line, lineLen, "Household key (WiFi, server)");
  } else {
    snprintf(title, titleLen, "Take %s's key?", name);
    snprintf(line, lineLen, "From their X4, for WiFi");
  }
  shown = code;
  return true;
}

void secretsAnswer(bool yes) {
  House &h = houseData();
  if (question == GIVE_IT && yes) {
    if (encodeGive(key, h.id, asked, h.me, askPub, giving, randomBytes, givePacket)) giveLeft = 5, giveAt = 0;
  } else if (question == TAKE_IT && yes) {
    // Networks sealed with the key this X4 had come along, sealed again with the new one.
    NetList &l = list();
    char ssid[SSID + 1], pass[PASS + 1];
    const bool had = secretsHaveKey();
    uint8_t old[KEY];
    memcpy(old, key, KEY);
    storeKey(offered);
    for (int i = 0; had && i < l.count; i++)
      if (openNetwork(old, h.id, l.e[i], ssid, pass)) {
        l.e[i].ver = l.nextVer(), l.e[i].editor = h.me;
        sealNetwork(key, h.id, l.e[i], ssid, pass, randomBytes);
      }
    saveNets();
    lowerFp = 0;
    mineMade = false;
  } else if (question != NONE && declinedCount < 6) {
    declined[declinedCount++] = asked;
  }
  question = NONE;
}

// ---- Networks.

bool secretsNetwork(int i, char *ssid, char *pass) {
  if (!secretsHaveKey()) return false;
  NetList &l = list();
  for (int k = 0; k < l.count; k++)
    if (!(l.e[k].flags & REMOVED) && openNetwork(key, houseData().id, l.e[k], ssid, pass) && i-- == 0) return true;
  return false;
}

bool secretsMergeNetwork(const uint8_t *encoded) {
  Network n;
  return decodeNetwork(encoded, n) && mergeOpened(n);
}
int secretsNetworkCount() { return list().count; }
void secretsEncodeNetwork(int i, uint8_t *out) { encodeNetwork(list().e[i], out); }

static int hexValue(char c) {
  return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
}
static bool textFromHex(const char *hex, char *out, size_t max) {
  const size_t n = strlen(hex);
  if (n % 2 || n / 2 > max) return false;
  for (size_t i = 0; i < n / 2; i++) {
    const int hi = hexValue(hex[2 * i]), lo = hexValue(hex[2 * i + 1]);
    if (hi < 0 || lo < 0) return false;
    out[i] = hi << 4 | lo;
    if (!out[i]) return false;
  }
  out[n / 2] = 0;
  return true;
}

bool secretsUsb(const char *line) {
  Print &out = usbOut();
  if (strcmp(line, "KEY GET") == 0) {
    if (secretsHaveKey()) out.printf("OK KEY %08lx\n", (unsigned long)secretsFingerprint());
    else out.println("OK KEY none");
  } else if (strcmp(line, "KEY MAKE") == 0) {
    if (!houseLoad() || !houseData().id || secretsHaveKey()) return out.println("ERR"), true;
    secretsMakeKey();
    out.printf("OK KEY MAKE %08lx\n", (unsigned long)secretsFingerprint());
  } else if (strcmp(line, "WIFI LIST") == 0) {
    NetList &l = list();
    char ssid[SSID + 1], pass[PASS + 1];
    int n = 0;
    for (int i = 0; secretsHaveKey() && i < l.count; i++) {
      if ((l.e[i].flags & REMOVED) || !openNetwork(key, houseData().id, l.e[i], ssid, pass)) continue;
      out.printf("WF %08lx ", (unsigned long)l.e[i].id);
      for (const char *c = ssid; *c; c++) out.printf("%02x", (uint8_t)*c);
      out.printf(" %08lx\n", (unsigned long)l.e[i].editor);
      n++;
    }
    out.printf("OK WIFI LIST %d\n", n);
  } else if (strncmp(line, "WIFI ADD ", 9) == 0) {
    char ssidHex[2 * SSID + 2] = "", passHex[2 * PASS + 2] = "", ssid[SSID + 1], pass[PASS + 1], s[SSID + 1], p[PASS + 1];
    NetList &l = list();
    if (!secretsHaveKey() || sscanf(line + 9, "%65s %127s", ssidHex, passHex) != 2 || !textFromHex(ssidHex, ssid, SSID) ||
        !textFromHex(passHex, pass, PASS) || !*ssid)
      return out.println("ERR"), true;
    Network n = {};
    for (int i = 0; i < l.count; i++)  // the same network again: its new password
      if (!(l.e[i].flags & REMOVED) && openNetwork(key, houseData().id, l.e[i], s, p) && !strcmp(s, ssid)) n = l.e[i];
    if (!n.id) n.id = esp_random() | 1;
    n.ver = l.nextVer(), n.editor = houseData().me, n.flags = 0;
    if (!sealNetwork(key, houseData().id, n, ssid, pass, randomBytes) || !l.merge(n)) return out.println("ERR"), true;
    saveNets();
    out.printf("OK WIFI ADD %08lx\n", (unsigned long)n.id);
  } else if (strncmp(line, "WIFI DEL ", 9) == 0) {
    NetList &l = list();
    const int i = l.find(strtoul(line + 9, nullptr, 16));
    if (!secretsHaveKey() || i < 0 || (l.e[i].flags & REMOVED)) return out.println("ERR"), true;
    Network n = l.e[i];
    n.ver = l.nextVer(), n.editor = houseData().me, n.flags = REMOVED;
    if (!sealNetwork(key, houseData().id, n, "", "", randomBytes) || !l.merge(n)) return out.println("ERR"), true;
    saveNets();
    out.println("OK WIFI DEL");
  } else {
    return false;
  }
  return true;
}

#else  // the Waveshare board: no household secrets

bool secretsHaveKey() { return false; }
uint32_t secretsFingerprint() { return 0; }
bool secretsKey(uint8_t[32]) { return false; }
void secretsMakeKey() {}
void secretsForget() {}
void secretsSyncStart() {}
bool secretsHeard(const LinkPacket &) { return false; }
void secretsTick() {}
bool secretsQuestion(char *, size_t, char *, size_t, int &) { return false; }
bool secretsAsking() { return false; }
void secretsAnswer(bool) {}
bool secretsNetwork(int, char *, char *) { return false; }
bool secretsMergeNetwork(const uint8_t *) { return false; }
int secretsNetworkCount() { return 0; }
void secretsEncodeNetwork(int, uint8_t *) {}
bool secretsUsb(const char *) { return false; }

#endif
