#!/usr/bin/env python3
"""RoomiePet home server: keeps a household's X4s in step on the home WiFi, and hands out signed firmware updates.

  pip install cryptography
  python3 server/roomiepet_server.py --add-household <id> <token>   # once per household (the web app shows both)
  python3 server/roomiepet_server.py [--port 8044] [--updates DIR] [--github owner/repo]

What it does (the X4's side is src/apps/house/home.cpp; the layout is src/apps/house/home_logic.h):
- An X4 that's charging and on the home WiFi finds it (a UDP broadcast "RPHOME?" on port 47474; or the address set in
  the web app) and checks in about once a minute: it sends the household's records and the shared WiFi networks, and
  gets back what it's missing, who's home, the time, and the newest release this server has, if it's newer than its
  own. So roommates' X4s stay in step without being held together, and their Pets see each other.
- Everything is sealed (AES-256-GCM) with the household's token, made from the household key: nobody else on the
  network (or on the internet, for a server out there) can read the household or pose as a roommate's X4. The
  server never has the household key, so it can't read the WiFi passwords (sealed with that key), and it can't
  sign firmware: each X4 checks a release's signature against the key built into it before it installs anything.
- Firmware: every signed .bin in --updates (default ./roomiepet-data/updates), plus, with --github, the X4 firmware
  attached to that repository's newest release (checked hourly).

Plain HTTP is fine on a home network (and works across the internet too: everything that matters is sealed or
signed). The data is kept in --data (default ./roomiepet-data): the household's records in JSON, readable by whoever
can read that folder.
"""
import argparse, hashlib, json, os, re, socket, struct, sys, threading, time, urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from cryptography.hazmat.primitives.ciphers.aead import AESGCM

MAGIC = b"RPS1"
HEAD, NONCE, TAG = 16, 12, 16
DISCOVERY_PORT = 47474
PAYLOAD_VERSION = 1
RECORD_LEN = 61          # house_logic.h's RECORD_LEN
NETWORK_LEN = 142        # secure_logic.h's NETWORK_LEN
TRAILER = 128            # ota_logic.h's TRAILER
OWNER_ONLY = {8, 9, 10}  # house_logic.h's ownerOnly: $cashtag, payment requests, catalog clothes
MAX_ANSWER_RECORDS = 120  # house_logic.h's MAX_RECORDS: an X4 can't take more in one answer
HOME_FOR = 600           # seconds a Pet counts as home after it checks in


# ---- The envelope (home_logic.h).

def open_request(token, body):
    if len(body) < HEAD + NONCE + TAG or body[:4] != MAGIC:
        return None
    try:
        return AESGCM(token).decrypt(body[HEAD:HEAD + NONCE], body[HEAD + NONCE:], body[:HEAD])
    except Exception:
        return None


def seal_answer(token, request, payload):
    nonce = os.urandom(NONCE)
    return request[:HEAD] + nonce + AESGCM(token).encrypt(nonce, payload, request[:HEAD + NONCE])


class Reader:
    def __init__(self, b):
        self.b, self.at = b, 0

    def take(self, n):
        if self.at + n > len(self.b):
            raise ValueError("short")
        self.at += n
        return self.b[self.at - n:self.at]

    def u8(self): return self.take(1)[0]
    def u16(self): return struct.unpack("<H", self.take(2))[0]
    def u32(self): return struct.unpack("<I", self.take(4))[0]


# ---- Records and networks, merged as the X4 merges them (house_logic.h's merge, secure_logic.h's NetList).

def record_key(r): return "%d:%d" % (struct.unpack_from("<I", r, 4)[0], struct.unpack_from("<H", r, 2)[0])
def record_ver(r): return struct.unpack_from("<II", r, 8)  # (ver, editor)


def record_ok(r):
    kind, origin, editor = r[0], struct.unpack_from("<I", r, 4)[0], struct.unpack_from("<I", r, 12)[0]
    seq = struct.unpack_from("<H", r, 2)[0]
    if kind == 0 or not origin or not seq or r[36] > 24:
        return False
    return kind not in OWNER_ONLY or editor == origin  # only its owner may change one of those


