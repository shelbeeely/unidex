#pragma once
// The household's home plan on the X4 (RoomiePet, plan_logic.h): the rooms roommates drew in the web app, kept on the
// card, passed on at Sync and through the home server, sealed (plan_seal.h). Until anyone draws theirs, the template
// sized to the household. The Waveshare board has none.
#include <Arduino.h>
#include "plan_logic.h"
#include "../../core/link.h"

int planRooms(plan::Room *out, int max);     // the rooms (out: plan::MAX_ROOMS fits); the template until there's a plan
bool planRoom(uint32_t id, plan::Room &out);
bool planTidy(uint32_t roomId);              // this X4's owner tidied it today (a TIDY record); true if it changed

// The household's Sync (house.cpp), while its radio is on: as secrets.h's.
void planSyncStart();
bool planHeard(const LinkPacket &p);  // true if it was a plan packet
void planTick();

// The home server (home.cpp): the sealed rooms to send (plan_seal.h's ENTRY_LEN bytes each), and ones it sent back.
int planEntryCount();
void planEncodedEntry(int i, uint8_t *out);
bool planMergeEntry(const uint8_t *encoded);  // true if it changed the plan

// USB and the web app (rooms as plan_logic.h's encodeRoom bytes, in hex):
//   PLAN LIST            -> "PR <room hex>" per room, "OK PLAN LIST <count> <0 template / 1 drawn>"
//   PLAN SET <room hex>  -> "OK PLAN SET" or "ERR" (not a proper room, or someone else's bedroom)
//   PLAN DEL <id hex>    -> "OK PLAN DEL" or "ERR"
//   TIDY <room id hex>   -> "OK TIDY" (tidied today: everyday mess there goes)
bool planUsb(const char *line);
