// Pet: a small creature you dress up (body, eyes, mouth, hat, extra), saved in NVS as one number ("pet_look").
// Main screen: A = say hi (happy eyes and a heart), B = edit, hold B = meet other Pets (meet.cpp), hold A = back.
// It blinks every few seconds for a minute after the last press. Edit: A = next row, B = change it (next option;
// Random: a new look; Done: save), hold B = previous option, hold A = save and back.
#include <esp_random.h>
#include "meet.h"
#include "pet.h"
#include "pet_logic.h"
#include "../house/house.h"
#include "../../core/app.h"
#include "../../core/devtools.h"
#include "../../core/display.h"
#include "../../core/power.h"
#include "../../core/storage.h"
#include "../../core/theme.h"

static const int MAIN_SCALE = 4, EDIT_SCALE = 3;  // 128 px on the main screen, 96 px while editing
static const int16_t EDIT_AVATAR_Y = CONTENT_TOP + 4, ROW_Y = HINTS_TOP - 14;
// The editor's rows: a layer (0 and up), or one of these. On the X4 (RoomiePet) clothes come first, and there are
// rows for the Pet's pronouns (pronouns.h's PRESETS) and whether it wears its pronoun pin.
enum : int8_t { PRONOUNS_ROW = -1, NAME_ROW = -2, RANDOM_ROW = -3, DONE_ROW = -4, PIN_ROW = -5 };
#if UNIDEX_X4
static const int8_t ROW_OF[] = {pet::OUTFIT, pet::HAIR,  pet::HAT,    pet::EXTRA, pet::BODY,  pet::EYES_LAYER,
                                pet::MOUTH,  PRONOUNS_ROW, PIN_ROW, NAME_ROW,   RANDOM_ROW, DONE_ROW};
#else
static const int8_t ROW_OF[] = {pet::BODY, pet::EYES_LAYER, pet::MOUTH, pet::HAT,  pet::EXTRA,
                                pet::HAIR, pet::OUTFIT,     NAME_ROW,   RANDOM_ROW, DONE_ROW};
#endif
static const int ROWS = sizeof ROW_OF;

enum Screen : uint8_t { MAIN, EDIT, MEET, NAME };
RTC_DATA_ATTR static uint8_t screen, row;
RTC_DATA_ATTR static uint32_t draft;  // the look being edited (kept through sleep)
RTC_DATA_ATTR static char places[pet::MAX_NAME + 1];  // the name being edited, a letter a place (pet_logic.h)
RTC_DATA_ATTR static uint8_t place;                    // the place being chosen
static bool blinking, happy;
static uint8_t heart;  // the heart beside it while it's happy: 0 none, else its scale (it pops: 1, then 2)
static uint32_t nextBlinkAt, openAt, lastPressAt;

static const int16_t MAIN_Y = CONTENT_TOP + (HINTS_TOP - CONTENT_TOP - pet::SIZE * MAIN_SCALE) / 2;  // centred
static const uint32_t AWAKE_MS = 60000;  // blinking stops this long after the last press

static const char *const HEART[] = {  // beside its head while it's happy
  ".##...##.",
  "####.####",
  "#########",
  "#########",
  ".#######.",
  "..#####..",
  "...###...",
  "....#....",
};

uint32_t petLookBits() { return storageGetInt("pet_look", pet::pack(pet::DEFAULT_LOOK)); }

uint32_t petExp() { return storageGetInt("pet_exp", 0); }

static pet::Owned owned() {
  pet::Owned o = {};
  storageGetBytes("pet_owned", &o, sizeof o);
  return o;
}

void petOwned(uint32_t out[7]) {
  const pet::Owned o = owned();
  memcpy(out, o.layer, sizeof o.layer);
}

static uint32_t randomNumber() { return devSeed() ? (uint32_t)rand() : esp_random(); }  // test build: repeatable