def network_key(n): return "%d" % struct.unpack_from("<I", n, 0)[0]
def network_ver(n): return (struct.unpack_from("<I", n, 4)[0], struct.unpack_from("<I", n, 8)[0])


def merge(store, items, key, ver, ok=lambda x: True):
    changed = 0
    for it in items:
        if not ok(it):
            continue
        k, old = key(it), store.get(key(it))
        if old is None or ver(it) > ver(bytes.fromhex(old)):
            store[k] = it.hex()
            changed += 1
    return changed


# ---- Firmware: signed releases (ota_logic.h's trailer after the image).

def image_length(d):
    if len(d) < 24 or d[0] != 0xE9 or not 0 < d[1] <= 16:
        return 0
    pos = 24
    for _ in range(d[1]):
        if pos + 8 > len(d):
            return 0
        pos += 8 + struct.unpack_from("<I", d, pos + 4)[0]
    pos = (pos + 1 + 15) // 16 * 16
    if d[23] == 1:
        pos += 32
    return pos if pos <= len(d) else 0


def read_release(path, public_key=None):
    """(seq, version, trailer, length) of a signed X4 firmware file, or None."""
    d = open(path, "rb").read()
    n = image_length(d)
    t = d[n:n + TRAILER]
    if not n or len(t) != TRAILER or t[:8] != b"RPOTA1\0\0":
        return None
    seq, size = struct.unpack_from("<II", t, 8)
    if size != n or t[16:48] != hashlib.sha256(d[:n]).digest():
        return None
    if public_key is not None:
        from cryptography.hazmat.primitives import hashes
        from cryptography.hazmat.primitives.asymmetric import ec
        from cryptography.hazmat.primitives.asymmetric.utils import encode_dss_signature
        try:
            key = ec.EllipticCurvePublicKey.from_encoded_point(ec.SECP256R1(), public_key)
            key.verify(encode_dss_signature(int.from_bytes(t[64:96], "big"), int.from_bytes(t[96:128], "big")),
                       t[:64], ec.ECDSA(hashes.SHA256()))
        except Exception:
            return None
    return seq, t[48:64].rstrip(b"\0").decode(), t, n + TRAILER


class Releases:
    def __init__(self, folder, public_key=None):
        self.folder, self.public_key, self.by_seq, self.lock = folder, public_key, {}, threading.Lock()
        os.makedirs(folder, exist_ok=True)
        self.scan()

    def scan(self):
        found = {}
        for name in sorted(os.listdir(self.folder)):
            if name.endswith(".bin"):
                r = read_release(os.path.join(self.folder, name), self.public_key)
                if r:
                    found[r[0]] = (os.path.join(self.folder, name),) + r
        with self.lock:
            self.by_seq = found

    def newest(self):
        with self.lock:
            return self.by_seq[max(self.by_seq)] if self.by_seq else None

    def get(self, seq):
        with self.lock:
            return self.by_seq.get(seq)


def fetch_github(repo, folder, releases):
    """The X4 firmware attached to the repository's newest release, into the updates folder."""
    try:
        req = urllib.request.Request(f"https://api.github.com/repos/{repo}/releases/latest",
                                     headers={"Accept": "application/vnd.github+json", "User-Agent": "roomiepet-server"})
        rel = json.load(urllib.request.urlopen(req, timeout=30))
        for a in rel.get("assets", []):
            name = a.get("name", "")
            if re.fullmatch(r"roomiepet-x4-[\w.\-]+-firmware\.bin", name) and not os.path.exists(os.path.join(folder, name)):
                tmp = os.path.join(folder, name + ".part")
                with urllib.request.urlopen(a["browser_download_url"], timeout=120) as r, open(tmp, "wb") as f:
                    f.write(r.read())
                os.replace(tmp, os.path.join(folder, name))
                print(f"downloaded {name}", flush=True)
        releases.scan()
    except Exception as e:
        print(f"GitHub: {e}", flush=True)


