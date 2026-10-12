#pragma once

// Which board this build is for. platformio.ini sets UNIDEX_X4=1 for the Xteink X4 (ESP32-C3, 4.26" 800x480
// panel, no PSRAM, no microphone, no clock chip); otherwise it's the Waveshare ESP32-S3-ePaper-1.54.
#ifndef UNIDEX_X4
#define UNIDEX_X4 0
#endif

// The name the device goes by on its screens and its WiFi. The X4 build is RoomiePet (the ChoreGotchi Ecosystem),
// built on unidex; the Waveshare build stays unidex. Code names (UNIDEX_*, the USB handshake) keep "unidex".
#define DEVICE_NAME (UNIDEX_X4 ? "RoomiePet" : "unidex")
#define DEVICE_SSID_PREFIX (UNIDEX_X4 ? "roomiepet" : "unidex")  // its WiFi, "roomiepet-1A2B": fits 15 characters

// Full CPU speed, for while a radio is on: the ESP32-S3 runs at 240 MHz, the X4's ESP32-C3 at 160 at most.
static const int CPU_FAST_MHZ = UNIDEX_X4 ? 160 : 240;
