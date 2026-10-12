// Manage on phone (Xteink X4): the web app (site/x4, built into the firmware by tools/embed_web.py) over the X4's
// own hotspot. The page talks to /api, which runs the same text commands as the website over USB (usbRun in
// core/usbsync.h): the household, Cash App, badges, the timetable and settings.
#include "../../core/board.h"
#if UNIDEX_X4
#include "phone.h"
#include <x4_web.h>
#include "../../core/hotspot.h"
#include "../../core/usbsync.h"

static uint32_t changes;  // API calls that changed something since it started (the screen redraws)

static bool changesThings(const String &lines) {
  for (const char *cmd : {"H PUT", "H START", "H LEAVE", "G SET", "P SET", "T ", "W ", "B ", "BX "})
    if (lines.startsWith(cmd) || lines.indexOf(String("\n") + cmd) >= 0) return true;
  return false;
}

static void handle(WiFiClient &c, const HotspotRequest &r) {
  if (r.path == "/api") {
    const String lines = r.arg("c");
    const String out = usbRun(lines.c_str());
    if (changesThings(lines)) changes++;
    return hotspotReply(c, 200, "text/plain; charset=utf-8", out);
  }
  for (const WebFile &f : WEB_FILES)
    if (r.path == f.path || (r.path == "/index.html" && f.path[1] == 0)) {
      return hotspotReply(c, 200, f.type, (const char *)f.data, f.size, "Content-Encoding: gzip\r\n");
    }
  // Anything else (a link opened by hand): the app.
  hotspotReply(c, 302, "text/plain", "", 0, "Location: /\r\n");
}

bool housePhoneStart() {
  changes = 0;
  return hotspotStart(handle);
}
void housePhoneStop() { hotspotStop(); }
bool housePhoneOn() { return hotspotOn(); }
void housePhonePoll() { hotspotPoll(); }
uint32_t housePhoneChanges() { return changes; }
#endif
