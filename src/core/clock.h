#pragma once
#include <time.h>

void clockBegin();        // once per boot: timezone, then load the PCF85063 time into system time
bool clockValid();
const char *clockSync();  // NTP over WiFi, radio off after; nullptr on success, else a short reason
void clockSet(time_t utc);
// When the time was last set from something that knows it (the internet, the USB sync, the clock chip): 0 if never
// since power-on. The ESP32's own timer drifts while it sleeps (the X4 has nothing else), so a roommate's device
// set more recently is the better clock (the House's Sync). clockAdopt sets the time from such a device and keeps
// its date, so two devices can't keep refreshing each other.
time_t clockTrustedAt();
void clockAdopt(time_t utc, time_t trustedAt);
void clockStatus(char *out, size_t len);  // chip registers + system time, for the USB `C` command
struct tm clockLocal();   // the system time in London time
