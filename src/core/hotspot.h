#pragma once
#include <Arduino.h>
#include <WiFi.h>

// The device's own WiFi hotspot with a tiny web server, for a phone nearby (the House's phone page). Nothing goes
// through the internet. A new password every time it starts; the screen shows it as a QR code the phone's camera
// joins with, and the phone's internet check is answered with the page, so it opens by itself. Never at the same
// time as normal WiFi or the device-to-device radio. It keeps the device awake while on and stops itself after a
// few minutes with nobody using it.

struct HotspotRequest {
  String path, query;
  String arg(const char *name) const;  // a query value, %XX- and +-decoded
};

// Answers one request: write a whole HTTP response with hotspotReply (a GET; anything else is turned away).
typedef void (*HotspotHandler)(WiFiClient &client, const HotspotRequest &request);

bool hotspotStart(HotspotHandler handler);  // false if the WiFi is busy
void hotspotStop();
bool hotspotOn();
void hotspotPoll();      // call often while on: answers the phone; stops after the idle time
void hotspotActivity();  // a button press counts as use
const char *hotspotSsid();      // "roomiepet-1A2B" (unidex-1A2B off the X4)
const char *hotspotPassword();
const char *hotspotJoinCode();  // what the QR code holds: WIFI:T:WPA;S:<ssid>;P:<password>;;
uint32_t hotspotServed();       // pages served since it started

void hotspotReply(WiFiClient &c, int code, const char *type, const char *body, size_t len,
                  const String &headers = "");
void hotspotReply(WiFiClient &c, int code, const char *type, const String &body, const String &headers = "");
