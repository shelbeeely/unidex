// Checks the things a Pet can hold (items_logic.h and the built-in catalog, items.h) on a computer:
//   c++ -std=c++17 -O2 -I src/apps/pet tools/itemtest/item_test.cpp -o /tmp/item_test && /tmp/item_test
#include <cstdio>
#include <cstring>
#include <string>
#include "items.h"

using namespace items;
static int failures;
static void check(bool ok, const char *what) {
  printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
  failures += !ok;
}

static Catalog withBuiltins() {
  Catalog c;
  c.builtin = BUILTIN_ITEMS, c.builtinCount = BUILTIN_ITEM_COUNT;
  return c;
}

int main() {
  bool builtinsOk = BUILTIN_ITEM_COUNT > 50;
  for (int i = 0; i < BUILTIN_ITEM_COUNT; i++) {
    const Item &it = BUILTIN_ITEMS[i];
    builtinsOk &= nameOk(it.name) && it.id == idOf(it.name, it.bits) && it.id && it.id <= 0xFFFFFF;
    for (int j = 0; j < i; j++) builtinsOk &= BUILTIN_ITEMS[j].id != it.id;
  }
  check(builtinsOk, "the firmware's items: proper names, ids made from name and picture, no two alike");
  check(strcmp(BUILTIN_ITEMS[0].name, "Broom") == 0, "catalog/items.txt's order: the broom first");

  // An id is the same wherever the item is made, and changes with the name or the picture.
  const Item a = make("Teapot", BUILTIN_ITEMS[3].bits), b = make("Teapot", BUILTIN_ITEMS[3].bits);
  Item c2 = a;
  c2.bits[5] ^= 1;
  check(a.id == b.id && a.id != make("Teapot2", a.bits).id && a.id != idOf(c2.name, c2.bits),
        "an id comes from the name and picture alone");

  Catalog cat = withBuiltins();
  const uint32_t before = cat.digest();
  check(cat.add(a) && cat.count() == BUILTIN_ITEM_COUNT + 1 && cat.find(a.id) && cat.digest() != before,
        "a new item is added, found, and changes the digest");
  check(!cat.add(a) && !cat.add(BUILTIN_ITEMS[0]), "an item already known isn't added twice");
  Item forged = make("Fake", BUILTIN_ITEMS[1].bits);
  forged.id ^= 1;
  check(!cat.add(forged), "an item whose id doesn't match its name and picture is refused");
  Item noName = a;
  noName.name[0] = 0;
  check(!cat.add(noName), "an item with no name is refused");
  check(!cat.remove(BUILTIN_ITEMS[0].id) && cat.remove(a.id) && cat.digest() == before,
        "an added item can be removed (the firmware's can't); the digest is back");

  // Two catalogs with the same items in a different order have the same digest.
  Catalog x = withBuiltins(), y = withBuiltins();
  const Item i1 = make("One", BUILTIN_ITEMS[5].bits), i2 = make("Two", BUILTIN_ITEMS[6].bits);
  x.add(i1), x.add(i2), y.add(i2), y.add(i1);
  check(x.digest() == y.digest(), "the digest doesn't depend on the order");

  // The radio: one item to a packet, checked.
  uint8_t pkt[PACKET];
  const size_t n = encode(i1, pkt);
  Item back;
  check(n == PACKET && n <= 250 && decode(pkt, n, back) && back.id == i1.id && strcmp(back.name, "One") == 0 &&
            memcmp(back.bits, i1.bits, BYTES) == 0,
        "an item survives the radio, in one packet");
  pkt[10 + NAME + 3] ^= 0x10;
  check(!decode(pkt, n, back) && isItemPacket(pkt, n), "a damaged picture is noticed (the id no longer matches)");
  check(!isItemPacket(pkt, n - 1), "a short packet isn't an item");

  // Sharing: two devices keep sending until each has the other's items.
  Catalog p = withBuiltins(), q = withBuiltins();
  for (int k = 0; k < 5; k++) p.add(make(("P" + std::to_string(k)).c_str(), BUILTIN_ITEMS[k].bits));
  for (int k = 0; k < 3; k++) q.add(make(("Q" + std::to_string(k)).c_str(), BUILTIN_ITEMS[10 + k].bits));
  uint32_t turnP = 0, turnQ = 0;
  int beats = 0;
  for (; beats < 1000 && p.digest() != q.digest(); beats++) {
    uint8_t buf[PACKET];
    Item got;
    encode(p.at(sendOrder(p, turnP++)), buf);
    if (decode(buf, PACKET, got)) q.add(got);
    encode(q.at(sendOrder(q, turnQ++)), buf);
    if (decode(buf, PACKET, got)) p.add(got);
  }
  check(p.digest() == q.digest() && p.count() == BUILTIN_ITEM_COUNT + 8 && beats <= 8,
        "two devices end up with each other's items, the given ones sent first");

  // Drawing in the Pet's hand: inside the 32x32 grid, on its lower right, with a paper edge round the ink.
  int inks = 0, papers = 0, minX = 99, minY = 99, maxX = -1, maxY = -1;
  drawHeld(BUILTIN_ITEMS[0], [&](int x, int y, bool ink) {
    (ink ? inks : papers)++;
    minX = x < minX ? x : minX, minY = y < minY ? y : minY, maxX = x > maxX ? x : maxX, maxY = y > maxY ? y : maxY;
  });
  check(inks > 10 && papers > inks / 2 && minX >= 15 && minY >= 15 && maxX <= 31 && maxY <= 31,
        "a held item is drawn at the lower right, with a paper edge");

  printf(failures ? "\n%d check(s) failed\n" : "\nall checks passed\n", failures);
  return failures ? 1 : 0;
}
