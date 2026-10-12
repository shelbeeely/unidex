// The House's phone page (Xteink X4): over the device's own hotspot, a phone sees each roommate's Cash App link
// (to open, share, copy or text) and pastes payment requests made in Cash App (house_logic.h's PAYLINK). The page
// itself is phone_page.h.
#include "../../core/board.h"
#if UNIDEX_X4
#include "phone.h"
#include "house.h"
#include "phone_page.h"
#include "../../core/hotspot.h"
#include "../pet/pet.h"

using namespace house;

static uint32_t changes;  // requests saved or removed from the phone since it started (the screen redraws)

static String jsonText(const char *s) {
  String out = "\"";
  for (; *s; s++) {
    if (*s == '"' || *s == '\\') out += '\\';
    if ((uint8_t)*s >= 0x20) out += *s;
  }
  return out + "\"";
}

// What the page shows: the roommates (what you owe each, or they owe you, their $cashtag and their request for
// you), and the requests this device made.
static String stateJson() {
  House &h = houseData();
  const Record *m[MAX_MEMBERS];
  int32_t bal[MAX_MEMBERS];
  Debt d[MAX_MEMBERS];
  const int n = members(h, m);
  balances(h, m, n, bal);
  const int nd = settleUp(bal, n, d, MAX_MEMBERS);
  String out = "{\"people\":[";
  bool first = true;
  for (int i = 0; i < n; i++) {
    const uint32_t id = m[i]->origin;
    if (id == h.me) continue;
    uint32_t owe = 0, owed = 0;
    for (int k = 0; k < nd; k++) {
      if (m[d[k].from]->origin == h.me && m[d[k].to]->origin == id) owe += d[k].pence;
      if (m[d[k].from]->origin == id && m[d[k].to]->origin == h.me) owed += d[k].pence;
    }
    const char *tag = payTagOf(h, id);
    const Record *req = activeRequest(h, id, h.me);
    char url[64] = "";
    if (req) requestLinkUrl(req->text, url, sizeof url);
    out += String(first ? "" : ",") + "{\"id\":" + String(id) + ",\"name\":" + jsonText(m[i]->text) +
           ",\"tag\":" + (tag ? jsonText(tag) : String("null")) + ",\"owe\":" + String(owe) + ",\"owed\":" +
           String(owed) + ",\"req\":" + jsonText(url) + ",\"reqAmt\":" + String(req ? req->a : 0) + "}";
    first = false;
  }
  out += "],\"mine\":[";
  first = true;
  for (int i = 0; i < h.count; i++) {
    const Record &r = h.r[i];
    if (!live(r, PAYLINK) || r.origin != h.me) continue;
    char url[64];
    requestLinkUrl(r.text, url, sizeof url);
    out += String(first ? "" : ",") + "{\"seq\":" + String(r.seq) + ",\"url\":" + jsonText(url) + ",\"amt\":" +
           String(r.a) + ",\"for\":" + jsonText(r.c ? nameOf(h, r.c) : "anyone") + "}";
    first = false;
  }
  return out + "]}";
}

static void answer(WiFiClient &c, bool ok, const char *msg) {
  hotspotReply(c, 200, "application/json",
               String("{\"ok\":") + (ok ? "1" : "0") + ",\"msg\":" + jsonText(msg) + ",\"state\":" + stateJson() + "}");
}

static void handle(WiFiClient &c, const HotspotRequest &r) {
  House &h = houseData();
  if (r.path == "/state.json") return hotspotReply(c, 200, "application/json", stateJson());
  if (r.path == "/add") {
    char code[TEXT + 1];
    const String text = r.arg("text");
    if (!parseRequestLink(text.c_str(), code, sizeof code))
      return answer(c, false, "That isn't a Cash App request link (cash.app/pay/link/...)");
    uint32_t pence = parseAmount(r.arg("amt").c_str());
    if (!pence) pence = parseAmount(text.c_str());  // "Request $15 for..." in Cash App's share text
    uint32_t forWhom = strtoul(r.arg("for").c_str(), nullptr, 10);
    if (forWhom && (!member(h, forWhom) || forWhom == h.me)) forWhom = 0;
    if (!addRequestLink(h, code, pence, forWhom)) return answer(c, false, "The household is full");
    houseSave();
    changes++;
    return answer(c, true, "Saved. Roommates get it at the next Sync.");
  }
  if (r.path == "/remove") {
    const uint16_t seq = strtoul(r.arg("seq").c_str(), nullptr, 10);
    for (int i = 0; i < h.count; i++)
      if (h.r[i].kind == PAYLINK && h.r[i].origin == h.me && h.r[i].seq == seq && !(h.r[i].flags & DELETED)) {
        remove(h, h.r[i]);
        houseSave();
        changes++;
        return answer(c, true, "Removed");
      }
    return answer(c, false, "Already gone");
  }
  // The page, with the state inside it in place of the marker.
  static const char MARK[] = "/*STATE*/null";
  const char *at = strstr(HOUSE_PAGE, MARK);
  const String state = stateJson();
  const size_t before = at - HOUSE_PAGE, after = sizeof HOUSE_PAGE - 1 - before - (sizeof MARK - 1);
  const String head = String("HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: ") +
                      String(before + state.length() + after) +
                      "\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n";
  c.write((const uint8_t *)head.c_str(), head.length());
  c.write((const uint8_t *)HOUSE_PAGE, before);
  c.write((const uint8_t *)state.c_str(), state.length());
  c.write((const uint8_t *)at + sizeof MARK - 1, after);
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
