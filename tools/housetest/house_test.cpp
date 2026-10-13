// Checks the household's records, merging, rota, money and radio packets on a computer (no board needed):
//   c++ -std=c++17 -O2 -DUNIDEX_X4=1 -I src/apps/house tools/housetest/house_test.cpp -o /tmp/house_test && /tmp/house_test
#include <cstdio>
#include <cstdlib>
#include <vector>
#include "house_logic.h"

using namespace house;
static int failures;

static void check(bool ok, const char *what) {
  printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
  failures += !ok;
}

static House *newHouse(uint32_t id, uint32_t me, const char *name) {
  House *h = (House *)calloc(1, sizeof(House));
  h->id = id;
  h->me = me;
  add(*h, MEMBER, name, 0x1234);
  return h;
}

// Every record of `from` sent to `to` as packets.
static int send(const House &from, House &to) {
  uint8_t buf[250];
  int changes = 0;
  for (int at = 0; at < from.count;) {
    int n;
    const size_t len = encodeRecords(from, at, buf, n);
    changes += mergePacket(to, buf, len, [](const Record &, const Record *) {});
    at += n;
  }
  return changes;
}

static bool same(const House &a, const House &b) {
  if (a.count != b.count) return false;
  for (int i = 0; i < a.count; i++) {
    bool found = false;
    for (int k = 0; k < b.count; k++)
      if (memcmp(&a.r[i], &b.r[k], sizeof(Record)) == 0) found = true;
    if (!found) return false;
  }
  return true;
}

static Record *find(House &h, uint8_t kind, const char *text) {
  for (int i = 0; i < h.count; i++)
    if (h.r[i].kind == kind && strcmp(h.r[i].text, text) == 0) return &h.r[i];
  return nullptr;
}

