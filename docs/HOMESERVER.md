# Home WiFi and the home server (RoomiePet)

Three things for when RoomiePets are at home:

- **Shared WiFi networks.** Roommates share the household's WiFi networks (and passwords) between their X4s, sealed
  with a **household key** that only the household's X4s hold.
- **The home server.** A small program on a computer at home (a Raspberry Pi, a NAS, a desktop) or out on the
  internet. It keeps roommates' X4s in step over WiFi and hands out signed firmware updates.
- **Home mode.** While an X4 is **charging** on a WiFi it knows, it stays connected and checks in with the server
  every minute. The household stays in step without holding X4s together, a roommate's Pet that's home shows beside
  yours (and yours says "Biscuit's home!"), and a new release comes down by itself. On battery, none of this runs:
  WiFi would flatten the battery in hours.

## The household key

32 random bytes, made by the X4 that starts a household (House > Start). For a household started before this,
make it in the web app (Settings > Home WiFi > *Make the household key*) on one X4, once.

Roommates' X4s get it at **House > Sync**:

1. An X4 without the key asks for it, with a fresh P-256 public key.
2. A roommate's X4 that has it asks its owner: *Give Biscuit the key?* It shows a 4-digit code.
3. The X4 asking shows *Take Pip's key?* and its code.
4. If the codes match, say yes on both.

The key travels sealed (AES-256-GCM) with what the two fresh keys agree on (ECDH). Someone listening learns nothing.
Someone answering in the middle would make the codes differ. If two roommates made keys separately, the one with
the lower fingerprint wins: the other X4s ask for it at Sync, and networks shared under the old key move over.

## Shared WiFi networks

Add them in the web app (Settings > Home WiFi), up to 12. An X4 joins the strongest one in range, or its own network
from the website's setup if that one's there. Each network is sealed with a key made from the household key and
padded to one length, with its id, version and who changed it checked too:

- Nobody without the household key can read, add, change or remove one.
- The home server can't read them either.

They spread at Sync (one a beat) and through the home server; the newest change wins, a removal too.

## The home server

```sh
pip install cryptography
python3 server/roomiepet_server.py --add-household <id> <token>   # once: the web app shows the line to copy
python3 server/roomiepet_server.py --github <owner>/<repo> --pub src/core/ota_key.h   # then run it (as a service)
```

In the web app, **Settings > Home server > Set up the server** shows the `--add-household` line. The token is made
from the household key: it lets the server hold the household, but it isn't the key.

X4s find the server on the home WiFi by themselves (a broadcast on UDP port 47474), or use the address set in the
web app (for a server elsewhere, or a network that blocks broadcasts). It listens on port 8044 (`--port`).

| | |
|---|---|
| **Household** | Each X4 sends its records and the shared networks; the server keeps the newest of each (with the X4's rules: a $cashtag, a payment request or catalog clothes only from their owner) and answers with what that X4 is missing. |
| **Who's home** | Every Pet that checked in within 10 minutes. X4s show roommates' Pets from the last 3. The web app marks them on Roommates. |
| **The time** | The server's clock sets an X4's when that's better (as a roommate's does at Sync). |
| **Relaying** | A release that reaches the household on an X4 first (from another X4 at the park, say) goes up to the server when that X4 next checks in. The server asks for it, checks the signature with the release key (`--pub src/core/ota_key.h`: without it, it never asks), and offers it to every other X4 as it checks in. One X4 uploads a release at a time, in pieces, picking up where it stopped. |
| **Updates** | Every signed `.bin` in `--updates` (default `roomiepet-data/updates`), plus, with `--github`, the X4 firmware attached to the newest release, checked hourly. An X4 on an older release downloads the newest in 16 KB pieces (picking up where it stopped), checks the signature and every byte as it does over the radio ([OTA.md](OTA.md)), and installs it once it's been idle (it doesn't sleep on USB). |

## The radio and the WiFi at home

The X4 has one radio. The device-to-device radio (ESP-NOW) and a WiFi connection must use the same channel, and a
connected X4 can't change channel. So:

| | Where | What happens |
|---|---|---|
| **Meet** (any Pets) | The meeting channel (1), everywhere | Meeting is in person and asked for, so it **takes precedence**: a home WiFi connection drops while Meet is open, and comes back after. Pets at the park, at work or at home always find each other, from any household. |
| **Sync** (roommates) | The home WiFi's channel at home; the meeting channel elsewhere | Charging and connected at home: it **stays connected**, and the radio uses the WiFi's channel (the home server carries on beside it). Not connected: it goes to the channel of a household WiFi in range (a quick scan), else the meeting channel. Hearing nobody for 5 s, it looks on the other channels (1, 6 and 11 first, about a second each) and stays where it hears a roommate. A new roommate's X4, which doesn't know the home WiFi yet, still finds one that's connected. |
| **The home server** | The home WiFi | Background only, while charging. It gives way to Meet and to the phone hotspot, and rides along with Sync. |

The household mostly no longer needs the radio at home: the server keeps it in step. Sync is for handing over the
household key, joining, and roommates who are out together.

### What keeps it safe

| | |
|---|---|
| Someone on the network reads the household, or poses as a roommate's X4 | Every request and answer is sealed (AES-256-GCM) with the household's token. Without it, nothing opens, and the server answers "403". |
| Someone replays an X4's old request | Each X4 numbers its requests and the server takes each number once ("409"). Answers are bound to the request they answer. |
| The server (or someone who took it over) | It has the token, so it holds the household's records (in `roomiepet-data`, readable by whoever can read that folder). It never has the household key: it can't read the WiFi passwords, and it can't make firmware, because X4s install only releases signed with the RoomiePet release key. |
| A server out on the internet | The same: plain HTTP is fine, because what matters is sealed or signed. A firewall or a reverse proxy is still a good idea. |