const char *petChoreDone(bool onTime, bool helped, bool rescued) {
  storagePutInt("pet_exp", petExp() + (onTime ? 15 : 10));  // EXP: levels only
  // The days in a row with a chore done, and whether this is today's first.
  const uint32_t today = houseToday(), last = storageGetInt("pet_day", 0);
  int streak = storageGetInt("pet_streak", 0);
  const bool firstToday = today && last != today;
  if (today && last != today) {
    streak = last + 1 == today ? streak + 1 : 1;
    storagePutInt("pet_day", today);
    storagePutInt("pet_streak", streak);
  }
  pet::ChoreLuck luck = {onTime, helped, rescued, firstToday, (uint8_t)(streak > 1 ? streak - 1 : 0),
                         (uint8_t)storageGetInt("pet_dry", 0)};
  pet::Owned o = owned();
  const pet::Drop *d = pet::rollDrop(luck, o, randomNumber(), randomNumber(), randomNumber());
  if (!d) {
    if (pet::giftsOwned(o) < pet::DROP_COUNT) storagePutInt("pet_dry", luck.dry + 1);
    return nullptr;
  }
  pet::give(o, *d);
  storagePutBytes("pet_owned", &o, sizeof o);
  storagePutInt("pet_dry", 0);
  static char gift[48];
  snprintf(gift, sizeof gift, "Gift! %s (%s)", d->what, pet::RARITY_NAMES[d->rarity]);
  char news[64];
  snprintf(news, sizeof news, "%s Dress me up", gift);
  houseAddNews(news);
  return gift;
}

String petName() { return storageGetString("pet_name"); }

String petPronouns() { return UNIDEX_X4 ? storageGetString("pet_pronouns") : String(); }

// The pronoun pin over a Pet drawn at `scale` with its top left at x0, y0 (pronouns.h draws it at half a Pet pixel,
// so it needs a scale of 2 or more).
static void drawPin(const char *pronouns, int scale, int16_t x0, int16_t y0) {
  if (!pronouns || !*pronouns || scale < 2) return;
  pronouns::drawPin(pronouns, [&](int x, int y, bool ink) {
    const int16_t left = x * scale / 2, top = y * scale / 2;
    display.fillRect(x0 + left, y0 + top, (x + 1) * scale / 2 - left, (y + 1) * scale / 2 - top, ink ? BLACK : WHITE);
  });
}

static uint32_t changes;  // counts saves from the website; the screen and the home line each notice a new one

void petChanged() { changes++; }

void petDraw(uint32_t look, int scale, int16_t x0, int16_t y0, PetMood mood, const char *pronouns) {
  pet::Look l = pet::unpack(look);
  if (mood == PetMood::Happy) l.part[pet::EYES_LAYER] = pet::HAPPY_EYES;
  pet::drawLook(l, mood == PetMood::Blink, [&](int x, int y, bool ink) {
    display.fillRect(x0 + x * scale, y0 + y * scale, scale, scale, ink ? BLACK : WHITE);
  });
  if (l.pin) drawPin(pronouns, scale, x0, y0);
}

void petDrawTag(const char *name, int16_t cx, int16_t bottom) {
  if (!name || !*name) return;
  display.setFont(FONT_TINY);
  const int16_t w = textWidth(name) + 8, h = 14, x = cx - w / 2, y = bottom - h;
  display.fillRoundRect(x - 1, y - 1, w + 2, h + 2, 4, WHITE);  // a white rim keeps it clear of what's behind
  display.fillRoundRect(x, y, w, h, 3, BLACK);
  display.setTextColor(WHITE);
  display.setCursor(x + 4, y + 10);
  display.print(name);
  display.setTextColor(BLACK);
}

