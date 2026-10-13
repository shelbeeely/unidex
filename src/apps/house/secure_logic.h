#pragma once
// The household's secrets (RoomiePet): a household key only its members' X4s hold, the WiFi networks they share
// (sealed with it), and handing the key to a roommate's X4 over the radio without anyone listening learning it. No
// Arduino code (mbed TLS, as the firmware has and a computer can install), so it can be checked on a computer
// (tools/securetest).
//
// - The key: 32 random bytes, made once per household (the web app's "Make the household key", by the first
//   roommate to need it). Its fingerprint (4 bytes of a hash of it) tells households' keys apart without saying them.
// - Handing it over (the household's Sync screen): an X4 without it broadcasts ASK with a fresh P-256 public key; a
//   roommate's X4 with it asks its owner ("Give Biscuit's X4 the household key?") and answers GIVE with its own fresh
//   public key and the key sealed (AES-256-GCM) with what both public keys agree on (ECDH). Someone listening learns
//   nothing. Someone answering instead (in the middle) would make a different confirmation code: both screens show
//   the 4-digit code, and both owners say yes only if they match.
// - A WiFi network: its SSID and password sealed with a key made from the household key, padded to one length (so
//   the size doesn't give the password's length away), with its id, version, editor and fingerprint checked too:
//   nobody without the key can read, add, change or remove one. They travel at Sync (one a beat) and through the home
//   server (home_logic.h), which can't read them either.
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <mbedtls/ecdh.h>
#include <mbedtls/ecp.h>
#include <mbedtls/bignum.h>
#include <mbedtls/gcm.h>
#include <mbedtls/md.h>
#include <mbedtls/sha256.h>
#include <mbedtls/version.h>

namespace secure {

static const size_t KEY = 32, PUB = 65, NONCE = 12, TAG = 16;
typedef void (*Random)(uint8_t *out, size_t n);  // fills with random bytes (the ESP32's RNG, /dev/urandom)

inline void put32(uint8_t *p, uint32_t v) {
  for (int i = 0; i < 4; i++) p[i] = v >> (8 * i);
}
inline uint32_t get32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

// HMAC-SHA256 of up to three pieces.
inline void hmac(const uint8_t *key, size_t keyLen, const uint8_t *a, size_t na, const uint8_t *b, size_t nb,
                 const uint8_t *c, size_t nc, uint8_t out[32]) {
  mbedtls_md_context_t ctx;
  mbedtls_md_init(&ctx);
  mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 1);
  mbedtls_md_hmac_starts(&ctx, key, keyLen);
  if (na) mbedtls_md_hmac_update(&ctx, a, na);
  if (nb) mbedtls_md_hmac_update(&ctx, b, nb);
  if (nc) mbedtls_md_hmac_update(&ctx, c, nc);
  mbedtls_md_hmac_finish(&ctx, out);
  mbedtls_md_free(&ctx);
}

// A key for one use, made from the household key: HMAC(key, label) (HKDF-Expand's one block, the key being
// random already).
inline void derive(const uint8_t key[KEY], const char *label, uint8_t out[KEY]) {
  static const uint8_t one = 1;
  hmac(key, KEY, (const uint8_t *)label, strlen(label), &one, 1, nullptr, 0, out);
}
inline uint32_t fingerprint(const uint8_t key[KEY]) {
  uint8_t d[KEY];
  derive(key, "RoomiePet fingerprint", d);
  const uint32_t f = get32(d);
  return f ? f : 1;
}

// AES-256-GCM.
inline bool seal(const uint8_t key[KEY], const uint8_t nonce[NONCE], const uint8_t *aad, size_t aadLen,
                 const uint8_t *in, size_t n, uint8_t *out, uint8_t tag[TAG]) {
  mbedtls_gcm_context g;
  mbedtls_gcm_init(&g);
  const bool ok = mbedtls_gcm_setkey(&g, MBEDTLS_CIPHER_ID_AES, key, 256) == 0 &&
                  mbedtls_gcm_crypt_and_tag(&g, MBEDTLS_GCM_ENCRYPT, n, nonce, NONCE, aad, aadLen, in, out, TAG, tag) == 0;
  mbedtls_gcm_free(&g);
  return ok;
}
inline bool open(const uint8_t key[KEY], const uint8_t nonce[NONCE], const uint8_t *aad, size_t aadLen,
                 const uint8_t *in, size_t n, const uint8_t tag[TAG], uint8_t *out) {
  mbedtls_gcm_context g;
  mbedtls_gcm_init(&g);
  const bool ok = mbedtls_gcm_setkey(&g, MBEDTLS_CIPHER_ID_AES, key, 256) == 0 &&
                  mbedtls_gcm_auth_decrypt(&g, n, nonce, NONCE, aad, aadLen, tag, TAG, in, out) == 0;
  mbedtls_gcm_free(&g);
  return ok;
}

