#!/usr/bin/env python3
"""Checks the home server (roomiepet_server.py) as an X4 would talk to it, over real HTTP and UDP:
  python3 -m unittest server/test_server.py   (needs: pip install cryptography)
The requests are sealed as src/apps/house/home_logic.h seals them; REQUEST_VECTOR is the one tools/securetest checks
the X4's code against, so both sides are held to the same bytes."""
import hashlib, os, socket, struct, sys, tempfile, threading, unittest, urllib.error, urllib.request
from http.server import ThreadingHTTPServer
from cryptography.hazmat.primitives.ciphers.aead import AESGCM

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import roomiepet_server as srv  # noqa: E402

HOUSE, PIP, BISCUIT = 77, 100, 200
TOKEN = bytes(range(32))
REQUEST_VECTOR = bytes.fromhex(
    "525053314d0000006400000005000000000102030405060708090a0b2f67ba77aac5b074e22cfeeec18c0c590a0276e9b1d2ed07be864006e483ce")


def record(kind, seq, origin, ver, text="", editor=None, a=0, d=0, flags=0):
    r = bytearray(srv.RECORD_LEN)
    r[0], r[1] = kind, flags
    struct.pack_into("<H", r, 2, seq)
    struct.pack_into("<8I", r, 4, origin, ver, editor if editor is not None else origin, ver, a, 0, 0, d)
    r[36] = len(text)
    r[37:37 + len(text)] = text.encode()
    return bytes(r)


def network(nid, ver, editor=PIP):
    n = bytearray(os.urandom(srv.NETWORK_LEN))  # sealed: the server can't read it, so anything opaque does
    struct.pack_into("<4I", n, 0, nid, ver, editor, 0xABCD)
    n[16] = 0
    return bytes(n)


def payload(release=0, records=(), nets=()):
    return (bytes([1]) + struct.pack("<IB", release, 1) + struct.pack("<H", len(records)) + b"".join(records) +
            bytes([len(nets)]) + b"".join(nets))


def seal(pet, number, body, token=TOKEN, house=HOUSE):
    head = b"RPS1" + struct.pack("<III", house, pet, number)
    nonce = os.urandom(12)
    return head + nonce + AESGCM(token).encrypt(nonce, body, head)


def open_answer(req, ans, token=TOKEN):
    assert ans[:16] == req[:16]
    return AESGCM(token).decrypt(ans[16:28], ans[28:], req[:28])


def parse(answer):
    r = srv.Reader(answer)
    assert r.u8() == 1
    t = r.u32()
    recs = [r.take(srv.RECORD_LEN) for _ in range(r.u16())]
    nets = [r.take(srv.NETWORK_LEN) for _ in range(r.u8())]
    home = [struct.unpack("<IIB", r.take(9)) for _ in range(r.u8())]
    trailer = r.take(128) if r.u8() == 1 else None
    return t, recs, nets, home, trailer


