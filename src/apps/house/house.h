#pragma once
#include <Arduino.h>
#include "house_logic.h"

// The household shared by roommates' Pets (house_logic.h), kept on the SD card (/house.dat). Shared by the House
// app (house.cpp) and the Pet, whose mood follows the chores.

// The household, loaded from the card on first use after a wake. False with no readable card.
bool houseLoad();
house::House &houseData();
void houseSave();     // after any change (writes the whole file, once)
uint32_t houseToday();  // local days since 1970, or 0 while the clock isn't set
void houseStart();    // a new household with this device's Pet in it
void houseJoin(uint32_t id, uint32_t clock);  // join someone's household (heard over the radio), after their clock
void houseLeave();    // forget the household on this device
void houseUpdateMe(); // this Pet's name and look onto its member record, if they changed
const char *houseCurrency();  // "$"
void houseFormatMoney(int32_t pence, char *out, size_t len);  // "$6.50"

// A chore this device's Pet just did (on the X4 or through the web app): EXP for the Pet, 10, or 15 when it was done
// by its due day. `before` is the chore as it was (nullptr if new). Nothing when it isn't this Pet's doing.
void houseCreditChore(const house::Record *before, const house::Record &after);

// The home screen's line, from what was last loaded (no card read).
void houseStatus(char *out, size_t len);

// How this device's Pet feels about the household, and what it says (empty if nothing).
enum class HouseFeeling : uint8_t { Normal, Proud, Sulky };
struct HouseMood {
  HouseFeeling feeling;
  char say[48];
};
HouseMood houseMood();

// News the Pet passes on once: a roommate did a chore (it cheers) or their Pet sent a message.
void houseAddNews(const char *text);
bool houseNews(char *out, size_t len);
void houseNewsSeen();