# ---- Households.

class Households:
    def __init__(self, folder):
        self.folder, self.lock = folder, threading.Lock()
        os.makedirs(folder, exist_ok=True)
        self.config_path = os.path.join(folder, "households.json")
        self.config = json.load(open(self.config_path)) if os.path.exists(self.config_path) else {}
        self.state = {}

    def add(self, house_id, token_hex):
        token = bytes.fromhex(token_hex)
        if len(token) != 32:
            raise ValueError("the token is 64 hex digits")
        self.config["%08x" % house_id] = {"token": token.hex()}
        self.save_config()

    def save_config(self):
        tmp = self.config_path + ".tmp"
        with open(os.open(tmp, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600), "w") as f:
            json.dump(self.config, f, indent=2)
        os.replace(tmp, self.config_path)

    def token(self, house_id):
        h = self.config.get("%08x" % house_id)
        return bytes.fromhex(h["token"]) if h else None

    def load(self, house_id):
        name = "%08x" % house_id
        if name not in self.state:
            path = os.path.join(self.folder, name + ".json")
            self.state[name] = json.load(open(path)) if os.path.exists(path) else {}
            for k in ("records", "networks", "numbers", "seen"):
                self.state[name].setdefault(k, {})
        return self.state[name]

    def save(self, house_id):
        path = os.path.join(self.folder, "%08x.json" % house_id)
        with open(path + ".tmp", "w") as f:
            json.dump(self.state["%08x" % house_id], f)
        os.replace(path + ".tmp", path)


def handle_sync(households, releases, body, now=None):
    """One check-in: (HTTP status, response body)."""
    now = int(now or time.time())
    if len(body) < HEAD or body[:4] != MAGIC:
        return 400, b"not a RoomiePet request"
    house, pet, number = struct.unpack_from("<III", body, 4)
    token = households.token(house)
    payload = open_request(token, body) if token else None
    if payload is None:
        return 403, b"unknown household or wrong token"
    with households.lock:
        st = households.load(house)
        last = st["numbers"].get(str(pet), 0)
        if number <= last:  # a replay, or an X4 whose numbers went back: it carries on after this
            return 409, b"last %d" % last
        st["numbers"][str(pet)] = number
        try:
            r = Reader(payload)
            if r.u8() != PAYLOAD_VERSION:
                return 400, b"unknown payload version"
            release, flags = r.u32(), r.u8()
            theirs = [r.take(RECORD_LEN) for _ in range(r.u16())]
            nets = [r.take(NETWORK_LEN) for _ in range(r.u8())]
        except ValueError:
            return 400, b"bad payload"
        merge(st["records"], theirs, record_key, record_ver, record_ok)
        merge(st["networks"], nets, network_key, network_ver)
        st["seen"][str(pet)] = [now, flags]
        # What they're missing, or have older copies of: up to what an X4 takes in one answer.
        have = {record_key(x): record_ver(x) for x in theirs}
        send = [bytes.fromhex(v) for k, v in st["records"].items()
                if k not in have or record_ver(bytes.fromhex(v)) > have[k]][:MAX_ANSWER_RECORDS]
        have_nets = {network_key(n): network_ver(n) for n in nets}
        send_nets = [bytes.fromhex(v) for k, v in st["networks"].items()
                     if k not in have_nets or network_ver(bytes.fromhex(v)) > have_nets[k]][:12]
        home = [(int(p), now - t, f) for p, (t, f) in st["seen"].items() if now - t < HOME_FOR][:8]
        households.save(house)
    out = bytearray([PAYLOAD_VERSION]) + struct.pack("<I", now)
    out += struct.pack("<H", len(send)) + b"".join(send)
    out += bytes([len(send_nets)]) + b"".join(send_nets)
    out += bytes([len(home)]) + b"".join(struct.pack("<IIB", p, ago, f) for p, ago, f in home)
    newest = releases.newest() if releases else None
    if newest and newest[1] > release:
        out += b"\x01" + newest[3]
    else:
        out += b"\x00"
    return 200, seal_answer(token, body, bytes(out))


