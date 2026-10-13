#!/usr/bin/env python3
"""Makes the release key for radio updates (src/core/ota_logic.h): an ECDSA P-256 key pair. The private half goes to
a file outside the repository (keep it safe and secret: whoever has it can update every X4); the public half is
written into the firmware (src/core/ota_key.h), so X4s take only releases signed with it.

  python3 tools/ota/keygen.py [--out ~/.roomiepet/ota-signing-key.pem]

Then add the private key as the GitHub secret OTA_SIGNING_KEY (Settings > Secrets and variables > Actions), commit
ota_key.h, and release: from that release on, X4s pass signed releases to each other. Run it once: a new key means
X4s running the old one take nothing signed with the new one (they'd need a USB update first).
"""
import argparse, os, sys
from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric import ec

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")
HEADER = os.path.join(ROOT, "src/core/ota_key.h")


def write_header(public_bytes, path=HEADER):
    rows = ",\n".join("    " + ", ".join(f"0x{b:02x}" for b in public_bytes[i:i + 13]) for i in range(0, 65, 13))
    with open(path, "w") as f:
        f.write("#pragma once\n// The release key's public half (tools/ota/keygen.py): X4s take radio updates signed with it "
                "only\n// (ota_logic.h). Its private half is the GitHub secret OTA_SIGNING_KEY.\n#include <stdint.h>\n\n"
                f"#define OTA_KEY_SET 1\nstatic const uint8_t OTA_PUBLIC_KEY[65] = {{\n{rows}}};\n")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", default=os.path.expanduser("~/.roomiepet/ota-signing-key.pem"))
    a = ap.parse_args()
    if os.path.exists(a.out):
        sys.exit(f"{a.out} exists: not replacing a release key (X4s trust the one they have)")
    key = ec.generate_private_key(ec.SECP256R1())
    os.makedirs(os.path.dirname(a.out) or ".", exist_ok=True)
    fd = os.open(a.out, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    with os.fdopen(fd, "wb") as f:
        f.write(key.private_bytes(serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8,
                                  serialization.NoEncryption()))
    write_header(key.public_key().public_bytes(serialization.Encoding.X962, serialization.PublicFormat.UncompressedPoint))
    print(f"private key: {a.out} (secret: add it as the GitHub secret OTA_SIGNING_KEY)\npublic key: {HEADER} (commit it)")


if __name__ == "__main__":
    main()
