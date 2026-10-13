# Updates between X4s (RoomiePet)

An X4 on an older release takes a newer one from the home server while it's charging at home ([HOMESERVER.md](HOMESERVER.md)), or from any X4 it syncs with (House, Sync) or meets (Pet, hold B), over
the same radio (ESP-NOW), and installs it the next time it goes to sleep. One roommate updates over USB, and the whole
household follows at the next Sync, without a computer. The Waveshare board (unidex) isn't part of this.

## What stops a bad update

| Risk | What stops it |
|---|---|
| Someone sends their own firmware | Only a release **signed with the RoomiePet release key** (ECDSA P-256) is taken. X4s hold just the public half (`src/core/ota_key.h`); the private half never goes in the repository (it's the GitHub secret `OTA_SIGNING_KEY` and the maintainer's own copy). An X4 checks the signature before it takes a single byte. |
| An old, signed release with a bug put back | The signature covers the **release number** (`v1.5` is 1005000, from the git tag). An X4 takes only a higher number than its own, and never one that failed to start on it before. |
| Damaged or changed bytes on the way (a bad radio, a liar in the middle) | The signature also covers the image's **SHA-256** and length. The X4 hashes everything it wrote before it'll install it, and once more just before. A source whose bytes don't match is ignored for 10 minutes, and the download starts again. |
| An update that doesn't start | It's installed into the other app partition; the old one stays. The bootloader boots the new one on trial, and the firmware keeps it only once it has got through starting up (`otaBoot`). If it doesn't, the bootloader goes back to the old one, which won't take that release again. |
| A flood of fake offers | Signatures are checked at most once every 3 seconds, and only for an offer newer than this X4's release. |
| A flat battery half way | No download starts on a low battery. A download carries on where it stopped (progress is saved every 64 KB), and installing is just a restart. |

You can turn it off: **Settings > Updates** on the X4 (`on` / `off`, and it shows `getting v1.5 40%` or `v1.5 ready`),
or **Settings > Updates** in the web app. Over USB, everything is as before.

Not covered: someone with the private key can update every X4, so keep it safe. Someone flashing their own
firmware over USB can do anything with their own X4 (this is about what X4s take from each other). A developer's own
build is unsigned: it takes updates (turn them off in Settings while testing) but never offers itself.

## How it works

- `tools/ota/sign.py` appends a 128-byte **trailer** to `firmware.bin`: the magic `RPOTA1`, the release number, the
  image's length and SHA-256, the version, then the signature over those. The bootloader ignores what follows an
  image, so a signed file flashes over USB like any other, and the X4 running it can show the trailer to others.
  The release number and version are read from a marker built into the firmware (`RPOTA:<number>:<version>;`), so
  they can't disagree with it, and builds that aren't a clean, tagged release aren't signed.
- `src/core/ota_logic.h` (checked on a computer by `tools/otatest`): the trailer, release numbers, the radio packets,
  and a download:
  - **OFFER:** every ~2 s, while the radio is on, an X4 running a signed release broadcasts its trailer.
  - **WANT:** an X4 on an older release, once the offer's signature checks out, asks that X4 for 12 chunks at a time.
  - **DATA:** the answer, 224 bytes a packet.
  - A whole window is written to flash in order, each sector erased as it's reached, so a download resumed from a
    saved place never writes over old bytes.
- `src/core/ota_crypto.h`: SHA-256 and the signature check, with mbed TLS. It's the same code on the X4 and in the
  test, which checks it against an image signed by `sign.py`.
- `src/core/ota.cpp`: the X4's side. It offers its own release (checked as another X4 would check it), answers asks,
  and downloads into the other app partition. When the X4 next goes to sleep it checks everything once more, then
  switches partitions and restarts (a restart, not a sleep, so the new firmware starts clean).

A 1.8 MB release is about 8,400 packets: a minute or two with two X4s side by side in Meet or Sync. Longer is fine:
it carries on next time.

## Setting it up (once)

```sh
pip install cryptography
python3 tools/ota/keygen.py   # private key: ~/.roomiepet/ota-signing-key.pem; public key: src/core/ota_key.h
```

1. Commit `src/core/ota_key.h`. Until it holds a key, X4s neither offer nor take updates by radio.
2. Add the private key file's contents as the repository secret **OTA_SIGNING_KEY** (Settings > Secrets and
   variables > Actions).
3. Keep a copy of the private key somewhere safe and offline. Lose it, and X4s can't be updated by radio until each
   has been flashed over USB with firmware holding a new key. Leak it, and anyone can update everyone's X4: make a
   new key and update every X4 over USB.

## Releasing

Tag the release `vX.Y` or `vX.Y.Z` and publish it. The Website workflow builds the X4 firmware at that tag, signs
it (its build runs `tools/ota/sign_build.py`, which signs when `OTA_SIGNING_KEY` is set), checks the signature, and
attaches `roomiepet-x4-<tag>-firmware.bin` to the release. Flash that file to one X4, and the rest follow by radio.

To sign by hand:

```sh
OTA_SIGNING_KEY=~/.roomiepet/ota-signing-key.pem pio run -e x4 -t upload   # build, sign and flash
python3 tools/ota/sign.py .pio/build/x4/firmware.bin --verify             # check a signed file
```
