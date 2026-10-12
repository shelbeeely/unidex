#include "../../core/board.h"
#if UNIDEX_X4
#include "usb.h"
#include "house.h"
#include "../../core/usbsync.h"

using namespace house;

static int hexValue(char c) {
  return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
}

bool houseUsb(const char *line) {
  if (strncmp(line, "H ", 2) != 0) return false;
  const char *cmd = line + 2;
  Print &out = usbOut();
  if (!houseLoad()) {
    out.println("ERR nocard");
    return true;
  }
  House &h = houseData();
  if (strcmp(cmd, "DUMP") == 0) {
    out.printf("HH %lu %lu %lu %u %u\n", (unsigned long)h.id, (unsigned long)h.me, (unsigned long)h.clock,
               h.nextSeq, h.count);
    uint8_t rec[RECORD_LEN];
    char hex[2 * RECORD_LEN + 1];
    for (int i = 0; i < h.count; i++) {
      encodeRecord(h.r[i], rec);
      for (size_t k = 0; k < RECORD_LEN; k++) snprintf(hex + 2 * k, 3, "%02x", rec[k]);
      out.printf("HR %s\n", hex);
    }
    out.println("OK H DUMP");
  } else if (strncmp(cmd, "PUT ", 4) == 0) {
    const char *hex = cmd + 4;
    uint8_t rec[RECORD_LEN];
    bool ok = h.id && strlen(hex) == 2 * RECORD_LEN;
    for (size_t k = 0; ok && k < RECORD_LEN; k++) {
      const int hi = hexValue(hex[2 * k]), lo = hexValue(hex[2 * k + 1]);
      ok = hi >= 0 && lo >= 0;
      rec[k] = hi << 4 | lo;
    }
    Record r;
    if (!ok || !decodeRecord(rec, r)) {
      out.println("ERR");
      return true;
    }
    if (r.origin == h.me && r.seq > h.nextSeq) h.nextSeq = r.seq;  // made in the web app as this device
    out.printf("OK H PUT %d\n", (int)merge(h, r));
  } else if (strcmp(cmd, "SAVE") == 0) {
    houseSave();
    out.println("OK H SAVE");
  } else if (strcmp(cmd, "START") == 0) {
    if (h.id) {
      out.println("ERR");
    } else {
      houseStart();
      houseSave();
      out.println("OK H START");
    }
  } else if (strcmp(cmd, "LEAVE") == 0) {
    houseLeave();
    out.println("OK H LEAVE");
  } else {
    out.println("ERR");
  }
  return true;
}
#endif