def make_handler(households, releases):
    class Handler(BaseHTTPRequestHandler):
        server_version = "RoomiePet/1"

        def reply(self, code, body, headers=()):
            self.send_response(code)
            self.send_header("Content-Type", "application/octet-stream")
            self.send_header("Content-Length", str(len(body)))
            for k, v in headers:
                self.send_header(k, v)
            self.end_headers()
            self.wfile.write(body)

        def do_POST(self):
            if self.path != "/v1/sync":
                return self.reply(404, b"")
            n = int(self.headers.get("Content-Length", 0))
            if not 0 < n <= 64 * 1024:
                return self.reply(413, b"")
            self.reply(*handle_sync(households, releases, self.rfile.read(n)))

        def do_GET(self):
            m = re.fullmatch(r"/v1/firmware/(\d+)", self.path)
            rel = releases.get(int(m.group(1))) if m and releases else None
            if not rel:
                return self.reply(404, b"")
            data = open(rel[0], "rb").read()[:rel[4]]
            rng = re.fullmatch(r"bytes=(\d+)-(\d*)", self.headers.get("Range", ""))
            if not rng:
                return self.reply(200, data)
            a = int(rng.group(1))
            b = min(int(rng.group(2)) if rng.group(2) else len(data) - 1, len(data) - 1)
            if a > b:
                return self.reply(416, b"")
            self.reply(206, data[a:b + 1], [("Content-Range", f"bytes {a}-{b}/{len(data)}")])

        def log_message(self, fmt, *args):
            if os.environ.get("ROOMIEPET_LOG"):
                super().log_message(fmt, *args)

    return Handler


def discovery(port, stop):
    """Answers X4s looking for a server on this network."""
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    s.bind(("", DISCOVERY_PORT))
    s.settimeout(0.5)
    while not stop.is_set():
        try:
            data, addr = s.recvfrom(64)
        except socket.timeout:
            continue
        if data.startswith(b"RPHOME?"):
            s.sendto(b"RPHOME %d" % port, addr)
    s.close()


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", type=int, default=8044)
    ap.add_argument("--data", default="roomiepet-data")
    ap.add_argument("--updates", help="folder of signed X4 firmware (default: <data>/updates)")
    ap.add_argument("--github", help="owner/repo: fetch the X4 firmware from its newest release, hourly")
    ap.add_argument("--pub", help="ota_key.h: serve only firmware signed with this key (the X4 checks anyway)")
    ap.add_argument("--add-household", nargs=2, metavar=("ID", "TOKEN"), help="from the web app's Home server card")
    ap.add_argument("--no-discovery", action="store_true", help="don't answer RPHOME? broadcasts (set the address instead)")
    a = ap.parse_args()
    households = Households(a.data)
    if a.add_household:
        households.add(int(a.add_household[0], 16), a.add_household[1])
        print(f"household {a.add_household[0]} added")
        return
    if not households.config:
        sys.exit("no households yet: --add-household <id> <token> (the web app's Home server card shows them)")
    pub = None
    if a.pub:
        pub = bytes(int(x, 16) for x in re.findall(r"0x([0-9a-f]{2})", open(a.pub).read().split("OTA_PUBLIC_KEY")[1]))
    releases = Releases(a.updates or os.path.join(a.data, "updates"), pub)
    stop = threading.Event()
    if a.github:
        def poll():
            while not stop.is_set():
                fetch_github(a.github, releases.folder, releases)
                stop.wait(3600)
        threading.Thread(target=poll, daemon=True).start()
    if not a.no_discovery:
        threading.Thread(target=discovery, args=(a.port, stop), daemon=True).start()
    newest = releases.newest()
    print(f"RoomiePet home server on port {a.port}: {len(households.config)} household(s), "
          f"firmware {newest[2] if newest else 'none'}", flush=True)
    ThreadingHTTPServer(("", a.port), make_handler(households, releases)).serve_forever()


if __name__ == "__main__":
    main()
