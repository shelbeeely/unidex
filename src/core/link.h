#pragma once
#include <stddef.h>
#include <stdint.h>

// Device-to-device radio (ESP-NOW): short packets straight to other unidex devices nearby, no router. Only on
// while something asks for it (the Pet's Meet screen); it owns the WiFi radio meanwhile (netClaim) and keeps the
// device awake. Each session uses a new random radio address, so a device can't be followed by its chip address.
struct LinkPacket {
  uint8_t mac[6];  // who sent it (their address for this session)
  int8_t rssi;     // signal strength in dBm (about -25 touching, -60 across a room), 0 if unknown
  uint32_t at;     // millis() when the radio received it (the main loop may get to it later)
  uint8_t len;
  uint8_t data[250];
};

// Which channel, and whether the WiFi stays on:
// - MEET (the Pet's Meet: any Pets, anywhere): the meeting channel (1) every device listens on, so two Pets at the
//   park, at work or at home always find each other. Meeting is in person and asked for, so it takes the WiFi over
//   (the home server's connection drops, and comes back after).
// - HOUSEHOLD (the House's Sync: roommates): connected to a WiFi (charging at home), it stays connected and talks on
//   that WiFi's channel (the chip has one radio: both must share it). Not connected, it starts on `channel` (the home
//   WiFi's, if one the household shares is in range: net.h's netHomeChannel; 0: the meeting channel), and the Sync
//   screen moves it (linkSetChannel) to look for roommates while it hears none.
enum class LinkMode : uint8_t { MEET, HOUSEHOLD };
const char *linkStart(LinkMode mode = LinkMode::MEET, uint8_t channel = 0);  // nullptr when on, else why not
void linkStop();
bool linkOn();
bool linkSharesWifi();             // on, and riding on a WiFi connection (its channel can't move)
uint8_t linkChannel();
bool linkSetChannel(uint8_t ch);   // false while it shares the WiFi
bool linkBroadcast(const uint8_t *data, size_t len);                // to every device in range
bool linkSend(const uint8_t mac[6], const uint8_t *data, size_t len);  // to one (it confirms it heard)
bool linkPoll(LinkPacket &out);  // the next packet received, if any (call from the main loop)
