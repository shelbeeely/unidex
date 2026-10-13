// The home server, from the X4 (home.h, home_logic.h).
#include "home.h"
#include "house.h"
#include "secrets.h"
#include "../pet/pet.h"
#include "../../core/battery.h"
#include "../../core/clock.h"
#include "../../core/hotspot.h"
#include "../../core/link.h"
#include "../../core/net.h"
#include "../../core/ota.h"
#include "../../core/power.h"
#include "../../core/storage.h"
#include "../../core/usbsync.h"

#if UNIDEX_X4
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <esp_random.h>
#include "../../core/ota_logic.h"

using namespace house;

static const uint32_t CHECK_MS = 60000, RETRY_MS = 120000, JOIN_MS = 20000, HOME_FOR_S = 180, BLOCK = 16384;

enum State : uint8_t { OFF, JOINING, ON };
static State state;
static bool onUsb, held, firstCheck;
static uint32_t usbCheckedAt, stateAt, nextCheckAt, retryAt, fetchAt;
static IPAddress serverIp;
static uint16_t serverPort;
static char status[32] = "off";
static home::AtHome present[8];
static int presentCount;
static uint32_t presentAt;  // millis() of the answer `present` came in
static uint32_t number, numberUpTo;  // the request number, and how far NVS has it reserved

static void setStatus(const char *s) { strlcpy(status, s, sizeof status); }

static bool usbPower() {
  if (!usbCheckedAt || millis() - usbCheckedAt > 10000) onUsb = batteryCharging(), usbCheckedAt = millis() | 1;
  return onUsb;
}

// Something else has the WiFi (the radio, the phone hotspot, a transfer): leave it alone.
static bool wifiBusy() { return linkOn() || hotspotOn() || netClaimed(); }

static void stop(bool turnOff) {
  otaStreamStop();
  if (turnOff && !wifiBusy()) netOff();
  if (held) powerRelease(), held = false;
  state = OFF;
  serverPort = 0;
}

// Numbers each request once: reserved in NVS 50 at a time, so a restart never reuses one.
static uint32_t nextNumber() {
  if (number >= numberUpTo) {
    if (!numberUpTo) number = storageGetInt("home_ctr", 0);
    numberUpTo = number + 50;
    storagePutInt("home_ctr", (int32_t)numberUpTo);
  }
  return ++number;
}

// The server: the address set in the web app, else whoever answers "RPHOME?" on this WiFi.
static bool findServer() {
  if (serverPort) return true;
  const String url = storageGetString("home_url");
  if (url.length()) {
    String rest = url.startsWith("http://") ? url.substring(7) : url;
    const int slash = rest.indexOf('/');
    if (slash >= 0) rest = rest.substring(0, slash);
    const int colon = rest.indexOf(':');
    const String host = colon >= 0 ? rest.substring(0, colon) : rest;
    const uint16_t port = colon >= 0 ? rest.substring(colon + 1).toInt() : 80;
    if (!port || !WiFi.hostByName(host.c_str(), serverIp)) return false;
    serverPort = port;
    return true;
  }
  WiFiUDP udp;
  if (!udp.begin(home::DISCOVERY_PORT + 1)) return false;
  for (int tries = 0; tries < 2 && !serverPort; tries++) {
    udp.beginPacket(WiFi.broadcastIP(), home::DISCOVERY_PORT);
    udp.print("RPHOME?");
    udp.endPacket();
    const uint32_t until = millis() + 700;
    while (!serverPort && (int32_t)(millis() - until) < 0) {
      if (udp.parsePacket()) {
        char buf[32] = {};
        udp.read(buf, sizeof buf - 1);
        if (!strncmp(buf, "RPHOME ", 7) && atoi(buf + 7) > 0) serverIp = udp.remoteIP(), serverPort = atoi(buf + 7);
      }
      delay(10);
    }
  }
  udp.stop();
  return serverPort != 0;
}

// Reads a response body: up to cap bytes (a Content-Length one).
static size_t readBody(HTTPClient &http, uint8_t *buf, size_t cap) {
  const int want = http.getSize();
  if (want < 0 || (size_t)want > cap) return 0;
  WiFiClient *s = http.getStreamPtr();
  size_t got = 0;
  uint32_t last = millis();
  while (got < (size_t)want && millis() - last < 5000) {
    const int n = s->read(buf + got, want - got);
    if (n > 0) got += n, last = millis();
    else delay(2);
    powerAlive();
  }
  return got == (size_t)want ? got : 0;
}

static void noteArrivals(const home::AtHome *was, int wasCount) {
  for (int i = 0; i < presentCount; i++) {
    bool before = false;
    for (int k = 0; k < wasCount; k++) before |= was[k].pet == present[i].pet;
    const Record *m = member(houseData(), present[i].pet);
    if (!before && m && !firstCheck) {
      char text[48];
      snprintf(text, sizeof text, "%s's home!", m->text[0] ? m->text : "A roommate");
      houseAddNews(text);
    }
  }
}