int main() {
  const uint32_t HOUSE = 77, PIP = 100, BISCUIT = 200, MOSS = 300;

  // ---- Two roommates sync; the order and repeats don't matter.
  House *a = newHouse(HOUSE, PIP, "Pip");
  House *b = newHouse(HOUSE, BISCUIT, "Biscuit");
  add(*a, ITEM, "Milk");
  add(*b, ITEM, "Bread");
  send(*a, *b);
  send(*b, *a);
  check(same(*a, *b), "after swapping copies both devices hold the same household");
  check(send(*a, *b) == 0 && send(*b, *a) == 0, "sending again changes nothing");
  const Record *m[MAX_MEMBERS];
  check(members(*a, m) == 2 && m[0]->origin == PIP && m[1]->origin == BISCUIT, "members in the order they joined");

  // ---- Concurrent edits: the later change wins everywhere.
  Record *milkA = find(*a, ITEM, "Milk"), *milkB = find(*b, ITEM, "Milk");
  milkA->a = 1;
  touch(*a, *milkA);  // Pip bought it
  remove(*b, *milkB);  // Biscuit deleted it
  const bool deleteIsNewer = newer(*milkB, *milkA);
  House *a2 = (House *)malloc(sizeof(House)), *b2 = (House *)malloc(sizeof(House));
  *a2 = *a, *b2 = *b;
  send(*a, *b);
  send(*b, *a);
  send(*b2, *a2);
  send(*a2, *b2);
  check(same(*a, *b) && same(*a, *a2) && same(*a, *b2), "a conflict settles the same way in either order");
  check(!!(find(*a, ITEM, "Milk")->flags & DELETED) == deleteIsNewer && find(*a, ITEM, "Milk")->a == !deleteIsNewer,
        "the later change wins");

  // ---- The rota: the turn moves on to the next member after whoever did it.
  House *c = newHouse(HOUSE, MOSS, "Moss");
  send(*a, *c);
  send(*c, *a);
  send(*a, *b);
  Record *bins = add(*a, CHORE, "Bins", 7, 20000);
  check(!overdue(*bins, 20000) && overdue(*bins, 20001), "a chore is overdue the day after it's due");
  bins->c = 0;
  const uint32_t first = turnOf(*a, *bins);
  check(first == PIP || first == BISCUIT || first == MOSS, "a new chore is someone's turn");
  choreDone(*a, *bins, 20002);
  check(bins->c == PIP && bins->b == 20009 && turnOf(*a, *bins) == BISCUIT, "Pip does the bins: due in 7 days, Biscuit next");
  send(*a, *b);
  Record *binsB = find(*b, CHORE, "Bins");
  choreDone(*b, *binsB, 20009);
  check(turnOf(*b, *binsB) == MOSS, "after Biscuit, Moss");
  binsB->c = MOSS;
  check(turnOf(*b, *binsB) == PIP, "after Moss, back to Pip");

  // ---- Money: shared by everyone who had joined by then.
  House *d = newHouse(HOUSE, PIP, "Pip");
  d->me = BISCUIT;
  add(*d, MEMBER, "Biscuit");
  d->me = PIP;
  add(*d, EXPENSE, "Groceries", PIP, 3000);  // Pip paid 30.00 for both
  d->me = MOSS;
  add(*d, MEMBER, "Moss");                   // Moss joins after that
  add(*d, EXPENSE, "Pizza", MOSS, 1000);     // Moss paid 10.00 for all three
  const int n = members(*d, m);
  int n2;
  int32_t bal[MAX_MEMBERS];
  balances(*d, m, n, bal);
  check(n == 3 && bal[0] == 1500 - 333 && bal[1] == -1500 - 333 && bal[2] == 1000 - 334,
        "an expense is split among those who had joined; odd pennies stay with the payer");
  check(bal[0] + bal[1] + bal[2] == 0, "balances add up to nothing");
  Debt debts[8];
  int nd = settleUp(bal, n, debts, 8);
  int32_t paid = 0;
  for (int i = 0; i < nd; i++) paid += debts[i].pence;
  check(nd == 2 && debts[0].from == 1 && paid == 1833, "Biscuit settles up with two payments");
  add(*d, SETTLE, nullptr, BISCUIT, 1167, PIP);
  add(*d, SETTLE, nullptr, BISCUIT, 666, MOSS);
  balances(*d, m, n, bal);
  check(bal[0] == 0 && bal[1] == 0 && bal[2] == 0, "after paying, nobody owes anything");
  check(settleUp(bal, n, debts, 8) == 0, "nothing left to settle");

  // ---- Joining late: a member who joins after an expense (its clock taken from the household's) doesn't share it.
  House *late = newHouse(HOUSE, PIP, "Pip");
  add(*late, EXPENSE, "Rent", PIP, 90000);
  House *joiner = (House *)calloc(1, sizeof(House));
  joiner->id = HOUSE, joiner->me = MOSS;
  tick(*joiner, late->clock);  // from Pip's hello
  add(*joiner, MEMBER, "Moss");
  send(*late, *joiner);
  send(*joiner, *late);
  n2 = members(*late, m);
  balances(*late, m, n2, bal);
  check(n2 == 2 && bal[1] == 0, "a roommate who joins later owes nothing for what was spent before");

  // ---- Messages: only the newest ten are kept, wherever they come from.
  House *e = newHouse(HOUSE, PIP, "Pip");
  for (int i = 0; i < 14; i++) add(*e, MESSAGE, "Hi");
  check(countKind(*e, MESSAGE) == MAX_MESSAGES, "ten messages kept");
  House *f = newHouse(HOUSE, BISCUIT, "Biscuit");
  send(*e, *f);
  check(countKind(*f, MESSAGE) == MAX_MESSAGES, "and ten after a sync");

  // ---- A full household makes room by dropping the oldest removed records.
  House *full = newHouse(HOUSE, PIP, "Pip");
  while (full->count < (int)MAX_RECORDS) add(*full, ITEM, "Thing");
  check(add(*full, ITEM, "One more") == nullptr, "a full household with nothing removed takes no more");
  remove(*full, full->r[5]);
  check(add(*full, ITEM, "One more") != nullptr && full->count == (int)MAX_RECORDS, "removing one makes room");

  // ---- Whose time to trust.
  const uint32_t T = 1750000000;
  check(timeIsBetter(0, 0, T, T - 60), "an unset clock takes a roommate's time");
  check(!timeIsBetter(0, 0, T, 0), "but not one that was never set from a good source");
  check(timeIsBetter(T, T - 86400, T + 300, T - 600), "a clock set a day ago takes one set 10 min ago");
  check(!timeIsBetter(T, T - 86400, T + 10, T - 600), "a few seconds apart isn't worth a change");
  check(!timeIsBetter(T + 300, T - 600, T, T - 86400), "the better clock keeps its own time");
  check(!timeIsBetter(T, T - 600, T + 300, T - 600), "two clocks set at the same time leave each other alone");

  // ---- Cash App $cashtags.
  check(validCashtag("pip") && validCashtag("$Pip_2024") && validCashtag("a-b"), "ordinary cashtags are valid");
  check(!validCashtag("") && !validCashtag("$") && !validCashtag("1234") && !validCashtag("has space") &&
            !validCashtag("abcdefghijklmnopqrstu"),
        "empty, all digits, spaces and over 20 characters are not");
  House *p1 = newHouse(HOUSE, PIP, "Pip");
  House *p2 = newHouse(HOUSE, BISCUIT, "Biscuit");
  check(payTagOf(*p1, PIP) == nullptr, "no cashtag until one is set");
  check(setPayTag(*p1, "$pipcash") && strcmp(payTagOf(*p1, PIP), "pipcash") == 0, "set your own cashtag");
  check(!setPayTag(*p1, "no way!"), "an invalid cashtag is refused");
  send(*p1, *p2);
  check(payTagOf(*p2, PIP) && strcmp(payTagOf(*p2, PIP), "pipcash") == 0, "a roommate's device learns it at Sync");
  setPayTag(*p1, "pip2");
  send(*p1, *p2);
  check(strcmp(payTagOf(*p2, PIP), "pip2") == 0, "a change reaches them too");
  // Biscuit's device tries to change Pip's cashtag: refused, on Biscuit's own device and on Pip's.
  for (int i = 0; i < p2->count; i++)
    if (p2->r[i].kind == PAYTAG) {
      strcpy(p2->r[i].text, "biscuitcash");
      touch(*p2, p2->r[i]);
    }
  check(payTagOf(*p2, PIP) == nullptr, "a cashtag changed by someone else isn't shown");
  send(*p2, *p1);
  check(strcmp(payTagOf(*p1, PIP), "pip2") == 0, "nor accepted by its owner's device");
  House *p3 = newHouse(HOUSE, MOSS, "Moss");
  send(*p2, *p3);
  send(*p1, *p3);
  check(strcmp(payTagOf(*p3, PIP), "pip2") == 0, "a third device keeps the owner's, whichever order copies arrive");
  check(setPayTag(*p1, "") && payTagOf(*p1, PIP) == nullptr, "clearing it");
  send(*p1, *p3);
  check(payTagOf(*p3, PIP) == nullptr, "and the clearing spreads");
  {  // Catalog clothes (RoomiePet): the owner's own WEAR record, spread at Sync, no one else's changes taken
    uint32_t w[4] = {0, 0, 0, 0}, got[4];
    check(!setWear(*p1, w), "no clothes, no record");
    w[0] = 0x123456, w[3] = 0xABCDEF;
    check(setWear(*p1, w) && !setWear(*p1, w), "wearing a catalog hat and outfit: once");
    send(*p1, *p2);
    wearOf(*p2, PIP, got);
    check(got[0] == 0x123456 && got[1] == 0 && got[3] == 0xABCDEF, "a roommate's device learns them at Sync");
    for (int i = 0; i < p2->count; i++)
      if (p2->r[i].kind == WEAR) p2->r[i].a = 0x999999, touch(*p2, p2->r[i]);
    wearOf(*p2, PIP, got);
    check(got[0] == 0, "clothes changed by someone else aren't shown");
    send(*p2, *p1);
    wearOf(*p1, PIP, got);
    check(got[0] == 0x123456, "nor taken by their owner's device");
  }
  char link[64];
  cashAppLink("pip2", 650, true, link, sizeof link);
  check(strcmp(link, "https://cash.app/$pip2/6.50") == 0, "a link with the amount");
  cashAppLink("pip2", 650, false, link, sizeof link);
  check(strcmp(link, "https://cash.app/$pip2") == 0, "and without");
  cashAppLink("abcdefghijklmnopqrst", 9999999, true, link, sizeof link);
  check(strlen(link) <= 53, "the longest link fits a version 3 QR code (53 bytes)");

  // ---- Cash App payment requests pasted from Cash App.
  char code[32];
  check(parseRequestLink("https://cash.app/pay/link/mu3w9zc7", code, sizeof code) && strcmp(code, "mu3w9zc7") == 0,
        "a request link");
  check(parseRequestLink("Pay me $15 for foods https://cash.app/pay/link/mu3w9zc7 thanks", code, sizeof code) &&
            strcmp(code, "mu3w9zc7") == 0,
        "a request link inside Cash App's share text");
  check(!parseRequestLink("https://cash.app/$pip", code, sizeof code) &&
            !parseRequestLink("cash.app/pay/link/ab", code, sizeof code) && !parseRequestLink("", code, sizeof code),
        "not a request link");
  check(parseAmount("Request $15 for foods") == 1500 && parseAmount("6.5") == 650 && parseAmount("$0.99") == 99 &&
            parseAmount("none") == 0,
        "amounts in pasted text");
  House *q1 = newHouse(HOUSE, PIP, "Pip");
  q1->me = BISCUIT;
  add(*q1, MEMBER, "Biscuit");
  q1->me = MOSS;
  add(*q1, MEMBER, "Moss");
  q1->me = PIP;
  check(activeRequest(*q1, PIP, BISCUIT) == nullptr, "no request until one is pasted");
  addRequestLink(*q1, "forall1", 1500, 0);
  check(activeRequest(*q1, PIP, BISCUIT) && activeRequest(*q1, PIP, MOSS), "a request for anyone shows for everyone");
  addRequestLink(*q1, "forbisc", 650, BISCUIT);
  check(strcmp(activeRequest(*q1, PIP, BISCUIT)->text, "forbisc") == 0 &&
            strcmp(activeRequest(*q1, PIP, MOSS)->text, "forall1") == 0,
        "the newest request for you wins; others see the one for anyone");
  q1->me = BISCUIT;
  add(*q1, SETTLE, nullptr, BISCUIT, 650, PIP);
  check(activeRequest(*q1, PIP, BISCUIT) == nullptr && activeRequest(*q1, PIP, MOSS) != nullptr,
        "once Biscuit settles up, Biscuit's requests are spent; Moss still sees the one for anyone");
  House *q2 = newHouse(HOUSE, MOSS, "Moss");
  send(*q1, *q2);
  check(activeRequest(*q2, PIP, MOSS) && strcmp(activeRequest(*q2, PIP, MOSS)->text, "forall1") == 0,
        "requests reach roommates at Sync");
  for (int i = 0; i < q2->count; i++)  // Moss's device tries to swap Pip's request for its own link
    if (q2->r[i].kind == PAYLINK) {
      strcpy(q2->r[i].text, "mosslink");
      touch(*q2, q2->r[i]);
    }
  check(activeRequest(*q2, PIP, MOSS) == nullptr, "a request changed by someone else isn't shown");
  char url[64];
  requestLinkUrl("mu3w9zc7", url, sizeof url);
  check(strcmp(url, "https://cash.app/pay/link/mu3w9zc7") == 0, "the link back");

  // ---- Reminders.
  Record bin = {};
  bin.kind = REMINDER, bin.a = 0, bin.b = 2;  // Tuesdays
  Record rent = {};
  rent.kind = REMINDER, rent.a = 1, rent.b = 1;  // the 1st
  check(remindToday(bin, 2, 15) && !remindToday(bin, 3, 15), "weekly reminder on its weekday");
  check(remindToday(rent, 4, 1) && !remindToday(rent, 4, 2), "monthly reminder on its day");

  // ---- Packets.
  Hello hi = {HOUSE, PIP, 1700000000, 1699990000, 0xABCD, 4242, "Pip"}, back;
  uint8_t buf[250];
  size_t len = encodeHello(hi, buf);
  check(len <= 250 && decodeHello(buf, len, back) && back.id == PIP && back.time == hi.time && back.trusted == hi.trusted && back.clock == 4242 &&
            strcmp(back.name, "Pip") == 0 && back.house == HOUSE,
        "hello round trip");
  check(memcmp(buf, "RPH", 3) == 0, "a RoomiePet's household packets start RPH");
  uint8_t fromUnidex[250];
  memcpy(fromUnidex, buf, len);
  memcpy(fromUnidex, UNIDEX_MAGIC, 3);
  check(typeOf(fromUnidex, len) == 0 && !decodeHello(fromUnidex, len, back), "a unidex's household packets are ignored");
  buf[30] = 1;
  check(!decodeHello(buf, len, back), "a name that isn't printable is refused");
  int count;
  len = encodeRecords(*a, 0, buf, count);
  check(len <= 250 && count == (int)PER_PACKET, "three records fit in a packet");
  House *other = newHouse(99, MOSS, "Moss");
  check(mergePacket(*other, buf, len, [](const Record &, const Record *) {}) == 0,
        "records for another household are ignored");
  buf[HEADER] = 9;
  check(mergePacket(*b, buf, len, [](const Record &, const Record *) {}) == 0, "a wrong count is refused");

  // ---- News: the callback sees another member's chore.
  House *g = newHouse(HOUSE, BISCUIT, "Biscuit");
  send(*a, *g);
  Record *binsA = find(*a, CHORE, "Bins");
  choreDone(*a, *binsA, 20010);
  int praised = 0;
  for (int at = 0; at < a->count;) {
    int k;
    const size_t l = encodeRecords(*a, at, buf, k);
    mergePacket(*g, buf, l, [&](const Record &r, const Record *old) {
      if (r.kind == CHORE && old && r.d != old->d && r.c != g->me) praised++;
    });
    at += k;
  }
  check(praised == 1, "a roommate's finished chore is news");

  printf(failures ? "%d FAILED\n" : "all passed\n", failures);
  return failures != 0;
}