void petDrawBubble(const char *text, int16_t cx, int16_t bottom) {
  if (!text || !*text) return;
  display.setFont(FONT_TINY);
  const int16_t w = textWidth(text) + 10, h = 15, y = bottom - 4 - h;
  int16_t x = cx - w / 2;
  x = max<int16_t>(1, min<int16_t>(x, display.width() - 1 - w));
  display.fillRoundRect(x, y, w, h, 5, WHITE);
  display.drawRoundRect(x, y, w, h, 5, BLACK);
  const int16_t tx = max<int16_t>(x + 6, min<int16_t>(cx, x + w - 7));  // the tail, under the speaker
  display.fillTriangle(tx - 3, y + h - 1, tx + 3, y + h - 1, tx, bottom, WHITE);
  display.drawLine(tx - 3, y + h - 1, tx, bottom, BLACK);
  display.drawLine(tx + 3, y + h - 1, tx, bottom, BLACK);
  display.setCursor(x + 5, y + 11);
  display.print(text);
}

void petDrawHeart(int16_t cx, int16_t cy, int scale) {
  const int16_t left = cx - 9 * scale / 2, top = cy - 4 * scale;
  for (int y = 0; y < 8; y++)
    for (int x = 0; x < 9; x++)
      if (HEART[y][x] == '#') display.fillRect(left + x * scale, top + y * scale, scale, scale, BLACK);
}

static void drawAvatar(const pet::Look &look, int scale, int16_t y0, bool eyesClosed = false) {
  const int16_t x0 = (display.width() - pet::SIZE * scale) / 2;
  pet::drawLook(look, eyesClosed, [&](int x, int y, bool ink) {
    display.fillRect(x0 + x * scale, y0 + y * scale, scale, scale, ink ? BLACK : WHITE);
  });
  if (look.pin) drawPin(petPronouns().c_str(), scale, x0, y0);
}

static void save() { storagePutInt("pet_look", pet::pack(pet::unpack(draft))); }  // skips the write if unchanged


// What the Pet says about the household, if anything: a roommate's news first (it cheers them on), then its own
// chores (proud when done today, sulking while one is late).
static bool householdSay(char *say, size_t len, HouseFeeling &feeling) {
  if (houseNews(say, len)) {
    feeling = HouseFeeling::Proud;
    return true;
  }
  const HouseMood mood = houseMood();
  feeling = mood.feeling;
  strlcpy(say, mood.say, len);
  return say[0] != 0;
}

static void drawMain() {
  const String name = petName(), pronouns = petPronouns();
  char right[48];  // its name, pronouns and level (chores give EXP: petChoreDone)
  snprintf(right, sizeof right, "%s%s%s%s%sLv %d", name.c_str(), pronouns.length() ? " (" : "", pronouns.c_str(),
           pronouns.length() ? ")" : "", name.length() || pronouns.length() ? "  " : "", pet::levelOf(petExp()));
  drawHeader("Pet", fitText(right, display.width() - 40).c_str());
  pet::Look look = pet::unpack(petLookBits());
  char say[64];
  HouseFeeling feeling = HouseFeeling::Normal;
  const bool talking = householdSay(say, sizeof say, feeling);
  if (happy || feeling == HouseFeeling::Proud) look.part[pet::EYES_LAYER] = pet::HAPPY_EYES;
  const bool eyesShut = blinking || (feeling == HouseFeeling::Sulky && !happy);  // sulking: eyes down
  if (talking) {  // a little smaller, with its words over its head
    const int16_t bubbleBottom = CONTENT_TOP + 26;
    petDrawBubble(fitText(say, display.width() - 12).c_str(), display.width() / 2, bubbleBottom);
    drawAvatar(look, MAIN_SCALE - 1, bubbleBottom + 2, eyesShut);
  } else {
    drawAvatar(look, MAIN_SCALE, MAIN_Y, eyesShut);
  }
  if (heart) petDrawHeart(175, CONTENT_TOP + 16 + (talking ? 30 : 0), heart);  // beside the head
  drawHints("hi", "dress up", "meet");
}

// A: it stays put (moving leaves smears on e-ink) and gets happy eyes while a heart pops up beside it, small then
// big; then back to normal with a refresh that drives every pixel, so nothing is left behind.
static Redraw sayHi() {
  powerHold();
  happy = true;
  heart = 1;
  displayFrame(drawMain);
  heart = 2;
  displayFrame(drawMain);
  delay(700);
  happy = false;
  heart = 0;
  houseNewsSeen();  // it has passed on the household's news
  displayClean(drawMain);
  powerRelease();
  return Redraw::None;
}