static void checkIn() {
  uint8_t key[32], token[32];
  House &h = houseData();
  if (!secretsKey(key) || !houseLoad() || !h.id) return;
  if (!findServer()) return setStatus("no server found");
  home::serverToken(key, token);
  const size_t cap = 1 + 4 + 1 + 2 + h.count * RECORD_LEN + 1 + secure::MAX_NETWORKS * secure::NETWORK_LEN;
  uint8_t *payload = (uint8_t *)malloc(cap), *request = (uint8_t *)malloc(cap + home::OVERHEAD);
  const size_t answerCap = 1 + 4 + 2 + MAX_RECORDS * RECORD_LEN + 1 + secure::MAX_NETWORKS * secure::NETWORK_LEN + 1 +
                           8 * 9 + 1 + ota::TRAILER + home::OVERHEAD + 64;
  uint8_t *answer = (uint8_t *)malloc(answerCap);
  if (!payload || !request || !answer) {
    free(payload), free(request), free(answer);
    return setStatus("out of memory");
  }
  home::Writer w(payload, cap);
  w.u8(home::PAYLOAD_VERSION);
  w.u32(otaRelease());
  w.u8(home::ON_USB);
  w.u16(h.count);
  uint8_t rec[RECORD_LEN];
  for (int i = 0; i < h.count; i++) encodeRecord(h.r[i], rec), w.bytes(rec, RECORD_LEN);
  const int nets = secretsNetworkCount();
  w.u8(nets);
  uint8_t net[secure::NETWORK_LEN];
  for (int i = 0; i < nets; i++) secretsEncodeNetwork(i, net), w.bytes(net, sizeof net);
  uint8_t nonce[secure::NONCE];
  esp_fill_random(nonce, sizeof nonce);
  const size_t len = w.ok ? home::sealRequest(token, h.id, h.me, nextNumber(), nonce, payload, w.n, request) : 0;

  HTTPClient http;
  http.setTimeout(5000);
  http.begin(serverIp.toString(), serverPort, "/v1/sync");
  http.addHeader("Content-Type", "application/octet-stream");
  const int code = len ? http.POST(request, len) : -1;
  size_t got = code > 0 ? readBody(http, answer, answerCap) : 0;
  http.end();
  size_t n = 0;
  if (code == 409 && got > 5 && got < answerCap && !memcmp(answer, "last ", 5)) {  // this X4's numbers went back (its NVS was wiped)
    answer[got] = 0;
    number = numberUpTo = strtoul((char *)answer + 5, nullptr, 10);
    setStatus("renumbering");
  } else if (code != 200 || !home::openAnswer(token, request, answer, got, payload, n)) {
    setStatus(code == 403 ? "server doesn't know us" : code > 0 ? "server said no" : "server not answering");
    serverPort = code > 0 ? serverPort : 0;  // not answering: look for it again next time
  } else {
    // The answer: the newest of everything, who's home, the time, the newest release.
    home::Reader r(payload, n);
    int changes = 0;
    if (r.u8() == home::PAYLOAD_VERSION) {
      const uint32_t t = r.u32();
      const uint32_t mine = clockValid() ? time(nullptr) : 0;
      if (timeIsBetter(mine, clockTrustedAt(), t, t)) clockAdopt(t, t);
      for (int i = r.u16(); i > 0 && r.ok; i--)
        if (const uint8_t *o = r.bytes(RECORD_LEN)) changes += mergeEncoded(h, o, houseHeardChange);
      for (int i = r.u8(); i > 0 && r.ok; i--)
        if (const uint8_t *o = r.bytes(secure::NETWORK_LEN)) secretsMergeNetwork(o);
      home::AtHome was[8];
      const int wasCount = presentCount;
      memcpy(was, present, sizeof was);
      presentCount = 0;
      for (int i = r.u8(); i > 0 && r.ok; i--) {
        home::AtHome a = {r.u32(), r.u32(), r.u8()};
        if (a.pet != h.me && a.ago < HOME_FOR_S && presentCount < 8) present[presentCount++] = a;
      }
      presentAt = millis();
      if (r.u8() == 1)
        if (const uint8_t *t = r.bytes(ota::TRAILER)) otaStreamOffer(t);
      noteArrivals(was, wasCount);
      if (wasCount != presentCount || memcmp(was, present, presentCount * sizeof(home::AtHome))) petChanged();
      firstCheck = false;
    }
    if (changes) houseSave(), petChanged();
    char s[32];
    snprintf(s, sizeof s, "%d home", presentCount);
    setStatus(s);
  }
  free(payload), free(request), free(answer);
}

