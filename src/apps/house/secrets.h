#pragma once
// The household's secrets on the X4 (RoomiePet, secure_logic.h): its household key, the WiFi networks roommates
// share (sealed with it), and handing the key to a roommate's X4 at the Sync screen. The Waveshare board has none.
#include <Arduino.h>
#include "../../core/link.h"
#include "../../core/input.h"

bool secretsHaveKey();
uint32_t secretsFingerprint();      // 0 without a key
bool secretsKey(uint8_t out[32]);   // false without one
void secretsMakeKey();              // a new household key (a new household, or the web app's "Make" when none has one)
void secretsForget();               // leaving or joining a household: its key and networks go

// The Sync screen, while its radio is on: every packet goes to secretsHeard first (true if it was one of these);
// secretsTick once a loop asks for the key (without one), passes networks on and repeats a GIVE.
void secretsSyncStart();
bool secretsHeard(const LinkPacket &p);
void secretsTick();
// A question for this X4's owner: "Give Biscuit's X4 the household key?" or "Take the household key from Pip's
// X4?", with the code both screens show. False when there's none.
bool secretsQuestion(char *title, size_t titleLen, char *line, size_t lineLen, int &code);
void secretsAnswer(bool yes);
bool secretsAsking();  // a question waiting for an answer

// The shared networks, opened, for connecting: false past the last one.
bool secretsNetwork(int i, char *ssid, char *pass);
// Merging networks from the home server (home.cpp): only ones sealed with this household's key are taken.
bool secretsMergeNetwork(const uint8_t *encoded);  // secure_logic.h's NETWORK_LEN bytes; true if it changed the list
int secretsNetworkCount();
void secretsEncodeNetwork(int i, uint8_t *out);

// USB and the web app:
//   KEY GET              -> "OK KEY <fingerprint, 8 hex digits, or none>"
//   KEY MAKE             -> "OK KEY MAKE <fingerprint>" or "ERR" (it has one, or isn't in a household)
//   WIFI LIST            -> "WF <id> <ssid hex> <added by: Pet id>" per network, "OK WIFI LIST <count>"
//   WIFI ADD <ssid hex> <password hex>  -> "OK WIFI ADD <id>" or "ERR" (no key, too long, full); the same SSID
//                                          again changes its password
//   WIFI DEL <id>        -> "OK WIFI DEL" or "ERR"
bool secretsUsb(const char *line);
