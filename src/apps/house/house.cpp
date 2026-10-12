// House: the household shared by roommates' Pets. Chores on a rota, the shopping list, money spent for everyone,
// regular reminders, the Pets' messages, and Sync, which swaps the household with roommates' devices nearby over
// the radio (no internet). Everything is picked from lists, so two buttons are enough: A = next, B = pick,
// hold B = the screen's extra (add, remove), hold A = back.
//
// Cash App: each roommate sets their $cashtag on their own device (Roommates, B on yourself). Money then shows the
// person being paid's code as a QR code, which a phone scans to open Cash App: the X4 only shows it, and no payment
// goes through the device.
#include <esp_random.h>
#include <qrcode.h>
#include "house.h"
#include "../../core/app.h"
#include "../../core/clock.h"
#include "../../core/display.h"
#include "../../core/launcher.h"
#include "../../core/link.h"
#include "../../core/power.h"
#include "../../core/storage.h"
#include "../../core/theme.h"
#include "../pet/flags.h"
#include "../pet/pet.h"
#include "../pet/held.h"
#include "phone.h"
#include "../../core/hotspot.h"

using namespace house;

// ---- Presets: everything a roommate adds is picked from these (the records keep the text, so the website or a
// later version can add others).
static const char *const CHORES[] = {"Bins out",   "Recycling",   "Dishes",      "Hoover",   "Bathroom",
                                     "Kitchen",    "Mop floors",  "Laundry",     "Plants",   "Food shop",
                                     "Dusting",    "Fridge clear", "Towels",     "Windows"};
static const int EVERY_DAYS[] = {1, 2, 3, 7, 14, 30};
static const char *const EVERY[] = {"every day", "every 2 days", "every 3 days", "every week", "every 2 weeks",
                                    "every month"};
static const char *const ITEMS[] = {"Milk",       "Bread",          "Eggs",         "Butter",      "Cheese",
                                    "Pasta",      "Rice",           "Tea",          "Coffee",      "Sugar",
                                    "Fruit",      "Veg",            "Loo roll",     "Kitchen roll", "Bin bags",
                                    "Dish soap",  "Dishwasher tabs", "Laundry soap", "Hand soap",   "Toothpaste",
                                    "Cleaning spray", "Sponges",    "Foil",         "Snacks",      "Oil"};
static const char *const SPENT_ON[] = {"Groceries", "Takeaway", "Utilities", "Internet", "Rent",
                                       "Cleaning",  "Drinks",   "Household", "Other"};
static const char *const REMINDERS[] = {"Bin day",   "Recycling day", "Rent due",    "Bills due",
                                        "House meeting", "Meter reading", "Cleaner comes", "Landlord visit"};
static const char *const MESSAGES[] = {"Out of milk!",        "Bins go out tonight", "Thanks for cleaning!",
                                       "Rent due soon",       "Quiet please, exams", "Pizza night?",
                                       "A parcel came for you", "Who's home tonight?", "Washer is free",
                                       "Heating on or off?",  "Guests over tonight", "Great job, team!",
                                       "Love you all"};
static const char *const WEEKDAYS[] = {"Sundays", "Mondays", "Tuesdays", "Wednesdays", "Thursdays", "Fridays",
                                       "Saturdays"};
#define COUNT(a) (int)(sizeof(a) / sizeof((a)[0]))

enum Screen : uint8_t { NO_HOUSE, MENU, CHORES_LIST, SHOPPING, MONEY, REMINDER_LIST, MESSAGE_LIST, ROOMMATES,
                        PICK, AMOUNT, CONFIRM, SYNC, PAY, CASHTAG, PHONE };
#if UNIDEX_X4
// The X4 adds "Manage on phone": the web app (site/x4) on a phone, over the X4's own hotspot (house/phone.cpp).
enum MenuRow : uint8_t { M_CHORES, M_SHOPPING, M_MONEY, M_REMINDERS, M_MESSAGES, M_ROOMMATES, M_PHONE, M_SYNC, M_ROWS };
#else
enum MenuRow : uint8_t { M_CHORES, M_SHOPPING, M_MONEY, M_REMINDERS, M_MESSAGES, M_ROOMMATES, M_SYNC, M_ROWS };
#endif
// What the picker is choosing.
enum Pick : uint8_t { P_CHORE, P_EVERY, P_ITEM, P_SPENT, P_REMINDER, P_WHEN, P_MESSAGE };
enum Ask : uint8_t { A_DELETE, A_SETTLE, A_CLEAR_BOUGHT, A_LEAVE, A_REMOVE_MEMBER };

static const int ROW_H = 22, ROWS_SHOWN = 6;

RTC_DATA_ATTR static uint8_t screen, cursor, top, back;  // back: the list a picker or question returns to
RTC_DATA_ATTR static uint8_t pick, pickCursor, pickTop, firstChoice;  // firstChoice: a two-step add's first pick
RTC_DATA_ATTR static uint8_t ask;
RTC_DATA_ATTR static uint32_t askOrigin, askTo, askPence;  // the record or the payment the question is about
RTC_DATA_ATTR static uint16_t askSeq;
RTC_DATA_ATTR static uint8_t digit, digits[5];             // the amount: $ hundreds, tens, units . dimes, cents
// PAY: whose code is shown (payTo), for how much (payPence, 0 = no amount) and, from Money, who pays (payFrom).
RTC_DATA_ATTR static uint32_t payTo, payFrom, payPence;
RTC_DATA_ATTR static char tagPlaces[MAX_CASHTAG + 1];  // CASHTAG: the tag being entered, ' ' where empty
RTC_DATA_ATTR static uint8_t tagPlace;

// The amount on the end of the link (https://cash.app/$tag/6.50) is the form in common use but not one Cash App
// documents: if a phone opens Cash App without it, set this to false and the link is just the $cashtag.
static const bool AMOUNT_IN_LINK = true;

// ---- Rows of the list screens: indexes into the household's records, in display order.

static int rows[MAX_RECORDS];
static int rowCount;  // records listed; the screen's extra rows (add, clear) come after them

static int byDue(const void *x, const void *y) {
  const Record &a = houseData().r[*(const int *)x], &b = houseData().r[*(const int *)y];
  return a.b < b.b ? -1 : a.b > b.b;
}

