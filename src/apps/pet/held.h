#pragma once
// The things a Pet can hold (RoomiePet, the X4): the catalog (items_logic.h) the firmware brings (items.h) plus the
// ones this X4 was given, kept on the card (/pet/items.bin), and passing them on over the radio. On the Waveshare
// board the catalog is empty and nothing is sent.
#include "items_logic.h"

const items::Catalog &itemsCatalog();       // the X4 only
const items::Item *itemsFind(uint32_t id);  // nullptr for 0 or an item this X4 doesn't have (yet)
bool itemsAdd(const items::Item &it);        // a new item, saved to the card: false if known, broken or full
bool itemsRemove(uint32_t id);               // an item it was given (the firmware's stay)
uint32_t itemsDigest();                      // items_logic.h's Catalog::digest, 0 on the Waveshare board

// Sharing, while the radio is on (Meet, the household's Sync): every packet received goes to itemsHeard first (true
// if it was an item packet, new items kept); every HELLO heard tells itemsNoteDigest the other device's digest; and
// itemsBeat, once a beat, sends one item while a device heard in the last few seconds has a different catalog.
bool itemsHeard(const uint8_t *p, size_t len);
void itemsNoteDigest(uint32_t theirs);
void itemsBeat();
int itemsReceived();  // items received since start, for the screens ("+2 things to hold")

// USB and the web app (usbsync), lines starting "I ":
//   I LIST               -> "IT <id, 6 hex> <name hex> <picture, 64 hex> <0 firmware / 1 given>" per item,
//                           "OK I LIST <count> <digest, 8 hex>"
//   I ADD <name hex> <picture hex>  -> "OK I ADD <id>" or "ERR" (known already: "OK I ADD <id>" too)
//   I DEL <id hex>       -> "OK I DEL" or "ERR"
bool itemsUsb(const char *line);
