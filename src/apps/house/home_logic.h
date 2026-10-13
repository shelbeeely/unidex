#pragma once
// The home server (RoomiePet, server/roomiepet_server.py): what an X4 on its home WiFi says to it and hears back. No
// Arduino code, so it can be checked on a computer (tools/securetest), and the server follows the same layout.
//
// While an X4 is charging and on a WiFi it knows, it checks in about once a minute: it sends its household's
// records, the shared WiFi networks (still sealed: the server can't read them) and its release number; the server
// keeps the newest of everything from every roommate and answers with it, who's home (checked in lately), the time,
// and the newest signed release it has (ota_logic.h: the X4 checks the signature itself and downloads it from
// /v1/firmware/<release>, which needs no key). So the household stays in step between X4s that are never awake
// together, and roommates' Pets can see each other.
//
// Every request and answer is sealed (AES-256-GCM) with the household's server token: a key made from the household
// key (secure_logic.h's derive), which the server is given once (the web app shows it). So someone else on the WiFi,
// or on the internet for a server out there, can't read the household or pretend to be a roommate's X4, and the
// server never sees the household key (or the WiFi passwords sealed with it). Each X4 numbers its requests (the
// server takes each number once: no replaying), and the answer is bound to the request it answers.
//   request:  "RPS1", house u32, pet u32, number u32, nonce (12), sealed payload, tag (16)
//   answer:   the same: its own nonce; checked with the request's 16 first bytes and nonce
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "secure_logic.h"

namespace home {

static const uint8_t MAGIC[4] = {'R', 'P', 'S', '1'};
static const size_t HEAD = 16, OVERHEAD = HEAD + secure::NONCE + secure::TAG;
static const uint16_t DISCOVERY_PORT = 47474;  // "RPHOME?" broadcast here; the server answers "RPHOME <port>"
static const uint8_t PAYLOAD_VERSION = 1;
enum Flags : uint8_t { ON_USB = 1, CAN_RELAY = 2 };  // CAN_RELAY: it runs a signed release it can upload
static const size_t TICKET = 16;

inline void serverToken(const uint8_t houseKey[secure::KEY], uint8_t out[secure::KEY]) {
  secure::derive(houseKey, "RoomiePet home server", out);
}

inline void head(uint8_t *o, uint32_t house, uint32_t pet, uint32_t number) {
  memcpy(o, MAGIC, 4);
  secure::put32(o + 4, house), secure::put32(o + 8, pet), secure::put32(o + 12, number);
}

// A request: OVERHEAD + n bytes into out. The nonce is kept (the answer is checked against it).
inline size_t sealRequest(const uint8_t token[secure::KEY], uint32_t house, uint32_t pet, uint32_t number,
                          const uint8_t nonce[secure::NONCE], const uint8_t *payload, size_t n, uint8_t *out) {
  head(out, house, pet, number);
  memcpy(out + HEAD, nonce, secure::NONCE);
  return secure::seal(token, nonce, out, HEAD, payload, n, out + HEAD + secure::NONCE,
                      out + HEAD + secure::NONCE + n)
             ? OVERHEAD + n
             : 0;
}
// The server's side (in Python, but the same steps): the payload, or false.
inline bool openRequest(const uint8_t token[secure::KEY], const uint8_t *in, size_t len, uint8_t *payload, size_t &n) {
  if (len < OVERHEAD || memcmp(in, MAGIC, 4)) return false;
  n = len - OVERHEAD;
  return secure::open(token, in + HEAD, in, HEAD, in + HEAD + secure::NONCE, n, in + len - secure::TAG, payload);
}

inline size_t sealAnswer(const uint8_t token[secure::KEY], const uint8_t *request, const uint8_t nonce[secure::NONCE],
                         const uint8_t *payload, size_t n, uint8_t *out) {
  uint8_t aad[HEAD + secure::NONCE];
  memcpy(aad, request, HEAD + secure::NONCE);
  memcpy(out, request, HEAD);
  memcpy(out + HEAD, nonce, secure::NONCE);
  return secure::seal(token, nonce, aad, sizeof aad, payload, n, out + HEAD + secure::NONCE,
                      out + HEAD + secure::NONCE + n)
             ? OVERHEAD + n
             : 0;
}
// The X4's side: the answer to `request` (its first HEAD + NONCE bytes), or false.
inline bool openAnswer(const uint8_t token[secure::KEY], const uint8_t *request, const uint8_t *in, size_t len,
                       uint8_t *payload, size_t &n) {
  if (len < OVERHEAD || memcmp(in, request, HEAD)) return false;
  uint8_t aad[HEAD + secure::NONCE];
  memcpy(aad, request, HEAD + secure::NONCE);
  n = len - OVERHEAD;
  return secure::open(token, in + HEAD, aad, sizeof aad, in + HEAD + secure::NONCE, n, in + len - secure::TAG, payload);
}

// ---- Payloads, numbers little-endian.
//   request: version u8, release u32, flags u8, records u16 + each (house_logic.h's RECORD_LEN), networks u8 + each
//            (secure_logic.h's NETWORK_LEN)
//   answer:  version u8, time u32 (unix), records u16 + each, networks u8 + each, home u8 + each (pet u32, seconds
//            since it checked in u32, flags u8), release u8 (0 or 1) + its 128-byte trailer (ota_logic.h), then
//            (if the X4 runs a signed release newer than any the server has) relay u8 (0 or 1) + a ticket (16): the
//            X4 then uploads it (PUT /v1/upload/<ticket hex>, X-Offset: <bytes so far>; the answer "next <n>" or
//            "done"), so the server can pass it to roommates' X4s as they check in. The server checks the
//            signature itself before it offers a relayed release.
//   Both then end with the home plan: rooms u8 + each (plan_seal.h's ENTRY_LEN bytes, sealed with the plan key
//   made from this token: the server can open them, for its page). The server keeps the newest of each room (with
//   the bedroom rule) and answers with the ones the X4 is missing or has older.

struct Writer {
  uint8_t *p;
  size_t n, cap;
  bool ok = true;
  Writer(uint8_t *buf, size_t size) : p(buf), n(0), cap(size) {}
  void bytes(const void *b, size_t k) {
    if (n + k > cap) ok = false;
    if (!ok) return;
    memcpy(p + n, b, k);
    n += k;
  }
  void u8(uint8_t v) { bytes(&v, 1); }
  void u16(uint16_t v) {
    const uint8_t b[2] = {(uint8_t)v, (uint8_t)(v >> 8)};
    bytes(b, 2);
  }
  void u32(uint32_t v) {
    uint8_t b[4];
    secure::put32(b, v);
    bytes(b, 4);
  }
};

struct Reader {
  const uint8_t *p;
  size_t n, at = 0;
  bool ok = true;
  Reader(const uint8_t *buf, size_t len) : p(buf), n(len) {}
  const uint8_t *bytes(size_t k) {
    if (!ok || at + k > n) {
      ok = false;
      return nullptr;
    }
    at += k;
    return p + at - k;
  }
  uint8_t u8() {
    const uint8_t *b = bytes(1);
    return b ? b[0] : 0;
  }
  uint16_t u16() {
    const uint8_t *b = bytes(2);
    return b ? b[0] | b[1] << 8 : 0;
  }
  uint32_t u32() {
    const uint8_t *b = bytes(4);
    return b ? secure::get32(b) : 0;
  }
  bool done() const { return ok && at == n; }
};

struct AtHome {
  uint32_t pet, ago;  // seconds since it checked in
  uint8_t flags;
};

}  // namespace home