static void listRecords() {
  House &h = houseData();
  rowCount = 0;
  const uint8_t kind = screen == CHORES_LIST ? CHORE : screen == SHOPPING ? ITEM : screen == REMINDER_LIST ? REMINDER
                       : screen == MESSAGE_LIST ? MESSAGE : screen == ROOMMATES ? MEMBER : 0;
  if (!kind) return;
  if (kind == MEMBER) {
    const Record *m[MAX_MEMBERS];
    const int n = members(h, m);
    for (int i = 0; i < n; i++) rows[rowCount++] = m[i] - h.r;
    return;
  }
  for (int pass = 0; pass < 2; pass++)  // shopping: still to buy, then bought
    for (int i = 0; i < h.count; i++)
      if (live(h.r[i], kind) && (kind != ITEM || (int)h.r[i].a == pass)) rows[rowCount++] = i;
  if (kind == CHORE) qsort(rows, rowCount, sizeof rows[0], byDue);
  if (kind == MESSAGE)  // newest first
    for (int i = 0; i < rowCount / 2; i++) {
      const int t = rows[i];
      rows[i] = rows[rowCount - 1 - i];
      rows[rowCount - 1 - i] = t;
    }
}

static bool anyBought() {
  for (int i = 0; i < rowCount; i++)
    if (houseData().r[rows[i]].a) return true;
  return false;
}

// The screen's rows after the records: "+ Add ..." (and "Clear bought" on the shopping list).
static int extraRows() {
  if (screen == ROOMMATES) return 0;
  if (screen == SHOPPING && anyBought()) return 2;
  return 1;
}

static void money(const Record **m, int &n, int32_t *bal, Debt *debts, int &nd) {
  n = members(houseData(), m);
  balances(houseData(), m, n, bal);
  nd = settleUp(bal, n, debts, MAX_MEMBERS);
}

static int totalRows() {
  if (screen == MENU) return M_ROWS;
  if (screen == MONEY) {
    const Record *m[MAX_MEMBERS];
    int32_t bal[MAX_MEMBERS];
    Debt d[MAX_MEMBERS];
    int n, nd;
    money(m, n, bal, d, nd);
    return nd + 1;
  }
  return rowCount + extraRows();
}

// ---- Drawing

// A row: `left` (after a pride flag, for a roommate who has one), `right` at the right edge.
static void drawRow(int slot, bool selected, const char *left, const char *right, uint8_t flag = 0) {
  const int16_t y = CONTENT_TOP + 3 + slot * ROW_H, baseline = y + ROW_H / 2 + 4;
  if (selected) display.fillRect(MARGIN - 4, y, display.width() - 2 * (MARGIN - 4), ROW_H - 2, BLACK);
  display.setTextColor(selected ? WHITE : BLACK);
  display.setFont(FONT_SMALL);
  const int16_t rightW = right && *right ? textWidth(right) + 8 : 0, flagW = flags::indexOf(flag) ? 19 : 0;
  if (flagW) petDrawFlag(flag, MARGIN, baseline - 9, 14, 9);
  display.setCursor(MARGIN + flagW, baseline);
  display.print(fitText(left, display.width() - 2 * MARGIN - rightW - flagW));
  if (rightW) drawRight(right, baseline);
  display.setTextColor(BLACK);
}

// Keeps the cursor's row in the window of rows shown.
static void scrollTo(uint8_t &cur, uint8_t &first, int total) {
  if (total <= 0) cur = 0;
  else if (cur >= total) cur = total - 1;
  if (cur < first) first = cur;
  if (cur >= first + ROWS_SHOWN) first = cur - ROWS_SHOWN + 1;
}

static void whenText(const Record &chore, uint32_t today, char *out, size_t len) {
  const char *who = turnOf(houseData(), chore) == houseData().me ? "you" : nameOf(houseData(), turnOf(houseData(), chore));
  if (!today) snprintf(out, len, "%s", who);
  else if (chore.b < today) snprintf(out, len, "%s, %lud late", who, (unsigned long)(today - chore.b));
  else if (chore.b == today) snprintf(out, len, "%s, today", who);
  else snprintf(out, len, "%s, %lud", who, (unsigned long)(chore.b - today));
}

static void scheduleText(const Record &r, char *out, size_t len) {
  if (r.a == 0) snprintf(out, len, "%s", WEEKDAYS[r.b % 7]);
  else snprintf(out, len, "%lu%s", (unsigned long)r.b,
                r.b % 10 == 1 && r.b != 11 ? "st" : r.b % 10 == 2 && r.b != 12 ? "nd" : r.b % 10 == 3 && r.b != 13 ? "rd" : "th");
}

static void drawMenu() {
  House &h = houseData();
  drawHeader("House");
  const uint32_t today = houseToday();
  char right[24];
  for (int i = top; i < M_ROWS && i < top + ROWS_SHOWN; i++) {
    right[0] = 0;
    int n = 0;
    switch (i) {
      case M_CHORES:
        for (int k = 0; k < h.count; k++)
          if (live(h.r[k], CHORE) && turnOf(h, h.r[k]) == h.me && today && h.r[k].b <= today) n++;
        if (n) snprintf(right, sizeof right, "%d yours", n);
        break;
      case M_SHOPPING:
        for (int k = 0; k < h.count; k++) n += live(h.r[k], ITEM) && !h.r[k].a;
        if (n) snprintf(right, sizeof right, "%d", n);
        break;
      case M_MONEY: {
        const Record *m[MAX_MEMBERS];
        int32_t bal[MAX_MEMBERS];
        Debt d[MAX_MEMBERS];
        int count, nd;
        money(m, count, bal, d, nd);
        for (int k = 0; k < count; k++)
          if (m[k]->origin == h.me && bal[k]) {
            char amount[16];
            houseFormatMoney(bal[k] < 0 ? -bal[k] : bal[k], amount, sizeof amount);
            snprintf(right, sizeof right, bal[k] < 0 ? "you owe %s" : "owed %s", amount);
          }
        break;
      }
      case M_REMINDERS:
        if (today) {
          const struct tm t = clockLocal();
          for (int k = 0; k < h.count; k++)
            if (live(h.r[k], REMINDER) && remindToday(h.r[k], t.tm_wday, t.tm_mday))
              snprintf(right, sizeof right, "%s today", h.r[k].text);
        }
        break;
      case M_MESSAGES:
        n = countKind(h, MESSAGE);
        if (n) snprintf(right, sizeof right, "%d", n);
        break;
      case M_ROOMMATES: {
        const Record *m[MAX_MEMBERS];
        snprintf(right, sizeof right, "%d", members(h, m));
        break;
      }
    }
    static const char *const NAMES[] = {"Chores", "Shopping", "Money", "Reminders", "Pet messages", "Roommates",
#if UNIDEX_X4
                                        "Manage on phone",
#endif
                                        "Sync with roommates"};
    drawRow(i - top, i == cursor, NAMES[i], right);
  }
  drawHints("next", "open", cursor == M_ROOMMATES ? "leave" : "");
}

