#pragma once
#include <stdint.h>

// Manage on phone (Xteink X4 only, house/phone.cpp): the web app (site/x4) served over the device's own hotspot
// (core/hotspot.h): the household, Cash App (open, share, paste requests), badges, the timetable and settings.
bool housePhoneStart();  // false if the WiFi is busy
void housePhoneStop();
bool housePhoneOn();
void housePhonePoll();         // call often while on; it stops itself after a few idle minutes
uint32_t housePhoneChanges();  // changes made from the phone since it started
