#pragma once
// Firmware updates from one X4 to another over the radio (RoomiePet), so every X4 in a household (and every one its
// Pet meets) ends up on the newest release without a computer. No Arduino code, so it can be checked on a computer
// (tools/otatest).
//
// Only a release signed with the project's key is taken, and only a newer one:
// - A release's firmware.bin has a 128-byte TRAILER appended (tools/ota/sign.py): its release number (seq), its
//   length and SHA-256, its version ("v1.5"), and an ECDSA P-256 signature over those, made with the release key
//   (kept off the repository: a GitHub secret). The X4 firmware holds only the public key (ota_key.h).
// - The bootloader ignores what follows an image, so the trailer is flashed with it (USB, or this radio) and the
//   X4 running that release can show it to others. An unsigned build (a developer's) takes updates but offers none.
// - A device takes an offer only when its signature checks out against the built-in key and its seq is higher than
//   its own (so nobody can put back an older, signed release with a fixed bug), writes it to the other app
//   partition, checks the SHA-256 of what it wrote, and only then installs it (on its next sleep). If the new
//   firmware doesn't start properly, the bootloader goes back to the old one (ota.cpp).
//
// The radio (ESP-NOW, at most 250 bytes a packet): "RPU", version 1, type, then
//   OFFER (broadcast every ~2 s by an X4 running a signed release): the 128-byte trailer
//   WANT  (to the offerer): the first 8 bytes of the image's SHA-256, offset u32, count u8: "send me count chunks
//         from offset"
//   DATA  (the answer, one packet a chunk): the SHA-256's first 8 bytes, offset u32, length u8, then the bytes
//         (CHUNK, or fewer at the end)
// A download goes WINDOW chunks at a time and survives the radio going off (progress is saved; it carries on next
// time either X4 is in Meet or Sync).
#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace ota {

static const uint8_t MAGIC[3] = {'R', 'P', 'U'};
static const uint8_t VERSION = 1;
enum Type : uint8_t { OFFER = 1, WANT = 2, DATA = 3 };
static const size_t HEADER = 5, TRAILER = 128, SIGNED = 64, VERSION_LEN = 16;
static const size_t CHUNK = 224, WINDOW = 12, SECTOR = 4096;
static const uint8_t TRAILER_MAGIC[8] = {'R', 'P', 'O', 'T', 'A', '1', 0, 0};

// The trailer: magic (8), seq u32, size u32, SHA-256 of the image (32), version (16, zero-padded), then the
// signature (r and s, 32 bytes each, big-endian) over those first 64 bytes.
struct Manifest {
  uint32_t seq, size;
  uint8_t sha[32];
  char version[VERSION_LEN + 1];
  uint8_t sig[64];
};

inline void put32(uint8_t *p, uint32_t v) {
  for (int i = 0; i < 4; i++) p[i] = v >> (8 * i);
}
inline uint32_t get32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

inline void packTrailer(const Manifest &m, uint8_t out[TRAILER]) {
  memset(out, 0, TRAILER);
  memcpy(out, TRAILER_MAGIC, 8);
  put32(out + 8, m.seq);
  put32(out + 12, m.size);
  memcpy(out + 16, m.sha, 32);
  memcpy(out + 48, m.version, strnlen(m.version, VERSION_LEN));
  memcpy(out + SIGNED, m.sig, 64);
}
// False if it isn't a trailer (the signature isn't checked here: ota_crypto.h's verify does that).
inline bool parseTrailer(const uint8_t *in, Manifest &m) {
  if (memcmp(in, TRAILER_MAGIC, 8) != 0) return false;
  m = {};
  m.seq = get32(in + 8);
  m.size = get32(in + 12);
  memcpy(m.sha, in + 16, 32);
  for (size_t i = 0; i < VERSION_LEN && in[48 + i]; i++) {
    if (in[48 + i] < 0x20 || in[48 + i] > 0x7E) return false;
    m.version[i] = in[48 + i];
  }
  memcpy(m.sig, in + SIGNED, 64);
  return m.seq > 0 && m.size > 0;
}

// A release number from its tag: "v1.5" is 1005000, "v1.5.2" 1005002; 0 for anything else (a build between
// releases keeps its last tag's number, so it never looks newer than that release).
inline uint32_t seqOf(const char *tag) {
  if (!tag || *tag != 'v') return 0;
  uint32_t part[3] = {0, 0, 0};
  int n = 0;
  const char *p = tag + 1;
  while (n < 3) {
    if (*p < '0' || *p > '9') return 0;
    while (*p >= '0' && *p <= '9') part[n] = part[n] * 10 + (*p++ - '0');
    if (part[n] > 999) return 0;
    n++;
    if (*p != '.') break;
    p++;
  }
  if (*p || n < 2) return 0;
  return part[0] * 1000000 + part[1] * 1000 + part[2];
}

// The length of an ESP32 app image (the bytes of firmware.bin, before any trailer), read through read(offset, buf,
// n): the 24-byte header, each segment (8-byte header and its data), padding with the checksum to a multiple of 16,
// and the SHA-256 the build appends. 0 if it doesn't look like one.
template <typename Read> uint32_t imageLength(Read read, uint32_t limit) {
  uint8_t h[24];
  if (!read(0, h, 24) || h[0] != 0xE9 || h[1] == 0 || h[1] > 16) return 0;
  uint32_t pos = 24;
  for (int i = 0; i < h[1]; i++) {
    uint8_t s[8];
    if (pos + 8 > limit || !read(pos, s, 8)) return 0;
    const uint32_t len = get32(s + 4);
    if (len > limit) return 0;
    pos += 8 + len;
  }
  pos = (pos + 1 + 15) / 16 * 16;  // the checksum byte, then to a 16-byte boundary
  if (h[23] == 1) pos += 32;       // the appended SHA-256
  return pos <= limit ? pos : 0;
}

