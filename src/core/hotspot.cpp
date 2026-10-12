#include "board.h"
#if UNIDEX_X4  // the House's phone page; the Waveshare board's Notes has its own (notes/phone.cpp)
#include "hotspot.h"
#include <DNSServer.h>
#include <esp_mac.h>
#include <esp_random.h>
#include <esp_wifi.h>
#include "net.h"
#include "power.h"

// The same small server as Notes' "Open on phone" (src/apps/notes/phone.cpp), without the notes: several
// connections at once (phones open a spare connection ahead of time and leave it empty), each request answered and
// closed straight away.

static const uint32_t IDLE_MS = 5 * 60000;
static const int CONNECTIONS = 6;
static const uint32_t REQUEST_WAIT_MS = 4000;  // an empty connection is dropped after this

static WiFiServer http(80, 8);
static DNSServer dns;  // every name the phone looks up points here, so its internet check finds the page
static HotspotHandler handler;
static bool on;
static uint32_t lastUse, served;
static char ssid[16], password[12], joinCode[64];

struct Connection {
  WiFiClient client;
  String head;
  uint32_t since;
};
static Connection connections[CONNECTIONS];

String HotspotRequest::arg(const char *name) const {
  const String all = "&" + query, key = String("&") + name + "=";
  const int at = all.indexOf(key);
  if (at < 0) return "";
  String raw = all.substring(at + key.length()), out;
  if (raw.indexOf('&') >= 0) raw = raw.substring(0, raw.indexOf('&'));
  for (size_t i = 0; i < raw.length(); i++) {
    if (raw[i] == '%' && i + 2 < raw.length()) out += (char)strtol(raw.substring(i + 1, i + 3).c_str(), nullptr, 16), i += 2;
    else out += raw[i] == '+' ? ' ' : raw[i];
  }
  return out;
}

void hotspotReply(WiFiClient &c, int code, const char *type, const char *body, size_t len, const String &headers) {
  const char *status = code == 200 ? "OK" : code == 204 ? "No Content" : code == 302 ? "Found" : code == 400 ? "Bad Request"
                                                                                               : "Not Found";
  const String head = "HTTP/1.1 " + String(code) + " " + status + "\r\nContent-Type: " + type +
                      "\r\nContent-Length: " + String(len) + "\r\nCache-Control: no-store\r\nConnection: close\r\n" +
                      headers + "\r\n";
  c.write((const uint8_t *)head.c_str(), head.length());
  if (len) c.write((const uint8_t *)body, len);
}

void hotspotReply(WiFiClient &c, int code, const char *type, const String &body, const String &headers) {
  hotspotReply(c, code, type, body.c_str(), body.length(), headers);
}

static void handle(WiFiClient &c, const String &head) {
  // "GET /path?query HTTP/1.1" and the Host header.
  const int sp1 = head.indexOf(' '), sp2 = head.indexOf(' ', sp1 + 1);
  const String method = sp1 < 0 ? "" : head.substring(0, sp1);
  const String target = sp1 < 0 || sp2 < 0 ? "/" : head.substring(sp1 + 1, sp2);
  HotspotRequest r;
  const int q = target.indexOf('?');
  r.path = q < 0 ? target : target.substring(0, q);
  r.query = q < 0 ? "" : target.substring(q + 1);
  String lower = head, host;
  lower.toLowerCase();
  const int h = lower.indexOf("\nhost:");
  if (h >= 0) {
    host = head.substring(h + 6, head.indexOf('\r', h + 6));
    host.trim();
    if (host.indexOf(':') >= 0) host = host.substring(0, host.indexOf(':'));
  }
  const String self = WiFi.softAPIP().toString();
  if (r.path == "/favicon.ico" || r.path.startsWith("/apple-touch-icon")) return hotspotReply(c, 204, "text/plain", "", 0);
  lastUse = millis();
  served++;
  if (host != self) {
    // The phone's internet check (captive.apple.com, /generate_204, /connecttest.txt) or a site typed by hand:
    // sending it here makes the phone open the page by itself.
    return hotspotReply(c, 302, "text/plain", "", 0, "Location: http://" + self + "/\r\n");
  }
  if (method != "GET") return hotspotReply(c, 400, "text/plain", String("GET only"));
  if (handler) handler(c, r);
}

static void serve() {
  for (WiFiClient incoming = http.accept(); incoming; incoming = http.accept()) {
    int slot = 0;
    for (int i = 0; i < CONNECTIONS; i++) {
      if (!connections[i].client) {
        slot = i;
        break;
      }
      if (connections[i].since < connections[slot].since) slot = i;  // all busy: the one waiting longest goes
    }
    connections[slot].client.stop();
    incoming.setNoDelay(true);
    connections[slot] = {incoming, "", millis()};
  }
  for (Connection &conn : connections) {
    if (!conn.client) continue;
    uint8_t buf[256];
    while (conn.client.available() && conn.head.length() < 4096) {
      const int n = conn.client.read(buf, sizeof buf);
      if (n <= 0) break;
      conn.head.concat((const char *)buf, n);
    }
    if (conn.head.indexOf("\r\n\r\n") >= 0) {
      handle(conn.client, conn.head);
    } else if (conn.client.connected() && millis() - conn.since < REQUEST_WAIT_MS && conn.head.length() < 4096) {
      continue;  // still waiting for the request
    }
    conn.client.stop();
    conn.client = WiFiClient();
    conn.head = "";
  }
}

bool hotspotStart(HotspotHandler h) {
  if (on) return true;
  if (netClaimed()) return false;
  handler = h;
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
  snprintf(ssid, sizeof ssid, "%s-%02X%02X", DEVICE_SSID_PREFIX, mac[4], mac[5]);
  // No look-alike characters (l/1, O/0), so it can be typed from the screen if the camera can't scan.
  static const char ALPHABET[] = "abcdefghjkmnpqrstuvwxyz23456789";
  for (size_t i = 0; i < sizeof password - 2; i++) password[i] = ALPHABET[esp_random() % (sizeof ALPHABET - 1)];
  password[sizeof password - 2] = 0;
  snprintf(joinCode, sizeof joinCode, "WIFI:T:WPA;S:%s;P:%s;;", ssid, password);
  netOff();
  setCpuFrequencyMhz(CPU_FAST_MHZ);
  WiFi.mode(WIFI_AP);
  if (!WiFi.softAP(ssid, password)) {
    WiFi.mode(WIFI_OFF);
    setCpuFrequencyMhz(80);
    return false;
  }
  WiFi.setSleep(false);
  esp_wifi_set_bandwidth(WIFI_IF_AP, WIFI_BW_HT20);  // a 20 MHz channel: phones join it more reliably
  http.begin();
  dns.start(53, "*", WiFi.softAPIP());
  powerHold();  // no light or deep sleep while the phone may be using it
  on = true, served = 0;
  lastUse = millis();
  return true;
}

void hotspotStop() {
  if (!on) return;
  dns.stop();
  for (Connection &conn : connections) conn.client.stop(), conn.client = WiFiClient(), conn.head = "";
  http.end();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  setCpuFrequencyMhz(80);
  powerRelease();
  on = false;
}

bool hotspotOn() { return on; }

void hotspotPoll() {
  if (!on) return;
  dns.processNextRequest();
  serve();
  powerAlive();
  if (millis() - lastUse > IDLE_MS) hotspotStop();
}

void hotspotActivity() { lastUse = millis(); }
const char *hotspotSsid() { return ssid; }
const char *hotspotPassword() { return password; }
const char *hotspotJoinCode() { return joinCode; }
uint32_t hotspotServed() { return served; }
#endif