static void drawList() {
  House &h = houseData();
  static const char *const TITLES[] = {"", "", "Chores", "Shopping", "Money", "Reminders", "Pet messages",
                                       "Roommates"};
  drawHeader(TITLES[screen]);
  const uint32_t today = houseToday();
  const int total = totalRows();
  char left[48], right[24];
  for (int i = top; i < total && i < top + ROWS_SHOWN; i++) {
    right[0] = 0;
    if (i >= rowCount) {
      const char *add = screen == CHORES_LIST ? "+ Add a chore" : screen == SHOPPING ? "+ Add to the list"
                        : screen == REMINDER_LIST ? "+ Add a reminder" : "+ Pet says...";
      if (screen == SHOPPING && i == rowCount + 1) add = "Clear bought";
      drawRow(i - top, i == cursor, add, "");
      continue;
    }
    const Record &r = h.r[rows[i]];
    strlcpy(left, r.text, sizeof left);
    if (screen == CHORES_LIST) whenText(r, today, right, sizeof right);
    else if (screen == SHOPPING && r.a) strlcpy(right, "got it", sizeof right);
    else if (screen == REMINDER_LIST) scheduleText(r, right, sizeof right);
    else if (screen == MESSAGE_LIST) snprintf(left, sizeof left, "%s: %s", nameOf(h, r.origin), r.text);
    else if (screen == ROOMMATES) {
      char pr[pronouns::MAX + 1];
      pronounsOf(r, pr);
      if (*pr) snprintf(left, sizeof left, "%s (%s)", r.text, pr);
      const char *tag = payTagOf(h, r.origin);
      snprintf(right, sizeof right, "%s%s%s%s", r.origin == h.me ? "you" : "", r.origin == h.me && tag ? ", " : "",
               tag ? "$" : "", tag ? tag : "");
      if (r.origin == h.me && !tag) strlcpy(right, "you, no $cashtag", sizeof right);
    }
    drawRow(i - top, i == cursor, left, right, screen == ROOMMATES ? (uint8_t)(r.d & 0xFF) : 0);
  }
  if (screen == CHORES_LIST && !today) drawToast("Set the clock (Settings or Sync)");
  const bool onRecord = cursor < rowCount;
  if (screen == CHORES_LIST) drawHints("next", onRecord ? "done" : "add", onRecord ? "remove" : "");
  else if (screen == SHOPPING)
    drawHints("next", onRecord ? (h.r[rows[cursor]].a ? "not got" : "got it") : cursor > rowCount ? "clear" : "pick",
              onRecord ? "remove" : "");
  else if (screen == REMINDER_LIST) drawHints("next", onRecord ? "" : "add", onRecord ? "remove" : "");
  else if (screen == MESSAGE_LIST) drawHints("next", onRecord ? "" : "pick", onRecord ? "remove" : "");
  else {
    const bool self = onRecord && h.r[rows[cursor]].origin == h.me;
    drawHints("next", self ? "$cashtag" : onRecord && payTagOf(h, h.r[rows[cursor]].origin) ? "Cash App" : "",
              onRecord && !self ? "remove" : "");
  }
}

static void drawMoney() {
  House &h = houseData();
  const Record *m[MAX_MEMBERS];
  int32_t bal[MAX_MEMBERS];
  Debt d[MAX_MEMBERS];
  int n, nd;
  money(m, n, bal, d, nd);
  drawHeader("Money");
  char left[48], amount[16];
  for (int i = top; i <= nd && i < top + ROWS_SHOWN; i++) {
    if (i == nd) {
      drawRow(i - top, i == cursor, "+ I paid for...", "");
      continue;
    }
    const char *from = m[d[i].from]->origin == h.me ? "You" : m[d[i].from]->text;
    snprintf(left, sizeof left, "%s %s %s", from, m[d[i].from]->origin == h.me ? "owe" : "owes",
             m[d[i].to]->origin == h.me ? "you" : m[d[i].to]->text);
    houseFormatMoney(d[i].pence, amount, sizeof amount);
    drawRow(i - top, i == cursor, left, amount);
  }
  if (!nd) {
    display.setFont(FONT_SMALL);
    drawCentered("Everyone is even", CONTENT_TOP + 3 + 2 * ROW_H + ROW_H / 2);
  }
  const bool onDebt = cursor < nd && (m[d[cursor].from]->origin == h.me || m[d[cursor].to]->origin == h.me);
  const bool payable = onDebt && (payTagOf(h, m[d[cursor].to]->origin) ||
                                  activeRequest(h, m[d[cursor].to]->origin, m[d[cursor].from]->origin));
  drawHints("next", cursor == nd ? "add" : onDebt ? (payable ? "pay" : "settled") : "",
            "");
}

// A QR code (version 3: 29 modules, 3 px each, about 1 cm on the X4) centred, its top at y0. False if it doesn't fit.
static bool drawQr(const char *text, int16_t y0) {
  QRCode qr;
  uint8_t modules[qrcode_getBufferSize(3)];
  if (qrcode_initText(&qr, modules, 3, ECC_LOW, text) != 0) return false;
  const int scale = 3, x0 = (display.width() - qr.size * scale) / 2;
  for (uint8_t y = 0; y < qr.size; y++)
    for (uint8_t x = 0; x < qr.size; x++)
      if (qrcode_getModule(&qr, x, y)) display.fillRect(x0 + x * scale, y0 + y * scale, scale, scale, BLACK);
  return true;
}