static void onEnter() {
  screen = MAIN;
  lastPressAt = millis();
}

static Redraw onButton(Event e) {
  lastPressAt = millis();
  if (screen == MEET) return meetButton(e);
  if (screen == MAIN) {
    if (e == Event::AShort) return sayHi();
    if (e == Event::BLong) {
      screen = MEET;
      meetEnter();
      return Redraw::Full;
    }
    if (e != Event::BShort) return Redraw::None;
    draft = petLookBits();
    row = 0;
    screen = EDIT;
    return Redraw::Full;  // the avatar changes size: a full refresh leaves no ghost
  }
  if (screen == NAME) {
    if (e == Event::AShort || e == Event::BLong) {
      places[place] = pet::stepChar(places[place], e == Event::AShort ? 1 : -1);
    } else if (e == Event::BShort && pet::nameDoneAt(places, place)) {
      char name[pet::MAX_NAME + 1];
      pet::nameFromPlaces(places, name);
      if (*name) storagePutString("pet_name", name);
      else storageRemoveKey("pet_name");
      petChanged();
      screen = EDIT;
    } else if (e == Event::BShort) {
      place++;
    } else {
      return Redraw::None;
    }
    return Redraw::Partial;
  }
  pet::Look look = pet::unpack(draft);
  const int kind = ROW_OF[row];
  if (e == Event::BShort && kind == NAME_ROW) {
    pet::placesFromName(petName().c_str(), places);
    place = 0;
    screen = NAME;
    return Redraw::Partial;
  }
  if (e == Event::AShort) {
    row = (row + 1) % ROWS;
  } else if (e == Event::BShort && kind == DONE_ROW) {
    save();
    screen = MAIN;
    return Redraw::Full;
  } else if ((e == Event::BShort || e == Event::BLong) && kind == PRONOUNS_ROW) {
    // The next (or previous) of the presets; pronouns typed in the web app count as before the first.
    const String now = petPronouns();
    int at = -1;
    for (int i = 0; i < pronouns::PRESET_COUNT; i++)
      if (now == pronouns::PRESETS[i]) at = i;
    const int n = pronouns::PRESET_COUNT;
    at = at < 0 ? (e == Event::BShort ? 0 : n - 1) : ((at + (e == Event::BShort ? 1 : -1)) % n + n) % n;
    if (*pronouns::PRESETS[at]) storagePutString("pet_pronouns", pronouns::PRESETS[at]);
    else storageRemoveKey("pet_pronouns");
    petChanged();
  } else if (e == Event::BShort && kind == PIN_ROW) {
    look.pin = !look.pin;
  } else if (e == Event::BShort && kind == RANDOM_ROW) {
    if (devSeed()) srand(devSeed() + draft);
    const bool pin = look.pin;
    look = pet::randomOwned(randomNumber, owned());
    look.pin = pin;
  } else if ((e == Event::BShort || e == Event::BLong) && kind >= 0) {
    pet::stepOwned(look, kind, e == Event::BShort ? 1 : -1, owned());  // gifts not found yet are skipped
  } else {
    return Redraw::None;
  }
  draft = pet::pack(look);
  return Redraw::Partial;
}

static Redraw onBack() {
  lastPressAt = millis();
  if (screen == MAIN) return Redraw::Exit;
  if (screen == MEET) {
    if (meetBack()) return Redraw::Partial;  // the friends list or its question closes first
    meetLeave();
    screen = MAIN;
    return Redraw::Full;
  }
  if (screen == NAME) {  // back without changing the name
    screen = EDIT;
    return Redraw::Partial;
  }
  save();
  screen = MAIN;
  return Redraw::Full;
}

