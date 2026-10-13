#pragma once
// The home server, from the X4 (RoomiePet, home_logic.h): while it's charging and a WiFi it knows is in range (its
// own, or one the household shares), it stays on that WiFi and checks in about once a minute. The household stays
// in step with roommates' X4s through the server, roommates' Pets that are home show on the Pet screen, and a newer
// signed release comes down from the server (ota.h checks it as ever). On battery it does nothing (the battery
// would last hours, not weeks). The radio (Meet, Sync) and the phone hotspot take the WiFi over whenever they start.
#include <Arduino.h>
#include "home_logic.h"

void homePoll();  // every loop (main.cpp)
// Roommates' Pets that checked in within the last few minutes (not this one): how many, up to max.
int homeAtHome(home::AtHome *out, int max);
void homeStatus(char *out, size_t len);  // "3 home", "no server found", "on battery", ...
// This X4 is on the home network: it checked in with the home server in the last 15 minutes (the virtual Home shows
// the whole house; away, only this Pet's bedroom).
bool homeOnHomeNetwork();

// USB and the web app:
//   HOME GET             -> "HM <pet id> <seconds ago> <flags>" per roommate home, then
//                           "OK HOME <server url hex, or -> <status hex>"
//   HOME SET <url hex or ->  -> "OK HOME SET" (the server's address, "http://host:port"; "-": find it on the WiFi)
//   HOME TOKEN           -> "OK HOME TOKEN <household id, 8 hex> <token, 64 hex>" for the server's setup, or "ERR"
//                           (no household key yet)
bool homeUsb(const char *line);
