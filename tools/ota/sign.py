#!/usr/bin/env python3
"""Signs an X4 firmware.bin for radio updates (src/core/ota_logic.h): appends the 128-byte trailer (release number,
length, SHA-256, version, and an ECDSA P-256 signature over them) that X4s check before taking it from each other.
The bootloader ignores bytes after the image, so the signed file flashes like any other.

  python3 tools/ota/sign.py .pio/build/x4/firmware.bin --key ~/.roomiepet/ota-signing-key.pem [--out signed.bin]
  python3 tools/ota/sign.py firmware.bin --verify --pub src/core/ota_key.h    # check a signed one

The key can also come from the environment (OTA_SIGNING_KEY: the PEM itself, or a path to it). The release number
and version are read from the firmware itself (the marker ota.cpp builds in), so they can't disagree with it.
"""
import argparse, hashlib, os, re, struct, sys
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec
from cryptography.hazmat.primitives.asymmetric.utils import decode_dss_signature, encode_dss_signature

MAGIC = b"RPOTA1\0\0"
TRAILER, SIGNED = 128, 64


def image_length(d):
    """ota_logic.h's imageLength."""
    if len(d) < 24 or d[0] != 0xE9 or not 0 < d[1] <= 16:
        sys.exit("not an ESP32 app image")
    pos = 24
    for _ in range(d[1]):
        pos += 8 + struct.unpack_from("<I", d, pos + 4)[0]
    pos = (pos + 1 + 15) // 16 * 16
    if d[23] == 1:
        pos += 32
    return pos


def marker(image):
    m = re.search(rb"RPOTA:(\d+):([\x20-\x7e]{1,16});", image)
    if not m:
        sys.exit("no RPOTA marker: not an X4 build with radio updates")
    return int(m.group(1)), m.group(2).decode()


def load_key(arg):
    pem = arg or os.environ.get("OTA_SIGNING_KEY", "")
    if not pem:
        sys.exit("no key: --key, or OTA_SIGNING_KEY")
    if "BEGIN" not in pem:
        pem = open(os.path.expanduser(pem)).read()
    return serialization.load_pem_private_key(pem.encode(), None)


def unsigned_part(seq, size, sha, version):
    return MAGIC + struct.pack("<II", seq, size) + sha + version.encode().ljust(16, b"\0")


def sign(data, key):
    image = data[:image_length(data)]
    seq, version = marker(image)
    if not seq:
        sys.exit(f"{version}: not a release (its release number is 0); tag it vX.Y or vX.Y.Z first")
    if "dirty" in version:
        sys.exit(f"{version}: built with uncommitted changes, not signing it")
    body = unsigned_part(seq, len(image), hashlib.sha256(image).digest(), version)
    r, s = decode_dss_signature(key.sign(body, ec.ECDSA(hashes.SHA256())))
    return image + body + r.to_bytes(32, "big") + s.to_bytes(32, "big"), seq, version


def public_from_header(path):
    text = open(path).read()
    return bytes(int(x, 16) for x in re.findall(r"0x([0-9a-f]{2})", text.split("OTA_PUBLIC_KEY")[1]))


def verify(data, pub):
    n = image_length(data)
    t = data[n:n + TRAILER]
    if len(t) != TRAILER or t[:8] != MAGIC:
        sys.exit("not signed")
    seq, size = struct.unpack_from("<II", t, 8)
    ok_hash = size == n and t[16:48] == hashlib.sha256(data[:n]).digest()
    key = ec.EllipticCurvePublicKey.from_encoded_point(ec.SECP256R1(), pub)
    sig = encode_dss_signature(int.from_bytes(t[64:96], "big"), int.from_bytes(t[96:128], "big"))
    try:
        key.verify(sig, t[:SIGNED], ec.ECDSA(hashes.SHA256()))
        ok_sig = True
    except Exception:
        ok_sig = False
    version = t[48:64].rstrip(b"\0").decode()
    print(f"{version} (release {seq}): hash {'ok' if ok_hash else 'WRONG'}, "
          f"signature {'ok' if ok_sig else 'WRONG'}")
    return ok_hash and ok_sig


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("bin")
    ap.add_argument("--key")
    ap.add_argument("--out")
    ap.add_argument("--verify", action="store_true")
    ap.add_argument("--pub", default=os.path.join(os.path.dirname(os.path.abspath(__file__)), "../../src/core/ota_key.h"))
    a = ap.parse_args()
    data = open(a.bin, "rb").read()
    if a.verify:
        sys.exit(0 if verify(data, public_from_header(a.pub)) else 1)
    signed, seq, version = sign(data, load_key(a.key))
    open(a.out or a.bin, "wb").write(signed)
    print(f"signed {version} (release {seq}, {len(signed) - TRAILER} bytes): {a.out or a.bin}")


if __name__ == "__main__":
    main()
