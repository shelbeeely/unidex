#include "../../core/usbsync.h"
#include "pet.h"
#include "pet_logic.h"
#include "../house/house.h"
#include "../../core/storage.h"

static const size_t MAX_NAME = 12;

static int hexValue(char c) {
  return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
}

// A name from hex: printable ASCII only (the screen's font has nothing else), trimmed, at most MAX_NAME.
static bool nameFromHex(const char *hex, String &out) {
  out = "";
  const size_t n = strlen(hex);
  if (n % 2 || n / 2 > MAX_NAME) return false;
  for (size_t i = 0; i < n; i += 2) {
    const int hi = hexValue(hex[i]), lo = hexValue(hex[i + 1]);
    if (hi < 0 || lo < 0) return false;
    const char c = (char)(hi << 4 | lo);
    if (c < 0x20 || c > 0x7E) return false;
    out += c;
  }
  out.trim();
  return true;
}

bool petUsb(const char *line) {
  if (strncmp(line, "P ", 2) != 0) return false;
  const char *cmd = line + 2;
  if (strcmp(cmd, "GET") == 0) {
    usbOut().printf(UNIDEX_X4 ? "OK P %08lx " : "OK P %07lx ", (unsigned long)petLookBits());
    for (const char c : petName()) usbOut().printf("%02x", (uint8_t)c);
#if UNIDEX_X4
    usbOut().print(' ');
    for (const char c : petPronouns()) usbOut().printf("%02x", (uint8_t)c);
    usbOut().printf(" %02x", petFlag());
#endif
    usbOut().print('\n');
  } else if (strncmp(cmd, "SET ", 4) == 0) {
    // "-" for no name (or no pronouns), so the pronouns after an empty name aren't taken for the name.
    char lookHex[12] = "", nameHex[2 * MAX_NAME + 2] = "", pronounsHex[2 * pronouns::MAX + 2] = "", flagHex[4] = "";
    String name, said;
    char *end;
    const int n = sscanf(cmd + 4, "%11s %25s %25s %3s", lookHex, nameHex, pronounsHex, flagHex);
    const unsigned long look = strtoul(lookHex, &end, 16);
    if (strcmp(nameHex, "-") == 0) nameHex[0] = 0;
    if (strcmp(pronounsHex, "-") == 0) pronounsHex[0] = 0;
    if (n < 1 || *end || !nameFromHex(n > 1 ? nameHex : "", name) || !nameFromHex(pronounsHex, said)) {
      usbOut().println("ERR");
      return true;
    }
    // Parts that don't exist, or gifts not found yet, fall back to the default.
    pet::Owned owned;
    petOwned(owned.layer);
    storagePutInt("pet_look", pet::pack(pet::onlyOwned(pet::unpack(look), owned)));
    if (name.length()) storagePutString("pet_name", name.c_str());
    else storageRemoveKey("pet_name");
#if UNIDEX_X4
    if (n > 2) {  // pronouns, cleaned (pronouns.h); without the third word they stay as they are
      char clean[pronouns::MAX + 1];
      pronouns::clean(said.c_str(), clean);
      if (*clean) storagePutString("pet_pronouns", clean);
      else storageRemoveKey("pet_pronouns");
    }
    if (n > 3) {  // the pride flag byte (flags.h): its index, and 80 when the pin shows it
      const unsigned long f = strtoul(flagHex, &end, 16);
      storagePutInt("pet_flag", *end || f > 255 || (f & 0x7F) >= (unsigned long)flags::COUNT ? 0 : f);
    }
    houseUpdateMe();  // the household's member card takes the new name, look and pronouns
#endif
    petChanged();
    usbOut().println("OK P SET");
  } else if (strcmp(cmd, "PARTS") == 0) {
    // Every part, for the web app's dress-up: "PP <layer> <index> <rarity> <owned> <hex name>" (pet.h).
    pet::Owned owned;
    petOwned(owned.layer);
    for (int layer = 0; layer < pet::LAYERS; layer++) {
      const pet::LayerParts lp = pet::partsOf(layer);
      for (int i = 0; i < lp.count; i++) {
        usbOut().printf("PP %d %d %u %d ", layer, i, (unsigned)pet::rarityOf(layer, i), (int)pet::owns(owned, layer, i));
        for (const char *c = lp.parts[i].name; *c; c++) usbOut().printf("%02x", (uint8_t)*c);
        usbOut().print('\n');
      }
    }
    usbOut().printf("OK P PARTS %lu %d %d\n", (unsigned long)petExp(), pet::giftsOwned(owned), pet::DROP_COUNT);
  } else {
    usbOut().println("ERR");
  }
  return true;
}