inline int rng(void *ctx, unsigned char *out, size_t n) {
  ((Random)ctx)(out, n);
  return 0;
}

// ---- Handing the key over: P-256 ECDH with a fresh key pair on each side.

struct Ephemeral {
  uint8_t priv[32];
  uint8_t pub[PUB];
};

inline bool makeEphemeral(Ephemeral &e, Random random) {
  mbedtls_ecp_group grp;
  mbedtls_mpi d;
  mbedtls_ecp_point q;
  mbedtls_ecp_group_init(&grp);
  mbedtls_mpi_init(&d);
  mbedtls_ecp_point_init(&q);
  size_t n = 0;
  const bool ok = mbedtls_ecp_group_load(&grp, MBEDTLS_ECP_DP_SECP256R1) == 0 &&
                  mbedtls_ecp_gen_keypair(&grp, &d, &q, rng, (void *)random) == 0 &&
                  mbedtls_mpi_write_binary(&d, e.priv, 32) == 0 &&
                  mbedtls_ecp_point_write_binary(&grp, &q, MBEDTLS_ECP_PF_UNCOMPRESSED, &n, e.pub, PUB) == 0 && n == PUB;
  mbedtls_ecp_point_free(&q);
  mbedtls_mpi_free(&d);
  mbedtls_ecp_group_free(&grp);
  return ok;
}

// What two sides agree on: false for a public key that isn't a point on the curve.
inline bool agree(const uint8_t priv[32], const uint8_t theirs[PUB], Random random, uint8_t z[32]) {
  mbedtls_ecp_group grp;
  mbedtls_mpi d, s;
  mbedtls_ecp_point q;
  mbedtls_ecp_group_init(&grp);
  mbedtls_mpi_init(&d);
  mbedtls_mpi_init(&s);
  mbedtls_ecp_point_init(&q);
  const bool ok = mbedtls_ecp_group_load(&grp, MBEDTLS_ECP_DP_SECP256R1) == 0 &&
                  mbedtls_mpi_read_binary(&d, priv, 32) == 0 &&
                  mbedtls_ecp_point_read_binary(&grp, &q, theirs, PUB) == 0 && mbedtls_ecp_check_pubkey(&grp, &q) == 0 &&
                  mbedtls_ecdh_compute_shared(&grp, &s, &q, &d, rng, (void *)random) == 0 &&
                  mbedtls_mpi_write_binary(&s, z, 32) == 0;
  mbedtls_ecp_point_free(&q);
  mbedtls_mpi_free(&s);
  mbedtls_mpi_free(&d);
  mbedtls_ecp_group_free(&grp);
  return ok;
}

// The key that seals the household key on its way, bound to both public keys.
inline void handoverKey(const uint8_t z[32], const uint8_t ask[PUB], const uint8_t give[PUB], uint8_t out[KEY]) {
  static const char label[] = "RoomiePet handover";
  uint8_t pubs[2 * PUB];
  memcpy(pubs, ask, PUB), memcpy(pubs + PUB, give, PUB);
  hmac(z, 32, (const uint8_t *)label, sizeof label - 1, pubs, sizeof pubs, nullptr, 0, out);
}

// The 4-digit code both screens show: the same on both only if each heard the other's public key.
inline int confirmCode(const uint8_t ask[PUB], const uint8_t give[PUB]) {
  uint8_t h[32];
  mbedtls_sha256_context c;
  mbedtls_sha256_init(&c);
#if MBEDTLS_VERSION_MAJOR < 3
  mbedtls_sha256_starts_ret(&c, 0), mbedtls_sha256_update_ret(&c, ask, PUB), mbedtls_sha256_update_ret(&c, give, PUB);
  mbedtls_sha256_finish_ret(&c, h);
#else
  mbedtls_sha256_starts(&c, 0), mbedtls_sha256_update(&c, ask, PUB), mbedtls_sha256_update(&c, give, PUB);
  mbedtls_sha256_finish(&c, h);
#endif
  mbedtls_sha256_free(&c);
  return (int)(get32(h) % 10000);
}

