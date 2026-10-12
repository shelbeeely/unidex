#pragma once
#include <FS.h>

// Xteink X4 only: the SD card through the FreeInk SDK (sdkcard.cpp). storage.cpp is the only user.
fs::FS &sdkCard();
bool sdkCardBegin();  // mount (true if a readable FAT card is in); retried on the next call if not
void sdkCardEnd();
uint64_t sdkCardTotal();
uint64_t sdkCardUsed();
