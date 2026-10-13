// Checks src/core/ota_logic.h and ota_crypto.h (radio firmware updates between X4s) on a computer:
//   c++ -std=c++17 -O2 -I src/core tools/otatest/ota_test.cpp -lmbedcrypto -o /tmp/ota_test && /tmp/ota_test
// (needs mbed TLS: apt install libmbedtls-dev). vector.h is a test image signed by tools/ota/sign.py with a throwaway
// key (tools/otatest/make_vector.py), so this also checks that the firmware takes what the signing tool makes.
#include <stdio.h>
#include <stdlib.h>
#include <vector>
#include "ota_crypto.h"
#include "vector.h"

using namespace ota;

static int failures;
static void check(bool ok, const char *what) {
  printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
  if (!ok) failures++;
}

// NOR flash, as the X4's: erasing a sector sets it to FF, writing can only clear bits.
struct Flash {
  std::vector<uint8_t> b = std::vector<uint8_t>(64 * 1024, 0x00);
  int erases = 0;
  bool erase(uint32_t s) {
    if (s % SECTOR || s + SECTOR > b.size()) return false;
    for (uint32_t i = 0; i < SECTOR; i++) b[s + i] = 0xFF;
    erases++;
    return true;
  }
  bool write(uint32_t at, const uint8_t *p, uint32_t n) {
    if (at + n > b.size()) return false;
    for (uint32_t i = 0; i < n; i++) b[at + i] &= p[i];
    return true;
  }
};

static Manifest signedManifest() {
  Manifest m;
  parseTrailer(SIGNED_IMAGE + IMAGE_LEN, m);
  return m;
}

// The sender's side: answers a WANT as ota.cpp's answerWant does, through a radio that loses, repeats and
// reorders packets.
static void serve(Download &d, Flash &f, const Manifest &m, uint32_t offset, uint8_t count, int &seed) {
  std::vector<std::vector<uint8_t>> air;
  for (int i = 0; i < count; i++) {
    const uint32_t at = offset + i * CHUNK;
    if (at >= m.size) break;
    const size_t n = m.size - at < CHUNK ? m.size - at : CHUNK;
    uint8_t pkt[250];
    const size_t len = encodeData(m.sha, at, SIGNED_IMAGE + at, n, pkt);
    seed = seed * 1103515245 + 12345;
    const int r = (seed >> 16) % 10;
    if (r < 2) continue;                                   // lost
    air.emplace_back(pkt, pkt + len);
    if (r == 9) air.emplace_back(pkt, pkt + len);          // heard twice
  }
  if (air.size() > 2) std::swap(air[0], air[air.size() - 1]);  // out of order
  for (auto &p : air) {
    uint8_t sha8[8];
    uint32_t off;
    const uint8_t *bytes;
    size_t n;
    if (!decodeData(p.data(), p.size(), sha8, off, bytes, n) || memcmp(sha8, d.m.sha, 8)) continue;
    d.data(off, bytes, n);
    if (d.windowFull())
      d.flush([&](uint32_t s) { return f.erase(s); }, [&](uint32_t a, const uint8_t *b, uint32_t k) { return f.write(a, b, k); });
  }
}

// Runs a download until it's done or `rounds` WANTs have gone.
static void run(Download &d, Flash &f, const Manifest &m, int rounds, int &seed) {
  uint32_t offset;
  uint8_t count;
  for (int i = 0; i < rounds && d.want(offset, count); i++) {
    uint8_t pkt[32];
    const size_t len = encodeWant(d.m.sha, offset, count, pkt);
    uint8_t sha8[8];
    uint32_t o;
    uint8_t c;
    if (decodeWant(pkt, len, sha8, o, c)) serve(d, f, m, o, c, seed);
  }
}

