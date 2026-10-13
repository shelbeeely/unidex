#pragma once
// Firmware updates between X4s over the radio (RoomiePet): see ota_logic.h for how they're made safe. They go while
// the radio is on (the Pet's Meet, the household's Sync): an X4 running a signed release offers it, an X4 on an older
// one takes it (a download can take a few sessions: it carries on where it stopped), checks it, and installs it the
// next time it goes to sleep. Off on the Waveshare board, in the test build, and until the release key is made
// (tools/ota/keygen.py).
#include <stddef.h>
#include <stdint.h>
#include "link.h"

void otaBoot();                        // end of setup(): a new release that started properly is kept
bool otaHeard(const LinkPacket &p);    // every packet the radio hears (Meet, Sync): true if it was an update's
void otaTick();                        // every loop while the radio is on: offers, asks, writes
void otaInstallIfReady();              // before deep sleep (power.cpp): restarts into a downloaded update
bool otaReady();                       // one is downloaded and checked (power.cpp installs it when idle on USB too)
bool otaEnabled();                     // Settings: take updates from other X4s (on unless turned off)
void otaSetEnabled(bool on);
// From the home server (src/apps/house/home.cpp): the release it offers (its 128-byte trailer: signature and all
// checked as over the radio); true if this X4 wants it. Then the bytes in order from otaStreamAt (UINT32_MAX: not
// getting one from the server), each piece to otaStreamWrite (false: start again). When the last arrives it's checked
// and waits to be installed, as over the radio. otaStreamStop keeps what came for next time.
bool otaStreamOffer(const uint8_t *trailer);
uint32_t otaStreamAt();
uint32_t otaStreamRelease();  // which release is coming, 0 if none
bool otaStreamWrite(const uint8_t *bytes, size_t n);
void otaStreamStop();
uint32_t otaRelease();  // this firmware's release number (0: not a release)
// For Settings and the web app: "v1.5 ready", "getting v1.5 40%", "on", "off", or "unavailable".
void otaStatus(char *out, size_t len);