// Paying payTo: their Cash App request for the payer, pasted on their phone page (house/phone.cpp), or else their
// $cashtag's link, as a QR code a phone scans, with what it is under it.
static void drawPay() {
  House &h = houseData();
  const char *tag = payTagOf(h, payTo);
  const Record *req = activeRequest(h, payTo, payFrom ? payFrom : h.me);
  char title[32];
  snprintf(title, sizeof title, "Pay %s", payTo == h.me ? "you" : nameOf(h, payTo));
  drawHeader(title);
  if (!tag && !req) {
    drawEmpty("No $cashtag yet", payTo == h.me ? "Set yours on Roommates" : UNIDEX_X4 ? "They set it on their X4," : "They set it on their unidex,",
              payTo == h.me ? "(B on yourself)." : "then Sync.");
    drawHints("", payFrom ? "settled" : "", "");
    return;
  }
  char link[64], line[48], amount[16];
  if (req) requestLinkUrl(req->text, link, sizeof link);
  else cashAppLink(tag, payPence, AMOUNT_IN_LINK, link, sizeof link);
  drawQr(link, CONTENT_TOP + 5);
  const uint32_t shown = req && req->a ? req->a : payPence;
  houseFormatMoney(shown, amount, sizeof amount);
  if (req) snprintf(line, sizeof line, shown ? "Cash App request  %s" : "Cash App request", amount);
  else if (payPence) snprintf(line, sizeof line, "$%s  %s", tag, amount);
  else snprintf(line, sizeof line, "$%s", tag);
  display.setFont(FONT_SMALL);
  drawCenteredLine(line, CONTENT_TOP + 5 + 29 * 3 + 18);
  display.setFont(FONT_TINY);
  drawCenteredLine(payTo == h.me ? "They scan this to pay you" : "Scan with your phone's camera",
                   CONTENT_TOP + 5 + 29 * 3 + 33);
  drawHints("", payFrom ? "settled" : "", "");
}

// Entering this device's $cashtag a place at a time: A (and hold B) change the letter, B moves on, and on an empty
// place with nothing after it, saves.
static bool tagDoneAt(int pos) {
  if (pos >= (int)MAX_CASHTAG - 1) return true;
  for (int i = pos; i < (int)MAX_CASHTAG; i++)
    if (tagPlaces[i] != ' ') return false;
  return true;
}

static void tagFromPlaces(char *out) {
  int n = 0;
  for (int i = 0; i < (int)MAX_CASHTAG; i++)
    if (tagPlaces[i] != ' ') out[n++] = tagPlaces[i];
  out[n] = 0;
}

static void drawCashtag() {
  drawHeader("Your $cashtag");
  display.setFont(FONT_SMALL);
  drawCenteredLine("So roommates can pay you", CONTENT_TOP + 18);
  // The places, $ first, the current one boxed; empty places after the cursor aren't drawn.
  int last = tagPlace;
  for (int i = 0; i < (int)MAX_CASHTAG; i++)
    if (tagPlaces[i] != ' ' && i > last) last = i;
  char shown[MAX_CASHTAG + 2] = "$";
  for (int i = 0; i <= last; i++) shown[i + 1] = tagPlaces[i] == ' ' ? '_' : tagPlaces[i];
  shown[last + 2] = 0;
  const int16_t base = CONTENT_TOP + 60;
  int16_t x = (display.width() - textWidth(shown)) / 2;
  for (int i = 0; shown[i]; i++) {
    const char one[2] = {shown[i], 0};
    const int16_t w = textWidth(one);
    if (i == tagPlace + 1) {
      display.fillRect(x - 1, base - 14, w + 2, 19, BLACK);
      display.setTextColor(WHITE);
    }
    display.setCursor(x, base);
    display.print(one);
    display.setTextColor(BLACK);
    x = display.getCursorX();  // the font's own advance
  }
  display.setFont(FONT_TINY);
  drawCenteredLine(tagDoneAt(tagPlace) ? "B: save (all empty: none)" : "B: next letter", CONTENT_TOP + 92);
  drawHints("letter", tagDoneAt(tagPlace) ? "save" : "next", "previous letter");
}

static int pickCount() {
  switch (pick) {
    case P_CHORE: return COUNT(CHORES);
    case P_EVERY: return COUNT(EVERY);
    case P_ITEM: return COUNT(ITEMS);
    case P_SPENT: return COUNT(SPENT_ON);
    case P_REMINDER: return COUNT(REMINDERS);
    case P_WHEN: return 7 + 28;  // the weekdays, then the 1st to the 28th of the month
    default: return COUNT(MESSAGES);
  }
}

static void pickText(int i, char *out, size_t len) {
  switch (pick) {
    case P_CHORE: strlcpy(out, CHORES[i], len); break;
    case P_EVERY: strlcpy(out, EVERY[i], len); break;
    case P_ITEM: strlcpy(out, ITEMS[i], len); break;
    case P_SPENT: strlcpy(out, SPENT_ON[i], len); break;
    case P_REMINDER: strlcpy(out, REMINDERS[i], len); break;
    case P_WHEN:
      if (i < 7) snprintf(out, len, "Every week: %s", WEEKDAYS[i]);
      else {
        Record r = {};
        r.a = 1, r.b = i - 6;
        char day[8];
        scheduleText(r, day, sizeof day);
        snprintf(out, len, "Every month: the %s", day);
      }
      break;
    default: strlcpy(out, MESSAGES[i], len);
  }
}

static void drawPick() {
  static const char *const TITLES[] = {"Which chore?", "How often?", "Shopping", "Paid for", "Reminder", "When?",
                                       "Your Pet says"};
  drawHeader(TITLES[pick]);
  char text[40];
  const int n = pickCount();
  for (int i = pickTop; i < n && i < pickTop + ROWS_SHOWN; i++) {
    pickText(i, text, sizeof text);
    drawRow(i - pickTop, i == pickCursor, text, "");
  }
  drawHints("next", "pick", "previous");
}

