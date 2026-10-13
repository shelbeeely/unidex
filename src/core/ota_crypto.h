#pragma once
// Checking a release's signature and SHA-256 (ota_logic.h) with mbed TLS: the firmware's (3.x, in the ESP32 Arduino
// core) and a computer's (2.28 or 3.x, for tools/otatest), so the X4 and the test run the same code.
#include <stddef.h>
#include <stdint.h>
#include <mbedtls/ecdsa.h>
#include <mbedtls/ecp.h>
#include <mbedtls/bignum.h>
#include <mbedtls/sha256.h>
#include <mbedtls/version.h>
#include "ota_logic.h"

namespace ota {

// SHA-256, a piece at a time.
struct Sha256 {
  mbedtls_sha256_context c;
  Sha256() {
    mbedtls_sha256_init(&c);
#if MBEDTLS_VERSION_MAJOR < 3
    mbedtls_sha256_starts_ret(&c, 0);
#else
    mbedtls_sha256_starts(&c, 0);
#endif
  }
  ~Sha256() { mbedtls_sha256_free(&c); }
  void add(const uint8_t *p, size_t n) {
#if MBEDTLS_VERSION_MAJOR < 3
    mbedtls_sha256_update_ret(&c, p, n);
#else
    mbedtls_sha256_update(&c, p, n);
#endif
  }
  void done(uint8_t out[32]) {
#if MBEDTLS_VERSION_MAJOR < 3
    mbedtls_sha256_finish_ret(&c, out);
#else
    mbedtls_sha256_finish(&c, out);
#endif
  }
};

// True if the manifest was signed by the key whose public half is `key` (65 bytes: 04, x, y).
inline bool verify(const Manifest &m, const uint8_t key[65]) {
  uint8_t t[TRAILER], hash[32];
  packTrailer(m, t);
  Sha256 h;
  h.add(t, SIGNED);
  h.done(hash);
  mbedtls_ecp_group grp;
  mbedtls_ecp_point q;
  mbedtls_mpi r, s;
  mbedtls_ecp_group_init(&grp);
  mbedtls_ecp_point_init(&q);
  mbedtls_mpi_init(&r);
  mbedtls_mpi_init(&s);
  const bool ok = mbedtls_ecp_group_load(&grp, MBEDTLS_ECP_DP_SECP256R1) == 0 &&
                  mbedtls_ecp_point_read_binary(&grp, &q, key, 65) == 0 && mbedtls_ecp_check_pubkey(&grp, &q) == 0 &&
                  mbedtls_mpi_read_binary(&r, m.sig, 32) == 0 && mbedtls_mpi_read_binary(&s, m.sig + 32, 32) == 0 &&
                  mbedtls_ecdsa_verify(&grp, hash, 32, &q, &r, &s) == 0;
  mbedtls_mpi_free(&s);
  mbedtls_mpi_free(&r);
  mbedtls_ecp_point_free(&q);
  mbedtls_ecp_group_free(&grp);
  return ok;
}

}  // namespace ota