static void drawEdit() {
  const pet::Look look = pet::unpack(draft);
  char gifts[24];  // gifts found so far (the rest are skipped)
  snprintf(gifts, sizeof gifts, "%d/%d gifts", pet::giftsOwned(owned()), pet::DROP_COUNT);
  drawHeader("Dress up", gifts);
  drawAvatar(look, EDIT_SCALE, EDIT_AVATAR_Y);
  for (int r = 0; r < ROWS; r++) {  // which row of the editor this is: one dot each
    const int16_t x = display.width() / 2 + (r * 2 - (ROWS - 1)) * 5, y = EDIT_AVATAR_Y + pet::SIZE * EDIT_SCALE + 5;
    if (r == row) display.fillCircle(x, y, 3, BLACK);
    else display.drawPixel(x, y, BLACK), display.drawCircle(x, y, 1, BLACK);
  }
  display.setFont(FONT_SMALL);
  const int kind = ROW_OF[row];
  char right[24];
  display.setCursor(MARGIN, ROW_Y);
  if (kind >= 0) {
    // The part, and where it is among the ones this Pet has.
    const pet::LayerParts lp = pet::partsOf(kind);
    const pet::Owned o = owned();
    int at = 0, of = 0;
    for (int i = 0; i < lp.count; i++)
      if (pet::owns(o, kind, i)) {
        of++;
        if (i <= look.part[kind]) at = of;
      }
    display.print(pet::LAYER_NAMES[kind]);
    snprintf(right, sizeof right, "%s  %d/%d", lp.parts[look.part[kind]].name, at, of);
    drawRight(right, ROW_Y);
    drawHints("next", "change", "previous");
  } else if (kind == PRONOUNS_ROW) {
    display.print("Pronouns");
    const String pr = petPronouns();
    drawRight(pr.length() ? pr.c_str() : "none", ROW_Y);
    drawHints("next", "change", "previous");
  } else if (kind == PIN_ROW) {
    display.print("Pronoun pin");
    drawRight(!look.pin ? "off" : petPronouns().length() ? "on" : "on (no pronouns)", ROW_Y);
    drawHints("next", look.pin ? "take off" : "put on", "");
  } else if (kind == NAME_ROW) {
    display.print("Name");
    const String name = petName();
    drawRight(name.length() ? name.c_str() : "none", ROW_Y);
    drawHints("next", "change", "");
  } else if (kind == RANDOM_ROW) {
    display.print("Random look");
    drawHints("next", "shuffle", "");
  } else {
    display.print("Done");
    drawHints("next", "save", "");
  }
}

// The name a letter at a time: the place being chosen in a black box (a blank place shows as a short line).
static void drawName() {
  drawHeader("Name");
  display.setFont(FONT_MEDIUM);
  int len = pet::MAX_NAME;
  while (len > 0 && places[len - 1] == ' ') len--;
  const int shown = max(len, place + 1);
  int16_t widths[pet::MAX_NAME], total = 0;
  for (int i = 0; i < shown; i++) {
    const char c[2] = {places[i], 0};
    widths[i] = places[i] == ' ' ? 10 : textWidth(c);
    total += widths[i] + 2;
  }
  const int16_t baseline = CONTENT_TOP + 62;
  int16_t x = (display.width() - total) / 2;
  for (int i = 0; i < shown; i++) {
    const char c[2] = {places[i], 0};
    if (i == place) {
      display.fillRoundRect(x - 2, baseline - 21, widths[i] + 4, 28, 3, BLACK);
      display.setTextColor(WHITE);
    } else if (places[i] == ' ') {
      display.drawFastHLine(x + 1, baseline + 2, widths[i] - 2, BLACK);
    }
    display.setCursor(x, baseline);
    display.print(c);
    display.setTextColor(BLACK);
    x += widths[i] + 2;
  }
  display.setFont(FONT_SMALL);
  const bool done = pet::nameDoneAt(places, place);
  char counter[24];
  snprintf(counter, sizeof counter, "letter %d of %d", place + 1, pet::MAX_NAME);
  display.setFont(FONT_TINY);
  if (done && len) snprintf(counter, sizeof counter, "%d letter%s", len, len == 1 ? "" : "s");
  drawCentered(done && !len ? "no name" : counter, CONTENT_TOP + 104);
  drawHints("letter", done ? "done" : "next", "previous");
}