static void drawAmount() {
  drawHeader("How much?");
  char d[5][2];
  for (int i = 0; i < 5; i++) d[i][0] = '0' + digits[i], d[i][1] = 0;
  const char *pieces[] = {houseCurrency(), d[0], d[1], d[2], ".", d[3], d[4]};
  static const int AT[] = {1, 2, 3, 5, 6};
  display.setFont(FONT_LARGE);
  // Centred, the digit being changed in a black box.
  String all;
  for (const char *p : pieces) all += p;
  int16_t x = (display.width() - textWidth(all.c_str())) / 2;
  for (int i = 0; i < 7; i++) {
    const int16_t w = textWidth(pieces[i]);
    const bool active = digit < 5 && AT[digit] == i;
    if (active) display.fillRect(x - 2, 50, w + 4, 36, BLACK);
    display.setTextColor(active ? WHITE : BLACK);
    display.setCursor(x, 78);
    display.print(pieces[i]);
    x += w;
  }
  display.setTextColor(BLACK);
  display.setFont(FONT_SMALL);
  char line[48];
  snprintf(line, sizeof line, "%s, shared by everyone", SPENT_ON[firstChoice % COUNT(SPENT_ON)]);
  drawCentered(line, 110);
  if (digit == 5) display.fillRect(70, 126, 60, 22, BLACK), display.setTextColor(WHITE);
  drawCentered("Save", 137);
  display.setTextColor(BLACK);
  drawHints("next", digit == 5 ? "save" : "+1", digit == 5 ? "" : "-1");
}

static void drawConfirm() {
  // The list it's about, with the question over it.
  const uint8_t was = screen;
  screen = back;
  if (screen == MONEY) drawMoney();
  else if (screen == MENU) drawMenu();
  else drawList();
  screen = was;
  char line[48], amount[16];
  houseFormatMoney(askPence, amount, sizeof amount);
  switch (ask) {
    case A_DELETE: drawSheet("Remove it?", "For everyone, at the", "next Sync."); break;
    case A_SETTLE:
      snprintf(line, sizeof line, "%s paid %s %s", askOrigin == houseData().me ? "You" : nameOf(houseData(), askOrigin),
               askTo == houseData().me ? "you" : nameOf(houseData(), askTo), amount);
      drawSheet("Settled up?", line);
      break;
    case A_CLEAR_BOUGHT: drawSheet("Clear bought?", "Takes what's been got", "off the list."); break;
    case A_REMOVE_MEMBER: drawSheet("Remove roommate?", "Their Pet leaves the", "rota and the money."); break;
    default: drawSheet("Leave the household?", "This device forgets it.", "The others keep it.");
  }
  drawHints("keep", "yes", "");
}

// ---- Sync: say hello and send every record, over and over, while listening for roommates' devices.

struct Heard {
  uint32_t id, house;
  char name[NAME + 1];
};
static Heard heard[MAX_MEMBERS];
static int heardCount, changes, sendAt;
static uint32_t nextHelloAt, nextSendAt, lastPressAt, savedAt, joinHouse, joinClock;
static char joinName[NAME + 1];
static uint32_t drawnAt;
static bool syncing, dirty, stale;  // stale: something to show that isn't on the screen yet
static const char *syncError;
static const uint32_t SYNC_STOP_MS = 90000;

static void syncStart() {
  heardCount = changes = sendAt = 0;
  joinHouse = joinClock = 0;
  dirty = false;
  nextHelloAt = nextSendAt = millis();
  lastPressAt = millis();
  syncError = linkStart();
  syncing = !syncError;
}

static void syncStop() {
  if (syncing) linkStop();
  syncing = false;
  if (dirty) houseSave();
  dirty = false;
}

static void sayHello() {
  House &h = houseData();
  Hello hi = {h.id,          h.me, clockValid() ? (uint32_t)time(nullptr) : 0, (uint32_t)clockTrustedAt(),
              petLookBits(), h.clock, "", itemsDigest()};
  strlcpy(hi.name, petName().c_str(), sizeof hi.name);
  if (!hi.name[0]) strlcpy(hi.name, "Pet", sizeof hi.name);
  uint8_t pkt[250];
  linkBroadcast(pkt, encodeHello(hi, pkt));
  itemsBeat();  // and a thing to hold, while a roommate's catalog differs (held.h)
}

static Redraw syncTick() {
  if (!syncing) return Redraw::None;
  House &h = houseData();
  const uint32_t now = millis();
  if (now - lastPressAt > SYNC_STOP_MS) {
    syncStop();
    return Redraw::Partial;
  }
  if ((int32_t)(now - nextHelloAt) >= 0) {
    sayHello();
    nextHelloAt = now + 450 + esp_random() % 100;  // a little jitter: two devices don't keep colliding
  }
  if (h.id && h.count && (int32_t)(now - nextSendAt) >= 0) {
    uint8_t pkt[250];
    int n;
    if (sendAt >= h.count) sendAt = 0;
    const size_t len = encodeRecords(h, sendAt, pkt, n);
    linkBroadcast(pkt, len);
    sendAt += n;
    nextSendAt = now + 40;
  }
  bool news = false;
  LinkPacket p;
  Hello hi;
  while (linkPoll(p)) {
    if (itemsHeard(p.data, p.len)) continue;  // a thing to hold, passed on (held.h)
    if (decodeHello(p.data, p.len, hi)) {
      if (hi.id == h.me) continue;
      itemsNoteDigest(hi.items);
      int k = 0;
      while (k < heardCount && heard[k].id != hi.id) k++;
      if (k == heardCount && heardCount < (int)MAX_MEMBERS) {
        heardCount++;
        news = true;
      }
      if (k < heardCount) {
        heard[k].id = hi.id, heard[k].house = hi.house;
        strlcpy(heard[k].name, hi.name, sizeof heard[k].name);
      }
      // A roommate's device knows the time better (this one's isn't set, or drifted while it slept): take it.
      const uint32_t mine = clockValid() ? time(nullptr) : 0;
      if (timeIsBetter(mine, clockTrustedAt(), hi.time, hi.trusted)) {
        clockAdopt(hi.time, hi.trusted);
        launcherToast(mine ? "Clock corrected" : "Clock set");
        news = true;
      }
      if (!h.id && hi.house && (!joinHouse || hi.house == joinHouse)) {
        news |= !joinHouse;
        joinHouse = hi.house;
        if (hi.clock > joinClock) joinClock = hi.clock;
        strlcpy(joinName, hi.name, sizeof joinName);
      }
    } else {
      const int got = mergePacket(h, p.data, p.len, [&](const Record &r, const Record *old) {
        char text[64];
        if (r.kind == CHORE && old && r.d != old->d && r.c && r.c != h.me && !(r.flags & DELETED)) {
          snprintf(text, sizeof text, "%s did %s! Yay!", nameOf(h, r.c), r.text);
          houseAddNews(text);
        } else if (r.kind == MESSAGE && !old && r.origin != h.me) {
          snprintf(text, sizeof text, "%s says: %s", nameOf(h, r.origin), r.text);
          houseAddNews(text);
        }
      });
      if (got) {
        changes += got;
        dirty = news = true;
      }
    }
  }
  if (dirty && now - savedAt > 5000) {
    houseSave();
    dirty = false;
    savedAt = now;
  }
  powerAlive();
  // At most one refresh every 1.5 s: a refresh holds up the loop, and the records keep coming meanwhile.
  stale |= news;
  if (!stale || now - drawnAt < 1500) return Redraw::None;
  stale = false;
  drawnAt = now;
  return Redraw::Partial;
}

