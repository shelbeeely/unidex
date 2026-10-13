#pragma once
// The release key's public half (tools/ota/keygen.py): X4s take radio updates signed with it only (ota_logic.h).
// Not made yet: until it is, X4s neither offer nor take updates over the radio (USB updates work as ever).
#include <stdint.h>

#define OTA_KEY_SET 0
static const uint8_t OTA_PUBLIC_KEY[65] = {0};