static void draw() {
  if (screen == NAME) drawName();
  else if (screen == EDIT) drawEdit();
  else if (screen == MEET) meetDraw();
  else drawMain();
}

#if UNIDEX_DEV
// Test build (X STATE detail): "main:<saved look>" or "edit:<row>:<look being edited>", looks in hex.
static const char *detail() {
  static char buf[32];
  if (screen == MEET) return meetDetail();
  if (screen == NAME) {
    char name[pet::MAX_NAME + 1];
    pet::nameFromPlaces(places, name);
    snprintf(buf, sizeof buf, "name:%d:%s", place, name);
    return buf;
  }
  if (screen == EDIT) snprintf(buf, sizeof buf, UNIDEX_X4 ? "edit:%d:%08lx" : "edit:%d:%07lx", row, (unsigned long)pet::pack(pet::unpack(draft)));
  else snprintf(buf, sizeof buf, UNIDEX_X4 ? "main:%08lx" : "main:%07lx", (unsigned long)pet::pack(pet::unpack(petLookBits())));
  return buf;
}
#endif

static Redraw tick() {
#if UNIDEX_DEV
  devSetDetail(detail);  // here rather than onEnter, which a wake from sleep skips
#endif
  static uint32_t seen;
  if (seen != changes) {  // a new look or name from the website
    seen = changes;
    if (screen == EDIT) draft = petLookBits();
    return Redraw::Partial;
  }
  if (screen == MEET) return meetTick();
#if UNIDEX_DEV
  if (devManualFrames()) return Redraw::None;  // test build: no blinking, for repeatable screenshots
#endif
  // Blink: eyes shut for one refresh every 2.5-6 s. On battery the device naps between presses, so it asks to
  // be woken in time for the next one.
  const uint32_t now = millis();
  if (screen != MAIN) {
    blinking = false;
    return Redraw::None;
  }
  if (blinking) {
    if ((int32_t)(now - openAt) < 0) {
      powerWakeWithin(openAt - now);
      return Redraw::None;
    }
    blinking = false;
    nextBlinkAt = now + 2500 + esp_random() % 3500;
    return Redraw::Tick;
  }
  if (now - lastPressAt > AWAKE_MS) return Redraw::None;
  if (nextBlinkAt == 0) nextBlinkAt = now + 2500 + esp_random() % 3500;
  if ((int32_t)(now - nextBlinkAt) < 0) {
    powerWakeWithin(nextBlinkAt - now);
    return Redraw::None;
  }
  blinking = true;
  openAt = now + 120;
  return Redraw::Tick;
}

static void onExit() {
  if (screen == MEET) {  // leaving the app (e.g. a new badge opened the Badge app): radio off
    meetLeave();
    screen = MAIN;
  }
#if UNIDEX_DEV
  devSetDetail(nullptr);
#endif
}

static void status(char *out, size_t len) {
  static int8_t named = -1;  // looked up once, and again after a change (not on every redraw of the home screen)
  static uint32_t seen;
  static char name[16];
  if (named < 0 || seen != changes) {
    seen = changes;
    strlcpy(name, petName().c_str(), sizeof name);
    named = name[0] != 0;
  }
  char news[48];
  if (houseNews(news, sizeof news)) {
    strlcpy(out, news, len);
    return;
  }
  const int friends = meetFriendCount();
  if (friends && named) snprintf(out, len, "%s, %d friend%s", name, friends, friends == 1 ? "" : "s");
  else if (friends) snprintf(out, len, "%d friend%s", friends, friends == 1 ? "" : "s");
  else if (named) snprintf(out, len, "Say hi to %s!", name);
  else snprintf(out, len, "Say hi!");
}

extern const App petApp = {"Pet", ICON_PET, onEnter, onButton, draw, onExit, onBack, status, tick};