static void drawSync() {
  House &h = houseData();
  drawHeader("Sync", syncing ? "radio on" : "");
  display.setFont(FONT_SMALL);
  if (syncError) {
    drawEmpty("Can't sync", syncError);
    drawHints("", "", "");
    return;
  }
  if (!syncing) {
    drawEmpty("Sync stopped", "B: start again");
    drawHints("", "start", "");
    return;
  }
  if (joinHouse && !h.id) {
    char line[48];
    snprintf(line, sizeof line, "Join %s's household?", joinName);
    drawEmpty("Found one!", line, "B: join");
    drawHints("", "join", "");
    return;
  }
  drawCenteredLine(UNIDEX_X4 ? "Hold near a roommate's X4" : "Hold near a roommate's unidex", CONTENT_TOP + 18);
  drawCenteredLine(h.id ? "to swap the household" : "to join their household", CONTENT_TOP + 34);
  // The Pets heard so far, side by side (in this household, or not yet in one).
  int shownPets = 0;
  for (int i = 0; i < heardCount && shownPets < 4; i++) {
    if (h.id && heard[i].house != h.id) continue;
    const int16_t cx = 25 + shownPets * 50;
    const Record *m = member(h, heard[i].id);
    petDraw(m ? m->a : petLookBits(), 1, cx - 16, CONTENT_TOP + 48, PetMood::Normal, nullptr, 0,
            m ? m->d >> 8 : petHeld());
    display.setFont(FONT_TINY);
    const String n = fitText(heard[i].name, 48);
    display.setCursor(cx - textWidth(n.c_str()) / 2, CONTENT_TOP + 94);
    display.print(n);
    shownPets++;
  }
  display.setFont(FONT_SMALL);
  char line[40];
  if (!shownPets) strlcpy(line, "Looking...", sizeof line);
  else snprintf(line, sizeof line, "%d change%s swapped", changes, changes == 1 ? "" : "s");
  drawCenteredLine(line, CONTENT_TOP + 118);
  drawHints("", "", "");
}

#if UNIDEX_X4
// ---- Manage on phone: the X4's hotspot, joined by scanning its QR code; the web app then opens by itself.

static uint32_t phoneSeen, phoneServedSeen;  // the phone page's changes and pages already on the screen

static void drawPhone() {
  drawHeader("Manage on phone", housePhoneOn() ? "on" : "");
  if (!housePhoneOn()) {
    drawEmpty("Stopped", "It stops after 5 minutes", "unused. B: start again");
    drawHints("", "start", "");
    return;
  }
  drawQr(hotspotJoinCode(), CONTENT_TOP + 5);
  display.setFont(FONT_TINY);
  drawCenteredLine(hotspotServed() ? "Connected: the page is open" : "Scan to join, the page opens", CONTENT_TOP + 104);
  char line[48];
  snprintf(line, sizeof line, "WiFi %s  password %s", hotspotSsid(), hotspotPassword());
  drawCenteredLine(line, CONTENT_TOP + 118);
  drawCenteredLine("Chores, money, Cash App, badges...", CONTENT_TOP + 132);
  drawHints("", "", "");
}

static Redraw phoneTick() {
  const bool wasOn = housePhoneOn();
  housePhonePoll();
  if (wasOn && !housePhoneOn()) return Redraw::Partial;  // stopped by itself
  if (housePhoneChanges() != phoneSeen) {
    phoneSeen = housePhoneChanges();
    launcherToast("Saved from the phone");
    return Redraw::Partial;
  }
  if ((hotspotServed() > 0) != (phoneServedSeen > 0)) {
    phoneServedSeen = hotspotServed();
    return Redraw::Partial;
  }
  return Redraw::None;
}
#endif

// ---- The app

static void draw() {
  if (!houseLoad()) return drawEmpty("No SD card", "The household is kept", "on the card.");
  if (screen != SYNC && screen != CONFIRM && !houseData().id) screen = NO_HOUSE;
  switch (screen) {
    case NO_HOUSE:
      drawHeader("House");
      drawEmpty("No household yet", "B: start one", "hold B: join a roommate's");
      drawHints("", "start", "join");
      return;
    case MENU: return drawMenu();
    case MONEY: return drawMoney();
    case PICK: return drawPick();
    case AMOUNT: return drawAmount();
    case CONFIRM: return drawConfirm();
    case SYNC: return drawSync();
#if UNIDEX_X4
    case PHONE: return drawPhone();
#endif
    case PAY: return drawPay();
    case CASHTAG: return drawCashtag();
    default: listRecords(); return drawList();
  }
}

static void open(uint8_t s) {
  screen = s;
  cursor = top = 0;
  listRecords();
}

static void startPick(uint8_t what) {
  back = screen == PICK || screen == AMOUNT ? back : screen;
  pick = what;
  pickCursor = pickTop = 0;
  screen = PICK;
}

static void askAbout(uint8_t what, const Record *r) {
  back = screen;
  ask = what;
  if (r) askOrigin = r->origin, askSeq = r->seq;
  screen = CONFIRM;
}

static Record *askedRecord() {
  House &h = houseData();
  for (int i = 0; i < h.count; i++)
    if (h.r[i].origin == askOrigin && h.r[i].seq == askSeq) return &h.r[i];
  return nullptr;
}

static uint32_t amountPence() {
  return digits[0] * 10000 + digits[1] * 1000 + digits[2] * 100 + digits[3] * 10 + digits[4];
}

