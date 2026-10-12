#pragma once
#include <stdint.h>

// The House's phone page (Xteink X4 only, house/phone.cpp): roommates' Cash App links to open or share, and a box
// to paste a payment request made in Cash App. Served over the device's own hotspot (core/hotspot.h).
bool housePhoneStart();  // false if the WiFi is busy
void housePhoneStop();
bool housePhoneOn();
void housePhonePoll();         // call often while on; it stops itself after a few idle minutes
uint32_t housePhoneChanges();  // requests saved or removed from the phone since it started