// ---- Radio packets ("RPK", version 1), at the household's Sync.
//   ASK  house u32, asker u32, asker's public key (65)
//   GIVE house u32, asker u32, giver u32, giver's public key (65), the household key sealed (32) and its tag (16)
//   NET  house u32, a network (NETWORK_LEN, below)
static const uint8_t MAGIC[3] = {'R', 'P', 'K'};
static const uint8_t VERSION = 1;
enum Type : uint8_t { ASK = 1, GIVE = 2, NET = 3 };
static const size_t HEADER = 5, ASK_LEN = HEADER + 8 + PUB, GIVE_LEN = HEADER + 12 + PUB + KEY + TAG;

inline size_t header(uint8_t *out, Type t) {
  memcpy(out, MAGIC, 3), out[3] = VERSION, out[4] = t;
  return HEADER;
}
inline uint8_t typeOf(const uint8_t *p, size_t len) {
  return len >= HEADER && memcmp(p, MAGIC, 3) == 0 && p[3] == VERSION ? p[4] : 0;
}

inline size_t encodeAsk(uint32_t house, uint32_t asker, const uint8_t pub[PUB], uint8_t *out) {
  header(out, ASK);
  put32(out + 5, house), put32(out + 9, asker);
  memcpy(out + 13, pub, PUB);
  return ASK_LEN;
}
inline bool decodeAsk(const uint8_t *p, size_t len, uint32_t &house, uint32_t &asker, uint8_t pub[PUB]) {
  if (typeOf(p, len) != ASK || len != ASK_LEN) return false;
  house = get32(p + 5), asker = get32(p + 9);
  memcpy(pub, p + 13, PUB);
  return house && asker && pub[0] == 4;
}

// GIVE, from the giver's side: `mine` is a fresh key pair made for this one. False if the asker's key is bad.
inline bool encodeGive(const uint8_t houseKey[KEY], uint32_t house, uint32_t asker, uint32_t giver,
                       const uint8_t askPub[PUB], const Ephemeral &mine, Random random, uint8_t *out) {
  uint8_t z[32], k[KEY];
  if (!agree(mine.priv, askPub, random, z)) return false;
  handoverKey(z, askPub, mine.pub, k);
  header(out, GIVE);
  put32(out + 5, house), put32(out + 9, asker), put32(out + 13, giver);
  memcpy(out + 17, mine.pub, PUB);
  static const uint8_t zero[NONCE] = {};  // a fresh key every time: one nonce is enough
  return seal(k, zero, out, 17, houseKey, KEY, out + 17 + PUB, out + 17 + PUB + KEY);
}
// GIVE, on the asker's side: the household key, and the giver's public key (for the code). False if it wasn't
// for this asker, or doesn't open.
inline bool decodeGive(const uint8_t *p, size_t len, uint32_t house, uint32_t asker, const Ephemeral &mine,
                       Random random, uint32_t &giver, uint8_t givePub[PUB], uint8_t houseKey[KEY]) {
  if (typeOf(p, len) != GIVE || len != GIVE_LEN || get32(p + 5) != house || get32(p + 9) != asker) return false;
  giver = get32(p + 13);
  memcpy(givePub, p + 17, PUB);
  uint8_t z[32], k[KEY];
  if (!agree(mine.priv, givePub, random, z)) return false;
  handoverKey(z, mine.pub, givePub, k);
  static const uint8_t zero[NONCE] = {};
  return open(k, zero, p, 17, p + 17 + PUB, KEY, p + 17 + PUB + KEY, houseKey);
}

// ---- Shared WiFi networks.

static const size_t SSID = 32, PASS = 63, PLAIN = 1 + SSID + 1 + PASS;
static const size_t NETWORK_LEN = 4 * 4 + 1 + NONCE + PLAIN + TAG, NET_PACKET_LEN = HEADER + 4 + NETWORK_LEN;
static const int MAX_NETWORKS = 12;
enum NetFlags : uint8_t { REMOVED = 1 };

struct Network {
  uint32_t id, ver, editor, fp;  // fp: the household key it's sealed with (its fingerprint)
  uint8_t flags;
  uint8_t nonce[NONCE];
  uint8_t sealed[PLAIN];
  uint8_t tag[TAG];
};

inline void encodeNetwork(const Network &n, uint8_t *o) {
  put32(o, n.id), put32(o + 4, n.ver), put32(o + 8, n.editor), put32(o + 12, n.fp);
  o[16] = n.flags;
  memcpy(o + 17, n.nonce, NONCE);
  memcpy(o + 17 + NONCE, n.sealed, PLAIN);
  memcpy(o + 17 + NONCE + PLAIN, n.tag, TAG);
}
inline bool decodeNetwork(const uint8_t *o, Network &n) {
  n.id = get32(o), n.ver = get32(o + 4), n.editor = get32(o + 8), n.fp = get32(o + 12);
  n.flags = o[16];
  memcpy(n.nonce, o + 17, NONCE);
  memcpy(n.sealed, o + 17 + NONCE, PLAIN);
  memcpy(n.tag, o + 17 + NONCE + PLAIN, TAG);
  return n.id && n.ver && n.fp && n.flags <= REMOVED;
}