int main() {
  // ---- The signed image.
  const Manifest m = signedManifest();
  check(imageLength([](uint32_t at, uint8_t *b, size_t n) { memcpy(b, SIGNED_IMAGE + at, n); return true; },
                    sizeof SIGNED_IMAGE) == IMAGE_LEN,
        "the image's length, from its headers (where the trailer starts)");
  check(m.seq == 1005000 && m.size == IMAGE_LEN && strcmp(m.version, "v1.5") == 0, "the trailer: release, size, version");
  uint8_t sha[32];
  Sha256 h;
  h.add(SIGNED_IMAGE, IMAGE_LEN);
  h.done(sha);
  check(memcmp(sha, m.sha, 32) == 0, "its SHA-256 is the image's");
  check(verify(m, TEST_KEY), "signed by tools/ota/sign.py: the firmware's check agrees");
  check(!verify(m, OTHER_KEY), "not taken with another key");
  Manifest bad = m;
  bad.seq++;
  check(!verify(bad, TEST_KEY), "a changed release number breaks the signature (no faking a newer one)");
  bad = m;
  bad.sha[5] ^= 1;
  check(!verify(bad, TEST_KEY), "nor a different image");
  bad = m;
  bad.sig[40] ^= 1;
  check(!verify(bad, TEST_KEY), "nor a damaged signature");
  uint8_t zero[65] = {4};
  check(!verify(m, zero), "nor a key that isn't a point on the curve (the unset ota_key.h)");

  // ---- Release numbers.
  check(seqOf("v1.5") == 1005000 && seqOf("v1.5.2") == 1005002 && seqOf("v12.0.1") == 12000001,
        "release numbers from tags");
  check(!seqOf("v1.4-3-gabc1234") && !seqOf("8bf1852-dirty") && !seqOf("v1") && !seqOf("1.5") && !seqOf("v1.5000"),
        "nothing else is a release");
  check(seqOf("v1.10") > seqOf("v1.9"), "v1.10 is newer than v1.9");

  // ---- Packets.
  uint8_t pkt[250];
  Manifest back;
  size_t n = encodeOffer(m, pkt);
  check(n <= 250 && decodeOffer(pkt, n, back) && back.seq == m.seq && !memcmp(back.sig, m.sig, 64) && verify(back, TEST_KEY),
        "an offer carries the whole signed trailer");
  pkt[1] = 'X';
  check(!decodeOffer(pkt, n, back) && !typeOf(pkt, n), "other radio packets aren't updates");
  uint8_t sha8[8], c;
  uint32_t o;
  n = encodeWant(m.sha, 4480, 12, pkt);
  check(decodeWant(pkt, n, sha8, o, c) && o == 4480 && c == 12 && !memcmp(sha8, m.sha, 8), "WANT");
  pkt[17] = WINDOW + 1;
  check(!decodeWant(pkt, n, sha8, o, c), "no asking for more than a window");
  const uint8_t *bytes;
  size_t k;
  n = encodeData(m.sha, 224, SIGNED_IMAGE + 224, CHUNK, pkt);
  check(n <= 250 && decodeData(pkt, n, sha8, o, bytes, k) && o == 224 && k == CHUNK && !memcmp(bytes, SIGNED_IMAGE + 224, k),
        "DATA fits a radio packet");
  check(!decodeData(pkt, n - 1, sha8, o, bytes, k), "a DATA packet cut short is dropped");

  // ---- A download, over a radio that loses, repeats and reorders.
  Flash f;
  Download d;
  d.begin(m);
  check(!d.data(8, SIGNED_IMAGE + 8, CHUNK) && !d.data(CHUNK * WINDOW, SIGNED_IMAGE, CHUNK) && !d.data(0, SIGNED_IMAGE, 10),
        "chunks out of place, out of the window or the wrong size are refused");
  int seed = 7;
  run(d, f, m, 1000, seed);
  check(d.finished() && !memcmp(f.b.data(), SIGNED_IMAGE, IMAGE_LEN), "the image arrives whole");
  check(d.writeTrailer([&](uint32_t s) { return f.erase(s); }, [&](uint32_t a, const uint8_t *b, uint32_t q) { return f.write(a, b, q); }) &&
            !memcmp(f.b.data(), SIGNED_IMAGE, sizeof SIGNED_IMAGE),
        "with its trailer after it, so this X4 can pass it on");
  check(f.erases == (int)((sizeof SIGNED_IMAGE + SECTOR - 1) / SECTOR), "each sector erased once");

  // ---- Stopped part way (out of reach), carried on later from the saved place.
  Flash g;
  Download d1;
  d1.begin(m);
  seed = 3;
  run(d1, g, m, 10, seed);
  const uint32_t saved = d1.done;
  check(saved > 0 && saved < IMAGE_LEN, "part way");
  for (uint32_t i = saved; i < saved + 600 && i < g.b.size(); i++) g.b[i] &= 0x5A;  // half-written bytes after it
  Download d2;
  d2.begin(m, saved);
  check(d2.done % SECTOR == 0 && d2.done <= saved, "carries on from the sector the saved place is in");
  run(d2, g, m, 1000, seed);
  check(d2.finished() && !memcmp(g.b.data(), SIGNED_IMAGE, IMAGE_LEN), "and the image arrives whole");

  // ---- Wrong bytes from a source: caught by the SHA-256 before installing (ota.cpp's finish).
  Flash e;
  Download d3;
  d3.begin(m);
  seed = 11;
  run(d3, e, m, 1000, seed);
  e.b[5000] ^= 0x10;
  Sha256 h2;
  h2.add(e.b.data(), IMAGE_LEN);
  h2.done(sha);
  check(memcmp(sha, m.sha, 32) != 0, "a changed byte doesn't match the signed SHA-256");

  printf(failures ? "%d FAILED\n" : "all passed\n", failures);
  return failures != 0;
}
