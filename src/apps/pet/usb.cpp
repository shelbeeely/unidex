#include "../../core/usbsync.h"
#include "pet.h"
#include "pet_logic.h"
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
    usbOut().printf("OK P %05lx ", (unsigned long)petLookBits());
    for (const char c : petName()) usbOut().printf("%02x", (uint8_t)c);
    usbOut().print('\n');
  } else if (strncmp(cmd, "SET ", 4) == 0) {
    char lookHex[12] = "", nameHex[2 * MAX_NAME + 2] = "";
    String name;
    char *end;
    const int n = sscanf(cmd + 4, "%11s %25s", lookHex, nameHex);
    const unsigned long look = strtoul(lookHex, &end, 16);
    if (n < 1 || *end || !nameFromHex(n > 1 ? nameHex : "", name)) {
      usbOut().println("ERR");
      return true;
    }
    // Parts that don't exist, or haven't been earned yet, fall back to the default.
    storagePutInt("pet_look", pet::pack(pet::onlyUnlocked(pet::unpack(look), petExp())));
    if (name.length()) storagePutString("pet_name", name.c_str());
    else storageRemoveKey("pet_name");
    petChanged();
    usbOut().println("OK P SET");
  } else if (strcmp(cmd, "PARTS") == 0) {
    // Every part, for the web app's dress-up: "PP <layer> <index> <EXP needed, 0 free> <hex name>", then
    // "OK P PARTS <EXP>".
    for (int layer = 0; layer < pet::LAYERS; layer++) {
      const pet::LayerParts lp = pet::partsOf(layer);
      for (int i = 0; i < lp.count; i++) {
        usbOut().printf("PP %d %d %u ", layer, i, (unsigned)pet::expNeeded(layer, i));
        for (const char *c = lp.parts[i].name; *c; c++) usbOut().printf("%02x", (uint8_t)*c);
        usbOut().print('\n');
      }
    }
    usbOut().printf("OK P PARTS %lu\n", (unsigned long)petExp());
  } else {
    usbOut().println("ERR");
  }
  return true;
}
