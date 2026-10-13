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
bool otaEnabled();                     // Settings: take updates from other X4s (on unless turned off)
void otaSetEnabled(bool on);
// For Settings and the web app: "v1.5 ready", "getting v1.5 40%", "on", "off", or "unavailable".
void otaStatus(char *out, size_t len);