// The next piece of a release the server offered.
static void fetchBlock() {
  const uint32_t at = otaStreamAt(), release = otaStreamRelease();
  if (at == UINT32_MAX || !release || (int32_t)(millis() - fetchAt) < 0) return;
  uint8_t *buf = (uint8_t *)malloc(BLOCK);
  if (!buf) return;
  char path[40], range[48];
  snprintf(path, sizeof path, "/v1/firmware/%lu", (unsigned long)release);
  snprintf(range, sizeof range, "bytes=%lu-%lu", (unsigned long)at, (unsigned long)(at + BLOCK - 1));
  HTTPClient http;
  http.setTimeout(5000);
  http.begin(serverIp.toString(), serverPort, path);
  http.addHeader("Range", range);
  const int code = http.GET();
  const size_t got = code == 206 ? readBody(http, buf, BLOCK) : 0;
  http.end();
  if (!got || !otaStreamWrite(buf, got)) fetchAt = millis() + 30000;  // try again in a while
  free(buf);
}

void homePoll() {
  const bool eligible = usbPower() && !wifiBusy() && houseLoad() && houseData().id && secretsHaveKey();
  if (!eligible) {
    if (state != OFF) stop(!wifiBusy());
    if (!usbPower()) setStatus("on battery");
    return;
  }
  const uint32_t now = millis();
  switch (state) {
    case OFF:
      if (retryAt && (int32_t)(now - retryAt) < 0) return;
      if (const char *err = netBegin()) {
        setStatus(err);
        retryAt = now + RETRY_MS;
        return;
      }
      state = JOINING, stateAt = now;
      if (!held) powerHold(), held = true;  // light sleep would drop the WiFi
      setStatus("joining WiFi");
      return;
    case JOINING:
      if (WiFi.status() == WL_CONNECTED) {
        state = ON, nextCheckAt = now, firstCheck = true;
        WiFi.setSleep(true);  // modem sleep: connected, at a fraction of the power
      } else if (now - stateAt > JOIN_MS) {
        stop(true);
        setStatus("WiFi not found");
        retryAt = now + RETRY_MS;
      }
      return;
    case ON:
      if (WiFi.status() != WL_CONNECTED) {
        stop(true);
        retryAt = now + 10000;
        return;
      }
      if ((int32_t)(now - nextCheckAt) >= 0) {
        checkIn();
        nextCheckAt = millis() + CHECK_MS;
      } else {
        fetchBlock();
      }
      return;
  }
}

int homeAtHome(home::AtHome *out, int max) {
  if (state != ON || millis() - presentAt > 2 * CHECK_MS) return 0;
  const int n = presentCount < max ? presentCount : max;
  memcpy(out, present, n * sizeof(home::AtHome));
  return n;
}

void homeStatus(char *out, size_t len) { strlcpy(out, status, len); }

bool homeUsb(const char *line) {
  Print &out = usbOut();
  if (strcmp(line, "HOME GET") == 0) {
    for (int i = 0; i < presentCount; i++)
      out.printf("HM %08lx %lu %u\n", (unsigned long)present[i].pet, (unsigned long)present[i].ago, present[i].flags);
    out.print("OK HOME ");
    const String url = storageGetString("home_url");
    if (url.length())
      for (char c : url) out.printf("%02x", (uint8_t)c);
    else out.print('-');
    out.print(' ');
    for (const char *c = status; *c; c++) out.printf("%02x", (uint8_t)*c);
    out.print('\n');
  } else if (strncmp(line, "HOME SET ", 9) == 0) {
    const char *hex = line + 9;
    if (!strcmp(hex, "-")) {
      storageRemoveKey("home_url");
    } else {
      String url;
      for (size_t i = 0; hex[i] && hex[i + 1] && url.length() < 96; i += 2) {
        char b[3] = {hex[i], hex[i + 1], 0};
        const char c = (char)strtol(b, nullptr, 16);
        if (c < 0x21 || c > 0x7E) return out.println("ERR"), true;
        url += c;
      }
      if (!url.length()) return out.println("ERR"), true;
      storagePutString("home_url", url.c_str());
    }
    serverPort = 0;  // found again with the new address
    out.println("OK HOME SET");
  } else if (strcmp(line, "HOME TOKEN") == 0) {
    uint8_t key[32], token[32];
    if (!houseLoad() || !houseData().id || !secretsKey(key)) return out.println("ERR"), true;
    home::serverToken(key, token);
    out.printf("OK HOME TOKEN %08lx ", (unsigned long)houseData().id);
    for (uint8_t b : token) out.printf("%02x", b);
    out.print('\n');
  } else {
    return false;
  }
  return true;
}

#else  // the Waveshare board: no home server

void homePoll() {}
int homeAtHome(home::AtHome *, int) { return 0; }
void homeStatus(char *out, size_t len) { strlcpy(out, "unavailable", len); }
bool homeUsb(const char *) { return false; }

#endif
