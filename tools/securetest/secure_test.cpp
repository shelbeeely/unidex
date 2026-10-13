// Checks src/apps/house/secure_logic.h and home_logic.h (the household key, shared WiFi, the home server) on a computer:
//   c++ -std=c++17 -O2 -I src/apps/house tools/securetest/secure_test.cpp -lmbedcrypto -o /tmp/secure_test && /tmp/secure_test
// (needs mbed TLS: apt install libmbedtls-dev). REQUEST_VECTOR was made with Python's cryptography (as the home
// server, server/roomiepet_server.py, seals), so the X4 and the server are checked to agree.
#include <stdio.h>
#include <stdlib.h>
#include "home_logic.h"

using namespace secure;

static int failures;
static void check(bool ok, const char *what) {
  printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
  if (!ok) failures++;
}

static void randomBytes(uint8_t *out, size_t n) {
  FILE *f = fopen("/dev/urandom", "rb");
  if (!f || fread(out, 1, n, f) != n) abort();
  fclose(f);
}

static const char REQUEST_VECTOR[] =
    "525053314d0000006400000005000000000102030405060708090a0b2f67ba77aac5b074e22cfeeec18c0c590a0276e9b1d2ed07be864006e483ce";

static size_t fromHex(const char *h, uint8_t *out) {
  size_t n = 0;
  for (; h[0] && h[1]; h += 2) {
    unsigned v;
    sscanf(h, "%2x", &v);
    out[n++] = v;
  }
  return n;
}

