#include "held.h"
#include <Arduino.h>
#include "../../core/board.h"
#include "../../core/link.h"
#include "../../core/storage.h"
#include "../../core/usbsync.h"

#if UNIDEX_X4
#include "items.h"

static const char *const FILE_PATH = "/pet/items.bin", *const FILE_TMP = "/pet/items.tmp";
static const uint8_t FILE_MAGIC[4] = {'R', 'P', 'I', 'T'}, FILE_VERSION = 2;
static const uint32_t DIFFER_MS = 5000;  // how long a different digest keeps us sending

static items::Catalog *catalog;
static uint32_t turn, differAt;
static int received;

// The card's file: magic, version, count, then the items as items_logic.h's Item, one after another.
static void load() {
  catalog = new items::Catalog();
  catalog->builtin = BUILTIN_ITEMS;
  catalog->builtinCount = BUILTIN_ITEM_COUNT;
  fs::File f = storageOpen(FILE_PATH);
  uint8_t head[6];
  if (!f || f.read(head, 6) != 6 || memcmp(head, FILE_MAGIC, 4) != 0 || head[4] != FILE_VERSION) return;
  for (int i = 0; i < head[5] && i < items::MAX_ADDED; i++) {
    items::Item it;
    if (f.read((uint8_t *)&it, sizeof it) != sizeof it) break;
    it.name[items::NAME] = 0;
    catalog->add(it);  // checked: a damaged one is skipped
  }
}

static void save() {
  fs::File f = storageOpen(FILE_TMP, "w");
  if (!f) return;
  const uint8_t head[6] = {FILE_MAGIC[0], FILE_MAGIC[1], FILE_MAGIC[2], FILE_MAGIC[3], FILE_VERSION,
                           (uint8_t)catalog->addedCount};
  bool ok = f.write(head, 6) == 6;
  for (int i = 0; ok && i < catalog->addedCount; i++)
    ok = f.write((const uint8_t *)&catalog->added[i], sizeof(items::Item)) == sizeof(items::Item);
  f.close();
  if (ok) storageReplace(FILE_TMP, FILE_PATH, 6 + catalog->addedCount * sizeof(items::Item));
}

const items::Catalog &itemsCatalog() {
  if (!catalog) load();
  return *catalog;
}

const items::Item *itemsFind(uint32_t id) { return id ? itemsCatalog().find(id) : nullptr; }

bool itemsAdd(const items::Item &it) {
  itemsCatalog();
  if (!catalog->add(it)) return false;
  save();
  return true;
}

bool itemsRemove(uint32_t id) {
  itemsCatalog();
  if (!catalog->remove(id)) return false;
  save();
  return true;
}

uint32_t itemsDigest() { return itemsCatalog().digest(); }

bool itemsHeard(const uint8_t *p, size_t len) {
  if (!items::isItemPacket(p, len)) return false;
  items::Item it;
  if (items::decode(p, len, it) && itemsAdd(it)) received++;
  return true;
}

void itemsNoteDigest(uint32_t theirs) {
  if (theirs && theirs != itemsDigest()) differAt = millis() | 1;
}

void itemsBeat() {
  if (!differAt || millis() - differAt > DIFFER_MS) return;
  const int i = items::sendOrder(itemsCatalog(), turn++);
  if (i < 0) return;
  uint8_t packet[items::PACKET_MAX];
  linkBroadcast(packet, items::encode(itemsCatalog().at(i), packet));
}

int itemsReceived() { return received; }

static int hexValue(char c) {
  return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
}
static bool fromHex(const char *hex, size_t hexLen, uint8_t *out, size_t n) {
  if (hexLen != 2 * n) return false;
  for (size_t i = 0; i < n; i++) {
    const int hi = hexValue(hex[2 * i]), lo = hexValue(hex[2 * i + 1]);
    if (hi < 0 || lo < 0) return false;
    out[i] = hi << 4 | lo;
  }
  return true;
}

bool itemsUsb(const char *line) {
  if (strncmp(line, "I ", 2) != 0) return false;
  const char *cmd = line + 2;
  Print &out = usbOut();
  if (strcmp(cmd, "LIST") == 0) {
    const items::Catalog &c = itemsCatalog();
    for (int i = 0; i < c.count(); i++) {
      const items::Item &it = c.at(i);
      out.printf("IT %06lx ", (unsigned long)it.id);
      for (const char *ch = it.name; *ch; ch++) out.printf("%02x", (uint8_t)*ch);
      out.printf(" %d %d %d %d ", it.kind, it.shape, it.top, it.rows);
      for (int k = 0; k < items::dataLen(it); k++) out.printf("%02x", it.data[k]);
      out.printf(" %d\n", i >= c.builtinCount);
    }
    out.printf("OK I LIST %d %08lx\n", c.count(), (unsigned long)c.digest());
  } else if (strncmp(cmd, "ADD ", 4) == 0) {
    // I ADD <name hex> <kind> <shape> <top> <rows> <picture hex>
    char nameHex[2 * items::NAME + 2] = {}, dataHex[2 * items::DATA + 2] = {};
    int kind, shape, top, rows;
    uint8_t nameBytes[items::NAME + 1] = {};
    items::Item it = {};
    const bool parsed = sscanf(cmd + 4, "%25s %d %d %d %d %433s", nameHex, &kind, &shape, &top, &rows, dataHex) == 6;
    const size_t nameLen = strlen(nameHex);
    if (parsed && kind >= 0 && kind < items::KINDS && (shape == items::ICON || shape == items::PIECE) && top >= 0 &&
        top < 32 && rows >= 0 && rows <= items::MAX_ROWS) {
      it.kind = kind, it.shape = shape, it.top = shape == items::ICON ? 0 : top;
      it.rows = shape == items::ICON ? items::SIDE : rows;
    }
    if (!parsed || !items::shapeOk(it) || nameLen % 2 || nameLen / 2 > (size_t)items::NAME ||
        !fromHex(nameHex, nameLen, nameBytes, nameLen / 2) || !items::nameOk((const char *)nameBytes) ||
        !fromHex(dataHex, strlen(dataHex), it.data, items::dataLen(it))) {
      out.println("ERR");
      return true;
    }
    it = it.shape == items::ICON ? items::makeIcon((const char *)nameBytes, it.kind, it.data)
                                 : items::makePiece((const char *)nameBytes, it.kind, it.top, it.rows, it.data);
    if (!itemsAdd(it) && !itemsFind(it.id)) {
      out.println("ERR");  // full
      return true;
    }
    out.printf("OK I ADD %06lx\n", (unsigned long)it.id);
  } else if (strncmp(cmd, "DEL ", 4) == 0) {
    out.println(itemsRemove(strtoul(cmd + 4, nullptr, 16)) ? "OK I DEL" : "ERR");
  } else {
    out.println("ERR");
  }
  return true;
}

#else  // the Waveshare board: no catalog

// (itemsCatalog isn't here: nothing on this board shows a catalog.)
const items::Item *itemsFind(uint32_t) { return nullptr; }
bool itemsAdd(const items::Item &) { return false; }
bool itemsRemove(uint32_t) { return false; }
uint32_t itemsDigest() { return 0; }
bool itemsHeard(const uint8_t *, size_t) { return false; }
void itemsNoteDigest(uint32_t) {}
void itemsBeat() {}
int itemsReceived() { return 0; }
bool itemsUsb(const char *) { return false; }

#endif
