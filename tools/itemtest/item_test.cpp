// Checks the catalog: things to hold and clothes (items_logic.h and the built-in catalog, items.h) on a computer:
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
    builtinsOk &= valid(it) && it.id && it.id <= 0xFFFFFF;
    for (int j = 0; j < i; j++) builtinsOk &= BUILTIN_ITEMS[j].id != it.id;
  }
  check(builtinsOk, "the firmware's items: proper names, kinds and shapes, ids made from them, no two alike");
  check(strcmp(BUILTIN_ITEMS[0].name, "Broom") == 0, "catalog/items.txt's order: the broom first");

  // An id is the same wherever the item is made, and changes with the name or the picture.
  const Item a = makeIcon("Teapot", HAND, BUILTIN_ITEMS[3].data), b = makeIcon("Teapot", HAND, BUILTIN_ITEMS[3].data);
  Item c2 = a;
  c2.data[5] ^= 1;
  check(a.id == b.id && a.id != makeIcon("Teapot2", HAND, a.data).id && a.id != idOf(c2) &&
            a.id != makeIcon("Teapot", HAT, a.data).id,
        "an id comes from the name, kind and picture alone");

  Catalog cat = withBuiltins();
  const uint32_t before = cat.digest();
  check(cat.add(a) && cat.count() == BUILTIN_ITEM_COUNT + 1 && cat.find(a.id) && cat.digest() != before,
        "a new item is added, found, and changes the digest");
  check(!cat.add(a) && !cat.add(BUILTIN_ITEMS[0]), "an item already known isn't added twice");
  Item forged = makeIcon("Fake", HAND, BUILTIN_ITEMS[1].data);
  forged.id ^= 1;
  check(!cat.add(forged), "an item whose id doesn't match its name and picture is refused");
  Item noName = a;
  noName.name[0] = 0;
  check(!cat.add(noName), "an item with no name is refused");
  check(!cat.remove(BUILTIN_ITEMS[0].id) && cat.remove(a.id) && cat.digest() == before,
        "an added item can be removed (the firmware's can't); the digest is back");

  // Two catalogs with the same items in a different order have the same digest.
  Catalog x = withBuiltins(), y = withBuiltins();
  const Item i1 = makeIcon("One", HAND, BUILTIN_ITEMS[5].data), i2 = makeIcon("Two", HAND, BUILTIN_ITEMS[6].data);
  x.add(i1), x.add(i2), y.add(i2), y.add(i1);
  check(x.digest() == y.digest(), "the digest doesn't depend on the order");

  // The radio: one item to a packet, checked.
  uint8_t pkt[PACKET_MAX];
  const size_t n = encode(i1, pkt);
  Item back;
  check(n == HEADER + ICON_BYTES && decode(pkt, n, back) && back.id == i1.id && strcmp(back.name, "One") == 0 &&
            memcmp(back.data, i1.data, ICON_BYTES) == 0,
        "an item survives the radio, in one packet");
  pkt[HEADER + 3] ^= 0x10;
  check(!decode(pkt, n, back) && isItemPacket(pkt, n), "a damaged picture is noticed (the id no longer matches)");
  encode(i1, pkt);
  check(!decode(pkt, n - 1, back) && !isItemPacket(pkt, HEADER - 1), "a short packet isn't an item");

  // Sharing: two devices keep sending until each has the other's items.
  Catalog p = withBuiltins(), q = withBuiltins();
  for (int k = 0; k < 5; k++) p.add(makeIcon(("P" + std::to_string(k)).c_str(), HAND, BUILTIN_ITEMS[k].data));
  for (int k = 0; k < 3; k++) q.add(makeIcon(("Q" + std::to_string(k)).c_str(), HAND, BUILTIN_ITEMS[10 + k].data));
  uint32_t turnP = 0, turnQ = 0;
  int beats = 0;
  for (; beats < 1000 && p.digest() != q.digest(); beats++) {
    uint8_t buf[PACKET_MAX];
    Item got;
    size_t len = encode(p.at(sendOrder(p, turnP++)), buf);
    if (decode(buf, len, got)) q.add(got);
    len = encode(q.at(sendOrder(q, turnQ++)), buf);
    if (decode(buf, len, got)) p.add(got);
  }
  check(p.digest() == q.digest() && p.count() == BUILTIN_ITEM_COUNT + 8 && beats <= 8,
        "two devices end up with each other's items, the given ones sent first");

  // Drawing in the Pet's hand: inside the 32x32 grid, on its lower right, with a paper edge round the ink.
  int inks = 0, papers = 0, minX = 99, minY = 99, maxX = -1, maxY = -1;
  drawItem(BUILTIN_ITEMS[0], [&](int x, int y, bool ink) {
    (ink ? inks : papers)++;
    minX = x < minX ? x : minX, minY = y < minY ? y : minY, maxX = x > maxX ? x : maxX, maxY = y > maxY ? y : maxY;
  });
  check(inks > 10 && papers > inks / 2 && minX >= 15 && minY >= 15 && maxX <= 31 && maxY <= 31,
        "a held item is drawn at the lower right, with a paper edge");

  // Clothes. A piece: rows of the Pet's grid, two bits a pixel, the largest still one packet.
  uint8_t rowsData[DATA];
  for (int i = 0; i < DATA; i++) rowsData[i] = (uint8_t)(i * 37 % 255) & 0x55;  // ink and see-through
  const Item big = makePiece("Big coat", OUTFIT, 5, MAX_ROWS, rowsData);
  uint8_t bigPkt[PACKET_MAX];
  const size_t bigLen = encode(big, bigPkt);
  Item bigBack;
  check(valid(big) && bigLen <= 250 && decode(bigPkt, bigLen, bigBack) && bigBack.id == big.id && bigBack.rows == MAX_ROWS,
        "the biggest piece (27 rows) fits one radio packet");
  check(!valid(makePiece("Too low", OUTFIT, 10, 25, rowsData)) && !valid(makeIcon("Held hair", HAIR, rowsData)) &&
            !valid(makePiece("Hand piece", HAND, 20, 4, rowsData)),
        "a piece must sit in the grid; hair and outfits are pieces; held things are icons");
  const Item *beret = nullptr, *crown = nullptr, *jumper = nullptr, *amulet = nullptr;
  for (int i = 0; i < BUILTIN_ITEM_COUNT; i++) {
    const Item &it = BUILTIN_ITEMS[i];
    if (!strcmp(it.name, "Beret")) beret = &it;
    if (!strcmp(it.name, "Crown")) crown = &it;
    if (!strcmp(it.name, "Heart jumper")) jumper = &it;
    if (!strcmp(it.name, "Amulet")) amulet = &it;
  }
  check(beret && beret->kind == HAT && beret->shape == PIECE && crown && crown->kind == HAT && crown->shape == ICON &&
            jumper && jumper->kind == OUTFIT && amulet && amulet->kind == ACCESSORY,
        "the firmware brings hats (a crown, a beret), accessories and an outfit");
  int crownLow = -1;
  drawItem(*crown, [&](int, int y, bool ink) { if (ink) crownLow = y > crownLow ? y : crownLow; });
  int amuletHigh = 99;
  drawItem(*amulet, [&](int, int y, bool ink) { if (ink) amuletHigh = y < amuletHigh ? y : amuletHigh; });
  check(crownLow == 9 && amuletHigh == 23, "a hat icon sits on the head (row 9), a necklace under the chin (row 23)");
  // Worn: a catalog piece takes its layer's place; an unknown id leaves the wardrobe's part.
  Catalog all = withBuiltins();
  auto find = [&](uint32_t id) { return all.find(id); };
  pet::Look withHat = pet::DEFAULT_LOOK;
  withHat.part[pet::HAT] = 1;  // the wardrobe's bow
  uint8_t g1[32][32] = {}, g2[32][32] = {}, g3[32][32] = {};
  Worn none = {}, beretOn = {}, unknown = {};
  beretOn.id[HAT] = beret->id, unknown.id[HAT] = 0x123456;
  drawPet(withHat, none, false, find, [&](int x, int y, bool ink) { g1[y][x] = ink ? 1 : 2; });
  drawPet(withHat, beretOn, false, find, [&](int x, int y, bool ink) { g2[y][x] = ink ? 1 : 2; });
  drawPet(withHat, unknown, false, find, [&](int x, int y, bool ink) { g3[y][x] = ink ? 1 : 2; });
  check(memcmp(g1, g2, sizeof g1) != 0 && memcmp(g1, g3, sizeof g1) == 0,
        "a worn beret replaces the bow; an item this device doesn't have shows the bow");
  Worn wrong = {};
  wrong.id[HAIR] = beret->id;  // a hat in the hair slot is ignored
  uint8_t g4[32][32] = {};
  drawPet(withHat, wrong, false, find, [&](int x, int y, bool ink) { g4[y][x] = ink ? 1 : 2; });
  check(memcmp(g1, g4, sizeof g1) == 0, "an item worn in the wrong place is ignored");

  printf(failures ? "\n%d check(s) failed\n" : "\nall checks passed\n", failures);
  return failures ? 1 : 0;
}