// A choice in the picker: one-step adds save now, two-step ones go on to the next question.
static Redraw picked(int i) {
  House &h = houseData();
  char text[TEXT + 1];
  pickText(i, text, sizeof text);
  const uint32_t today = houseToday();
  switch (pick) {
    case P_CHORE:
      if (!today) {
        launcherToast("Set the clock first");
        open(back);
        return Redraw::Partial;
      }
      firstChoice = i;
      startPick(P_EVERY);
      return Redraw::Partial;
    case P_EVERY:
      add(h, CHORE, CHORES[firstChoice], EVERY_DAYS[i], today ? today : 0);
      launcherToast("Chore added");
      break;
    case P_ITEM:
      add(h, ITEM, text);
      launcherToast("On the list");
      break;
    case P_SPENT:
      firstChoice = i;
      memset(digits, 0, sizeof digits);
      digit = 1;
      screen = AMOUNT;
      return Redraw::Partial;
    case P_REMINDER:
      firstChoice = i;
      startPick(P_WHEN);
      return Redraw::Partial;
    case P_WHEN:
      if (i < 7) add(h, REMINDER, REMINDERS[firstChoice], 0, i);
      else add(h, REMINDER, REMINDERS[firstChoice], 1, i - 6);
      launcherToast("Reminder added");
      break;
    case P_MESSAGE: {
      add(h, MESSAGE, text);
      launcherToast("Sent at the next Sync");
      break;
    }
  }
  houseSave();
  open(back);
  return Redraw::Partial;
}

static Redraw listButton(Event e) {
  House &h = houseData();
  listRecords();
  const int total = totalRows();
  if (e == Event::AShort) {
    cursor = total ? (cursor + 1) % total : 0;
    scrollTo(cursor, top, total);
    return Redraw::Partial;
  }
  const bool onRecord = cursor < rowCount;
  Record *r = onRecord ? &h.r[rows[cursor]] : nullptr;
  if (e == Event::BLong) {
    if (!r || (screen == ROOMMATES && r->origin == h.me)) return Redraw::None;
    askAbout(screen == ROOMMATES ? A_REMOVE_MEMBER : A_DELETE, r);
    return Redraw::Partial;
  }
  if (e != Event::BShort) return Redraw::None;
  if (screen == ROOMMATES && r && r->origin == h.me) {  // your own $cashtag
    const char *tag = payTagOf(h, h.me);
    memset(tagPlaces, ' ', MAX_CASHTAG);
    tagPlaces[MAX_CASHTAG] = 0;
    for (int i = 0; tag && tag[i] && i < (int)MAX_CASHTAG; i++) tagPlaces[i] = tag[i] >= 'A' && tag[i] <= 'Z' ? tag[i] + 32 : tag[i];
    tagPlace = 0;
    back = ROOMMATES;
    screen = CASHTAG;
    return Redraw::Partial;
  }
  if (screen == ROOMMATES && r) {  // a roommate's code, to pay them anything
    if (!payTagOf(h, r->origin)) return Redraw::None;
    payTo = r->origin, payFrom = 0, payPence = 0;
    back = ROOMMATES;
    screen = PAY;
    return Redraw::Full;
  }
  if (!r) {
    if (screen == SHOPPING && cursor == rowCount + 1) {
      askAbout(A_CLEAR_BOUGHT, nullptr);
      return Redraw::Partial;
    }
    if (screen == ROOMMATES) return Redraw::None;
    startPick(screen == CHORES_LIST ? P_CHORE : screen == SHOPPING ? P_ITEM : screen == REMINDER_LIST ? P_REMINDER
                                                                                                      : P_MESSAGE);
    return Redraw::Partial;
  }
  if (screen == CHORES_LIST) {
    const uint32_t today = houseToday();
    if (!today) {
      launcherToast("Set the clock first");
      return Redraw::Partial;
    }
    const Record before = *r;
    choreDone(h, *r, today);
    const char *gift = houseCreditChore(&before, *r);  // maybe a gift for the Pet
    houseSave();
    char text[48];
    if (gift) strlcpy(text, gift, sizeof text);
    else snprintf(text, sizeof text, "Done! Well done, %s", petName().length() ? petName().c_str() : "you");
    launcherToast(text);
    return Redraw::Partial;
  }
  if (screen == SHOPPING) {
    r->a = !r->a;
    touch(h, *r);
    houseSave();
    return Redraw::Partial;
  }
  return Redraw::None;
}