def fake_release(folder, seq, version):
    """A small signed-looking X4 image (the server checks the hash, the X4 the signature)."""
    seg = struct.pack("<II", 0x3C000000, 5000) + os.urandom(5000)
    head = bytearray(24)
    head[0], head[1], head[23] = 0xE9, 1, 1
    img = bytes(head) + seg
    img += b"\0" * ((len(img) + 1 + 15) // 16 * 16 - len(img) - 1) + b"\xEF"
    img += hashlib.sha256(img).digest()
    t = b"RPOTA1\0\0" + struct.pack("<II", seq, len(img)) + hashlib.sha256(img).digest() + version.encode().ljust(16, b"\0")
    t += os.urandom(64)
    with open(os.path.join(folder, f"x4-{version}.bin"), "wb") as f:
        f.write(img + t)
    return img + t


class ServerTest(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.mkdtemp()
        self.homes = srv.Households(self.dir)
        self.homes.add(HOUSE, TOKEN.hex())
        self.releases = srv.Releases(os.path.join(self.dir, "updates"))
        self.http = ThreadingHTTPServer(("127.0.0.1", 0), srv.make_handler(self.homes, self.releases))
        threading.Thread(target=self.http.serve_forever, daemon=True).start()
        self.url = "http://127.0.0.1:%d" % self.http.server_address[1]

    def tearDown(self):
        self.http.shutdown()
        self.http.server_close()

    def post(self, body):
        try:
            with urllib.request.urlopen(urllib.request.Request(self.url + "/v1/sync", data=body), timeout=5) as r:
                return r.status, r.read()
        except urllib.error.HTTPError as e:
            return e.code, e.read()

    def sync(self, pet, number, body, token=TOKEN):
        req = seal(pet, number, body, token)
        code, ans = self.post(req)
        self.assertEqual(code, 200, ans)
        return parse(open_answer(req, ans, token))

    def test_the_x4s_bytes(self):
        self.assertEqual(srv.open_request(TOKEN, REQUEST_VECTOR), b"hello roomiepet")

    def test_household_in_step_through_the_server(self):
        chore = record(2, 1, PIP, 5, "Bins")
        self.sync(PIP, 1, payload(records=[chore]))
        _, recs, _, home, _ = self.sync(BISCUIT, 1, payload())
        self.assertEqual(recs, [chore])
        self.assertEqual(sorted(p for p, _, _ in home), [PIP, BISCUIT])
        newer = record(2, 1, PIP, 9, "Bins", editor=BISCUIT, d=1)
        self.sync(BISCUIT, 2, payload(records=[newer]))
        _, recs, _, _, _ = self.sync(PIP, 2, payload(records=[chore]))
        self.assertEqual(recs, [newer], "the newest copy wins")
        _, recs, _, _, _ = self.sync(PIP, 3, payload(records=[newer]))
        self.assertEqual(recs, [], "nothing it already has")

    def test_only_owners_change_owner_only_records(self):
        tag = record(8, 2, PIP, 3, "pipcash")
        self.sync(PIP, 1, payload(records=[tag]))
        forged = record(8, 2, PIP, 50, "biscuitcash", editor=BISCUIT)
        self.sync(BISCUIT, 1, payload(records=[forged]))
        _, recs, _, _, _ = self.sync(BISCUIT, 2, payload())
        self.assertEqual(recs, [tag], "a $cashtag changed by someone else isn't kept")

    def test_networks_relayed_newest_wins(self):
        n1, n2 = network(5, 1), network(5, 2)
        self.sync(PIP, 1, payload(nets=[n1]))
        self.sync(PIP, 2, payload(nets=[n2]))
        _, _, nets, _, _ = self.sync(BISCUIT, 1, payload(nets=[n1]))
        self.assertEqual(nets, [n2])

    def test_wrong_token_and_replays(self):
        code, _ = self.post(seal(PIP, 1, payload(), token=bytes(32)))
        self.assertEqual(code, 403)
        code, _ = self.post(seal(PIP, 1, payload(), house=99))
        self.assertEqual(code, 403, "an unknown household")
        req = seal(PIP, 7, payload())
        self.assertEqual(self.post(req)[0], 200)
        code, body = self.post(req)
        self.assertEqual((code, body), (409, b"last 7"), "the same request again is turned away")
        self.assertEqual(self.post(seal(PIP, 3, payload()))[0], 409, "so is an older number")
        req = bytearray(seal(PIP, 8, payload()))
        req[-1] ^= 1
        self.assertEqual(self.post(bytes(req))[0], 403, "a changed request doesn't open")

    def test_answer_bound_to_its_request(self):
        req1, req2 = seal(PIP, 1, payload()), seal(PIP, 2, payload())
        _, ans1 = self.post(req1)
        with self.assertRaises(Exception):
            open_answer(req2, ans1)

    def test_firmware_offered_and_downloaded_in_pieces(self):
        data = fake_release(self.releases.folder, 1005000, "v1.5")
        self.releases.scan()
        _, _, _, _, trailer = self.sync(PIP, 1, payload(release=1004000))
        self.assertEqual(trailer[8:12], struct.pack("<I", 1005000), "a newer release is offered")
        _, _, _, _, trailer = self.sync(PIP, 2, payload(release=1005000))
        self.assertIsNone(trailer, "not to an X4 that has it")
        got = b""
        while len(got) < len(data):
            req = urllib.request.Request(self.url + "/v1/firmware/1005000",
                                         headers={"Range": "bytes=%d-%d" % (len(got), len(got) + 2047)})
            with urllib.request.urlopen(req, timeout=5) as r:
                self.assertEqual(r.status, 206)
                got += r.read()
        self.assertEqual(got, data)
        bad = bytearray(data)
        bad[100] ^= 1
        open(os.path.join(self.releases.folder, "broken.bin"), "wb").write(bad)
        self.releases.scan()
        self.assertEqual(len(self.releases.by_seq), 1, "a file whose hash is wrong isn't offered")

    def test_discovery(self):
        stop = threading.Event()
        t = threading.Thread(target=srv.discovery, args=(8044, stop), daemon=True)
        t.start()
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.settimeout(3)
        for _ in range(10):
            s.sendto(b"RPHOME?", ("127.0.0.1", srv.DISCOVERY_PORT))
            try:
                self.assertEqual(s.recvfrom(64)[0], b"RPHOME 8044")
                break
            except socket.timeout:
                continue
        s.close()
        stop.set()
        t.join()


if __name__ == "__main__":
    unittest.main()