int main() {
  const uint32_t HOUSE = 77, PIP = 100, BISCUIT = 200;
  uint8_t houseKey[KEY];
  randomBytes(houseKey, KEY);

  // ---- Handing the household key over.
  Ephemeral asker, giver, mallory;
  check(makeEphemeral(asker, randomBytes) && makeEphemeral(giver, randomBytes) && makeEphemeral(mallory, randomBytes) &&
            asker.pub[0] == 4 && memcmp(asker.pub, giver.pub, PUB),
        "fresh key pairs");
  uint8_t pkt[250], pub[PUB];
  uint32_t house, who;
  size_t n = encodeAsk(HOUSE, BISCUIT, asker.pub, pkt);
  check(n <= 250 && decodeAsk(pkt, n, house, who, pub) && house == HOUSE && who == BISCUIT && !memcmp(pub, asker.pub, PUB),
        "ASK carries the asker's public key");
  check(encodeGive(houseKey, HOUSE, BISCUIT, PIP, pub, giver, randomBytes, pkt), "GIVE made");
  uint8_t got[KEY], givePub[PUB];
  uint32_t from;
  check(decodeGive(pkt, GIVE_LEN, HOUSE, BISCUIT, asker, randomBytes, from, givePub, got) && from == PIP &&
            !memcmp(got, houseKey, KEY),
        "the asker gets the household key");
  check(confirmCode(asker.pub, givePub) == confirmCode(pub, giver.pub), "both screens show the same code");
  bool inClear = false;
  for (size_t i = 0; i + 8 <= GIVE_LEN; i++) inClear |= !memcmp(pkt + i, houseKey, 8);
  check(!inClear, "the key isn't in the packet in the clear");
  Ephemeral other;
  makeEphemeral(other, randomBytes);
  check(!decodeGive(pkt, GIVE_LEN, HOUSE, BISCUIT, other, randomBytes, from, givePub, got),
        "someone else who heard the GIVE can't open it");
  check(!decodeGive(pkt, GIVE_LEN, HOUSE, 999, asker, randomBytes, from, givePub, got), "nor is it taken by another asker");
  pkt[60] ^= 1;
  check(!decodeGive(pkt, GIVE_LEN, HOUSE, BISCUIT, asker, randomBytes, from, givePub, got), "a changed GIVE doesn't open");
  // Someone in the middle answers the asker with their own key pair and a key of their choosing: it opens, but the
  // codes differ, so neither owner says yes.
  uint8_t fake[KEY];
  randomBytes(fake, KEY);
  encodeGive(fake, HOUSE, BISCUIT, PIP, asker.pub, mallory, randomBytes, pkt);
  check(decodeGive(pkt, GIVE_LEN, HOUSE, BISCUIT, asker, randomBytes, from, givePub, got) &&
            confirmCode(asker.pub, givePub) != confirmCode(asker.pub, giver.pub),
        "a key from someone in the middle shows a different code than the real roommate's");
  uint8_t bad[PUB] = {4};
  check(!encodeGive(houseKey, HOUSE, BISCUIT, PIP, bad, giver, randomBytes, pkt), "a public key off the curve is refused");

  // ---- WiFi networks.
  Network net = {};
  net.id = 0xAB12, net.ver = 1, net.editor = PIP;
  check(sealNetwork(houseKey, HOUSE, net, "Flat 3", "correct horse battery", randomBytes), "a network sealed");
  char ssid[SSID + 1], pass[PASS + 1];
  check(openNetwork(houseKey, HOUSE, net, ssid, pass) && !strcmp(ssid, "Flat 3") && !strcmp(pass, "correct horse battery"),
        "and opened with the household key");
  uint8_t enc[NETWORK_LEN];
  encodeNetwork(net, enc);
  bool plainPass = false;
  for (size_t i = 0; i + 7 <= NETWORK_LEN; i++) plainPass |= !memcmp(enc + i, "correct", 7) || !memcmp(enc + i, "Flat 3", 6);
  check(!plainPass, "neither the SSID nor the password is in it in the clear");
  Network short1 = net, long1 = net;
  sealNetwork(houseKey, HOUSE, short1, "a", "12345678", randomBytes);
  sealNetwork(houseKey, HOUSE, long1, "a much longer network name!", "a much longer password, really quite long", randomBytes);
  check(sizeof short1.sealed == sizeof long1.sealed, "every network is the same size (no telling the password's length)");
  uint8_t key2[KEY];
  randomBytes(key2, KEY);
  check(!openNetwork(key2, HOUSE, net, ssid, pass), "another household's key can't open it");
  check(!openNetwork(houseKey, HOUSE + 1, net, ssid, pass), "nor in another household");
  Network t = net;
  t.flags = REMOVED;
  check(!openNetwork(houseKey, HOUSE, t, ssid, pass), "nobody without the key can mark it removed");
  t = net, t.ver = 99;
  check(!openNetwork(houseKey, HOUSE, t, ssid, pass), "nor make an old copy look newer");
  t = net, t.sealed[3] ^= 1;
  check(!openNetwork(houseKey, HOUSE, t, ssid, pass), "nor change it");
  Network back;
  n = encodeNetPacket(HOUSE, net, pkt);
  check(n <= 250 && decodeNetPacket(pkt, n, house, back) && house == HOUSE && openNetwork(houseKey, HOUSE, back, ssid, pass),
        "a network fits a radio packet");
  check(!sealNetwork(houseKey, HOUSE, t, "this name is far too long for any wifi network", "x", randomBytes),
        "an SSID over 32 characters is refused");

  // The list: newest wins, removals spread, full lists make room from removed ones.
  NetList l;
  check(l.merge(net) && !l.merge(net) && l.count == 1, "a network merges once");
  Network newer = net;
  newer.ver = 2, newer.flags = REMOVED;
  sealNetwork(houseKey, HOUSE, newer, "", "", randomBytes);
  check(l.merge(newer) && l.e[0].flags == REMOVED && !l.merge(net), "its removal wins over the older copy");
  for (uint32_t i = 2; l.count < MAX_NETWORKS; i++) {
    Network x = net;
    x.id = i;
    l.merge(x);
  }
  Network extra = net;
  extra.id = 999;
  check(l.merge(extra) && l.find(999) >= 0 && l.find(net.id) < 0, "a full list makes room from a removed one");
  extra.id = 1000;
  check(!l.merge(extra), "and refuses when there's none");
  check(l.nextVer() == 2, "the next version is past every one in the list");

  // ---- The home server's envelope.
  uint8_t token[KEY], token2[KEY];
  home::serverToken(houseKey, token);
  home::serverToken(key2, token2);
  check(memcmp(token, houseKey, KEY) != 0, "the server's token isn't the household key");
  uint8_t vec[256], payload[256];
  size_t vlen = fromHex(REQUEST_VECTOR, vec), pn;
  uint8_t fixed[KEY];
  for (int i = 0; i < 32; i++) fixed[i] = i;
  check(home::openRequest(fixed, vec, vlen, payload, pn) && pn == 15 && !memcmp(payload, "hello roomiepet", 15),
        "opens a request sealed by Python's cryptography (as the server does)");
  uint8_t nonce[NONCE], req[256], ans[256];
  for (int i = 0; i < 12; i++) nonce[i] = i;
  check(home::sealRequest(fixed, 77, 100, 5, nonce, (const uint8_t *)"hello roomiepet", 15, req) == vlen &&
            !memcmp(req, vec, vlen),
        "and seals one byte for byte the same");
  randomBytes(nonce, NONCE);
  size_t rlen = home::sealRequest(token, HOUSE, PIP, 6, nonce, (const uint8_t *)"records", 7, req);
  check(rlen && !home::openRequest(token2, req, rlen, payload, pn), "another household's token can't open it");
  req[13] ^= 1;  // the request number
  check(!home::openRequest(token, req, rlen, payload, pn), "its number can't be changed (no replaying as new)");
  req[13] ^= 1;
  uint8_t an[NONCE];
  randomBytes(an, NONCE);
  size_t alen = home::sealAnswer(token, req, an, (const uint8_t *)"answer", 6, ans);
  check(home::openAnswer(token, req, ans, alen, payload, pn) && pn == 6 && !memcmp(payload, "answer", 6), "the answer opens");
  uint8_t req2[256];
  randomBytes(nonce, NONCE);
  home::sealRequest(token, HOUSE, PIP, 7, nonce, (const uint8_t *)"records", 7, req2);
  check(!home::openAnswer(token, req2, ans, alen, payload, pn), "an old answer isn't taken for a new request");

  uint8_t buf[64];
  home::Writer w(buf, sizeof buf);
  w.u8(1), w.u32(1005000), w.u16(513);
  home::Reader r(buf, w.n);
  check(w.ok && r.u8() == 1 && r.u32() == 1005000 && r.u16() == 513 && r.done(), "payload writer and reader");
  r.u8();
  check(!r.ok, "reading past the end is caught");

  printf(failures ? "%d FAILED\n" : "all passed\n", failures);
  return failures != 0;
}