// What's checked along with the sealed part: the household, and everything about the network but its contents.
inline void networkAad(uint32_t house, const Network &n, uint8_t aad[21]) {
  put32(aad, house), put32(aad + 4, n.id), put32(aad + 8, n.ver), put32(aad + 12, n.editor), put32(aad + 16, n.fp);
  aad[20] = n.flags;
}

// Seals a network's SSID and password (both "" for a removed one) with the household key; id, ver, editor and
// flags must be set.
inline bool sealNetwork(const uint8_t houseKey[KEY], uint32_t house, Network &n, const char *ssid, const char *pass,
                        Random random) {
  const size_t s = strnlen(ssid, SSID + 1), p = strnlen(pass, PASS + 1);
  if (s > SSID || p > PASS) return false;
  uint8_t plain[PLAIN] = {}, k[KEY], aad[21];
  plain[0] = s;
  memcpy(plain + 1, ssid, s);
  plain[1 + SSID] = p;
  memcpy(plain + 2 + SSID, pass, p);
  n.fp = fingerprint(houseKey);
  derive(houseKey, "RoomiePet wifi", k);
  random(n.nonce, NONCE);
  networkAad(house, n, aad);
  return seal(k, n.nonce, aad, sizeof aad, plain, PLAIN, n.sealed, n.tag);
}
// Opens one: false if it isn't sealed with this key (another household's, or changed by someone without it).
inline bool openNetwork(const uint8_t houseKey[KEY], uint32_t house, const Network &n, char ssid[SSID + 1],
                        char pass[PASS + 1]) {
  if (n.fp != fingerprint(houseKey)) return false;
  uint8_t plain[PLAIN], k[KEY], aad[21];
  derive(houseKey, "RoomiePet wifi", k);
  networkAad(house, n, aad);
  if (!open(k, n.nonce, aad, sizeof aad, n.sealed, PLAIN, n.tag, plain) || plain[0] > SSID || plain[1 + SSID] > PASS)
    return false;
  memcpy(ssid, plain + 1, plain[0]), ssid[plain[0]] = 0;
  memcpy(pass, plain + 2 + SSID, plain[1 + SSID]), pass[plain[1 + SSID]] = 0;
  return true;
}

inline bool newerNet(const Network &x, const Network &y) { return x.ver != y.ver ? x.ver > y.ver : x.editor > y.editor; }

// The networks a household shares: merged like its records (each id's newest version wins, a removal included).
// The X4 merges only ones that open with its key; the home server, which can't open them, merges them as they come.
struct NetList {
  Network e[MAX_NETWORKS];
  int count = 0;

  int find(uint32_t id) const {
    for (int i = 0; i < count; i++)
      if (e[i].id == id) return i;
    return -1;
  }
  uint32_t nextVer() const {
    uint32_t v = 0;
    for (int i = 0; i < count; i++) v = e[i].ver > v ? e[i].ver : v;
    return v + 1;
  }
  // True if it changed the list. When it's full, a removed one makes room (the oldest); else the new one is dropped.
  bool merge(const Network &n) {
    const int i = find(n.id);
    if (i >= 0) {
      if (!newerNet(n, e[i])) return false;
      e[i] = n;
      return true;
    }
    if (count == MAX_NETWORKS) {
      int oldest = -1;
      for (int k = 0; k < count; k++)
        if ((e[k].flags & REMOVED) && (oldest < 0 || e[k].ver < e[oldest].ver)) oldest = k;
      if (oldest < 0) return false;
      e[oldest] = e[--count];
    }
    e[count++] = n;
    return true;
  }
};

inline size_t encodeNetPacket(uint32_t house, const Network &n, uint8_t *out) {
  header(out, NET);
  put32(out + 5, house);
  encodeNetwork(n, out + 9);
  return NET_PACKET_LEN;
}
inline bool decodeNetPacket(const uint8_t *p, size_t len, uint32_t &house, Network &n) {
  if (typeOf(p, len) != NET || len != NET_PACKET_LEN) return false;
  house = get32(p + 5);
  return decodeNetwork(p + 9, n);
}

}  // namespace secure