// ---- Packets.

inline size_t header(uint8_t *out, Type t) {
  memcpy(out, MAGIC, 3), out[3] = VERSION, out[4] = t;
  return HEADER;
}
inline uint8_t typeOf(const uint8_t *p, size_t len) {
  return len >= HEADER && memcmp(p, MAGIC, 3) == 0 && p[3] == VERSION ? p[4] : 0;
}

inline size_t encodeOffer(const Manifest &m, uint8_t *out) {
  header(out, OFFER);
  packTrailer(m, out + HEADER);
  return HEADER + TRAILER;
}
inline bool decodeOffer(const uint8_t *p, size_t len, Manifest &m) {
  return typeOf(p, len) == OFFER && len == HEADER + TRAILER && parseTrailer(p + HEADER, m);
}

inline size_t encodeWant(const uint8_t sha[32], uint32_t offset, uint8_t count, uint8_t *out) {
  header(out, WANT);
  memcpy(out + 5, sha, 8);
  put32(out + 13, offset);
  out[17] = count;
  return 18;
}
inline bool decodeWant(const uint8_t *p, size_t len, uint8_t sha8[8], uint32_t &offset, uint8_t &count) {
  if (typeOf(p, len) != WANT || len != 18) return false;
  memcpy(sha8, p + 5, 8);
  offset = get32(p + 13);
  count = p[17];
  return count >= 1 && count <= WINDOW;
}

inline size_t encodeData(const uint8_t sha[32], uint32_t offset, const uint8_t *data, size_t n, uint8_t *out) {
  header(out, DATA);
  memcpy(out + 5, sha, 8);
  put32(out + 13, offset);
  out[17] = (uint8_t)n;
  memcpy(out + 18, data, n);
  return 18 + n;
}
inline bool decodeData(const uint8_t *p, size_t len, uint8_t sha8[8], uint32_t &offset, const uint8_t *&data,
                       size_t &n) {
  if (typeOf(p, len) != DATA || len < 19) return false;
  memcpy(sha8, p + 5, 8);
  offset = get32(p + 13);
  n = p[17];
  data = p + 18;
  return n >= 1 && n <= CHUNK && len == 18 + n;
}

// ---- A download: the image comes a window (WINDOW chunks) at a time, kept here until the window is whole, then
// written to the partition in order. Writing erases each flash sector as the first byte of it is written, so a
// download resumed at a sector boundary (from a saved `done`, rounded down) never writes over old bytes.
struct Download {
  Manifest m = {};
  uint32_t done = 0;  // bytes written to the partition
  uint8_t buf[WINDOW * CHUNK];
  uint16_t have = 0;  // which chunks of the window have come

  void begin(const Manifest &manifest, uint32_t from = 0) {
    m = manifest;
    done = from / SECTOR * SECTOR;
    if (done > m.size) done = 0;
    have = 0;
  }
  uint32_t windowBytes() const {
    const uint32_t left = m.size - done;
    return left < WINDOW * CHUNK ? left : (uint32_t)(WINDOW * CHUNK);
  }
  int windowChunks() const { return (int)((windowBytes() + CHUNK - 1) / CHUNK); }
  bool finished() const { return done >= m.size; }
  bool windowFull() const { return !finished() && have == (uint16_t)((1u << windowChunks()) - 1); }

  // What to ask for: from the first missing chunk of the window to the last. False when nothing is missing.
  bool want(uint32_t &offset, uint8_t &count) const {
    if (finished()) return false;
    int first = -1, last = -1;
    for (int i = 0; i < windowChunks(); i++)
      if (!(have >> i & 1)) {
        if (first < 0) first = i;
        last = i;
      }
    if (first < 0) return false;
    offset = done + first * CHUNK;
    count = (uint8_t)(last - first + 1);
    return true;
  }

  // A chunk: true if it was one this window still needed (the right place and length).
  bool data(uint32_t offset, const uint8_t *bytes, size_t n) {
    if (finished() || offset < done || (offset - done) % CHUNK) return false;
    const uint32_t i = (offset - done) / CHUNK;
    if (i >= (uint32_t)windowChunks() || (have >> i & 1)) return false;
    const uint32_t expect = i + 1 < (uint32_t)windowChunks() ? CHUNK : windowBytes() - i * CHUNK;
    if (n != expect) return false;
    memcpy(buf + i * CHUNK, bytes, n);
    have |= 1 << i;
    return true;
  }

  // Writes a whole window: erase(sector start) for each sector it starts, then write(offset, bytes, n).
  template <typename Erase, typename Write> bool flush(Erase erase, Write write) {
    if (!windowFull()) return false;
    const uint32_t n = windowBytes();
    if (!writeErased(done, buf, n, erase, write)) return false;
    done += n;
    have = 0;
    return true;
  }

  // The trailer after the image, so this X4 can pass the release on once it runs it.
  template <typename Erase, typename Write> bool writeTrailer(Erase erase, Write write) {
    uint8_t t[TRAILER];
    packTrailer(m, t);
    return finished() && writeErased(m.size, t, TRAILER, erase, write);
  }

  template <typename Erase, typename Write>
  static bool writeErased(uint32_t at, const uint8_t *bytes, uint32_t n, Erase erase, Write write) {
    for (uint32_t s = (at + SECTOR - 1) / SECTOR * SECTOR; s < at + n; s += SECTOR)
      if (!erase(s)) return false;
    return write(at, bytes, n);
  }
};

}  // namespace ota