static Redraw onButton(Event e) {
  if (screen == SYNC) lastPressAt = millis();
  if (!houseLoad()) return Redraw::None;
  House &h = houseData();
  switch (screen) {
    case NO_HOUSE:
      if (e == Event::BShort) {
        houseStart();
        houseSave();
        open(MENU);
        launcherToast("Household started");
        return Redraw::Full;
      }
      if (e == Event::BLong) {
        screen = SYNC;
        syncStart();
        return Redraw::Full;
      }
      return Redraw::None;
    case MENU:
      if (e == Event::AShort) {
        cursor = (cursor + 1) % M_ROWS;
        scrollTo(cursor, top, M_ROWS);
        return Redraw::Partial;
      }
      if (e == Event::BLong && cursor == M_ROOMMATES) {
        askAbout(A_LEAVE, nullptr);
        return Redraw::Partial;
      }
      if (e != Event::BShort) return Redraw::None;
#if UNIDEX_X4
      if (cursor == M_PHONE) {
        screen = PHONE;
        phoneSeen = 0;
        if (!housePhoneStart()) launcherToast("WiFi is busy");
        return Redraw::Full;
      }
#endif
      if (cursor == M_SYNC) {
        screen = SYNC;
        syncStart();
        return Redraw::Full;
      }
      {
        static const uint8_t TO[] = {CHORES_LIST, SHOPPING, MONEY, REMINDER_LIST, MESSAGE_LIST, ROOMMATES};
        open(TO[cursor]);
      }
      return Redraw::Full;
    case MONEY: {
      const Record *m[MAX_MEMBERS];
      int32_t bal[MAX_MEMBERS];
      Debt d[MAX_MEMBERS];
      int n, nd;
      money(m, n, bal, d, nd);
      if (e == Event::AShort) {
        cursor = (cursor + 1) % (nd + 1);
        scrollTo(cursor, top, nd + 1);
        return Redraw::Partial;
      }
      if (e != Event::BShort) return Redraw::None;
      if (cursor >= nd) {
        startPick(P_SPENT);
        return Redraw::Partial;
      }
      if (m[d[cursor].from]->origin != h.me && m[d[cursor].to]->origin != h.me) return Redraw::None;
      payFrom = m[d[cursor].from]->origin;
      payTo = m[d[cursor].to]->origin;
      payPence = d[cursor].pence;
      if (payTagOf(h, payTo) || activeRequest(h, payTo, payFrom)) {  // a way to pay them: show it, then "settled?"
        back = MONEY;
        screen = PAY;
        return Redraw::Full;
      }
      askAbout(A_SETTLE, nullptr);
      askOrigin = payFrom;  // who paid
      askTo = payTo;
      askPence = payPence;
      return Redraw::Partial;
    }
    case PAY:
      if (e != Event::BShort || !payFrom) return Redraw::None;
      screen = MONEY;
      askAbout(A_SETTLE, nullptr);
      askOrigin = payFrom;
      askTo = payTo;
      askPence = payPence;
      return Redraw::Full;
    case CASHTAG:
      if (e == Event::AShort || e == Event::BLong) {
        const char *at = strchr(CASHTAG_CHARS, tagPlaces[tagPlace]);
        const int n = sizeof CASHTAG_CHARS - 1, i = at ? (int)(at - CASHTAG_CHARS) : 0;
        tagPlaces[tagPlace] = CASHTAG_CHARS[((i + (e == Event::AShort ? 1 : -1)) % n + n) % n];
        return Redraw::Partial;
      }
      if (e != Event::BShort) return Redraw::None;
      if (!tagDoneAt(tagPlace)) {
        tagPlace++;
        return Redraw::Partial;
      }
      {
        char tag[MAX_CASHTAG + 1];
        tagFromPlaces(tag);
        if (!setPayTag(h, tag)) {
          launcherToast("Needs a letter in it");
          tagPlace = 0;
          return Redraw::Partial;
        }
        houseSave();
        launcherToast(tag[0] ? "Shared at the next Sync" : "$cashtag removed");
      }
      open(ROOMMATES);
      return Redraw::Full;
    case PICK: {
      const int n = pickCount();
      if (e == Event::AShort || e == Event::BLong) {
        pickCursor = (pickCursor + (e == Event::AShort ? 1 : n - 1)) % n;
        scrollTo(pickCursor, pickTop, n);
        return Redraw::Partial;
      }
      if (e == Event::BShort) return picked(pickCursor);
      return Redraw::None;
    }
    case AMOUNT:
      if (e == Event::AShort) digit = (digit + 1) % 6;
      else if (e == Event::BShort && digit == 5) {
        if (!amountPence()) {
          launcherToast("Nothing to share");
          return Redraw::Partial;
        }
        add(h, EXPENSE, SPENT_ON[firstChoice % COUNT(SPENT_ON)], h.me, amountPence());
        houseSave();
        launcherToast("Shared with everyone");
        open(MONEY);
      } else if (e == Event::BShort || e == Event::BLong)
        digits[digit] = (digits[digit] + (e == Event::BShort ? 1 : 9)) % 10;
      else return Redraw::None;
      return Redraw::Partial;
    case CONFIRM:
      if (e == Event::AShort) {
        screen = back;
        return Redraw::Partial;
      }
      if (e != Event::BShort) return Redraw::None;
      if (ask == A_LEAVE) {
        houseLeave();
        open(NO_HOUSE);
        return Redraw::Full;
      }
      if (ask == A_SETTLE) {
        add(h, SETTLE, nullptr, askOrigin, askPence, askTo);
        launcherToast("Settled");
      } else if (ask == A_CLEAR_BOUGHT) {
        for (int i = 0; i < h.count; i++)
          if (live(h.r[i], ITEM) && h.r[i].a) remove(h, h.r[i]);
      } else if (Record *r = askedRecord()) {
        remove(h, *r);
      }
      houseSave();
      screen = back;
      cursor = top = 0;
      return Redraw::Partial;
    case SYNC:
      if (e == Event::BShort && !syncing) {
        syncStart();
        return Redraw::Partial;
      }
      if (e == Event::BShort && joinHouse && !h.id) {
        houseJoin(joinHouse, joinClock);
        houseSave();
        launcherToast("Welcome home!");
        return Redraw::Full;
      }
      return Redraw::None;
#if UNIDEX_X4
    case PHONE:
      hotspotActivity();
      if (e == Event::BShort && !housePhoneOn()) {
        if (!housePhoneStart()) launcherToast("WiFi is busy");
        return Redraw::Partial;
      }
      return Redraw::None;
#endif
    default: return listButton(e);
  }
}

static Redraw onBack() {
  switch (screen) {
    case SYNC:
      syncStop();
      open(houseData().id ? MENU : NO_HOUSE);
      cursor = M_SYNC;
      return Redraw::Full;
#if UNIDEX_X4
    case PHONE:
      housePhoneStop();
      open(MENU);
      cursor = M_PHONE;
      scrollTo(cursor, top, M_ROWS);
      return Redraw::Full;
#endif
    case PICK:
    case AMOUNT:
    case CONFIRM:
    case PAY:
    case CASHTAG:  // leaves without saving
      open(back);
      return Redraw::Full;
    case MENU:
    case NO_HOUSE: return Redraw::Exit;
    default: {
      const uint8_t was = screen;
      open(MENU);
      static const uint8_t ROW[] = {0, 0, M_CHORES, M_SHOPPING, M_MONEY, M_REMINDERS, M_MESSAGES, M_ROOMMATES};
      if (was < sizeof ROW) cursor = ROW[was];
      scrollTo(cursor, top, M_ROWS);
      return Redraw::Full;
    }
  }
}

static void onEnter() {
  screen = MENU;
  cursor = top = 0;
  if (houseLoad()) houseUpdateMe();  // the Pet may have a new name or look since
}

static void onExit() {
  syncStop();
#if UNIDEX_X4
  housePhoneStop();
#endif
}

static Redraw tick() {
  if (screen == SYNC) return syncTick();
#if UNIDEX_X4
  if (screen == PHONE) return phoneTick();
#endif
  return Redraw::None;
}

static void status(char *out, size_t len) { houseStatus(out, len); }

extern const App houseApp = {"House", ICON_HOUSE, onEnter, onButton, draw, onExit, onBack, status, tick, true};
