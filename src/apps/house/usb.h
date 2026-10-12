#pragma once

// The household for the web app (site/x4), over USB or the X4's hotspot (Xteink X4 only). The web app runs the same
// rules (house_logic.h compiled to WebAssembly): it reads the household, makes changes as this device would (as its
// Pet, with its clock), and sends back the records it changed, which are merged exactly as at a Sync.
//   H DUMP        -> "HH <household id> <this Pet id> <clock> <next seq> <count>", "HR <hex record>" per record
//                    (house_logic.h's encodeRecord), then "OK H DUMP"; "ERR nocard" without a card
//   H PUT <hex>   -> merges one record: "OK H PUT <0 same|1 added|2 updated|3 older|4 full>" or "ERR"
//   H SAVE        -> writes the household to the card: "OK H SAVE"
//   H START       -> a new household with this Pet in it (when not in one): "OK H START" or "ERR"
//   H LEAVE       -> this device forgets the household: "OK H LEAVE"
bool houseUsb(const char *line);  // false if the line isn't one of these
