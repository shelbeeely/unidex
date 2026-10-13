// Home (RoomiePet): the virtual household, a room at a time (world_logic.h, drawn by world_draw.h). The foundation's
// simple version: A = next room, hold A = back. Away from the home network, only this Pet's bedroom.
#include "../../core/app.h"
#include "../../core/clock.h"
#include "../../core/display.h"
#include "../../core/theme.h"
#include "../house/home.h"
#include "../house/house.h"
#include "../house/plan.h"
#include "../house/world_draw.h"
#include "../pet/pet.h"

#if UNIDEX_X4

RTC_DATA_ATTR static uint8_t current;  // the room shown
static plan::Room rooms[plan::MAX_ROOMS];
static world::Scene scene;

// The household now: the plan, the records, who's home, whether this X4 is at home.
static void build() {
  const int n = houseLoad() ? planRooms(rooms, plan::MAX_ROOMS) : 0;
  home::AtHome at[8];
  uint32_t home[world::MAX_PETS];
  const int k = homeAtHome(at, 7);
  for (int i = 0; i < k; i++) home[i] = at[i].pet;
  home[k] = petId();
  const bool away = !homeOnHomeNetwork();
  int minute = 0;
  if (clockValid()) {
    const struct tm t = clockLocal();
    minute = t.tm_hour * 60 + t.tm_min;
  }
  const world::Input in = {rooms, n, &houseData(), houseToday(), minute, home, k + 1, petId(), away};
  world::build(scene, in);
  if (scene.away && scene.awayRoom >= 0) current = scene.awayRoom;
  if (current >= scene.count) current = 0;
}

static void onEnter() { build(); }

static void draw() {
  build();
  if (!scene.count) {
    drawHeader("Home");
    drawEmpty("No home yet", "Join a household first");
    drawHints("", "", "");
    return;
  }
  if (scene.away && scene.awayRoom < 0) {
    drawHeader("Home", "away");
    drawEmpty("Away from home", "Set up your bedroom", "in the web app");
    drawHints("", "", "");
    return;
  }
  const plan::Room &r = rooms[current];
  char right[16];
  snprintf(right, sizeof right, "mess %d%%", scene.state[current].mess);
  drawHeader(r.name[0] ? r.name : plan::TYPE_NAMES[r.type], right);
  const int tile = r.w * 16 <= 192 && r.h * 16 <= HINTS_TOP - CONTENT_TOP - 4 ? 16 : 8;
  const int16_t x0 = (display.width() - r.w * tile) / 2, y0 = CONTENT_TOP + 2;
  world::drawRoom(r, scene.state[current], tile, x0, y0,
                  [](int x, int y, bool ink) { display.drawPixel(x, y, ink ? BLACK : WHITE); });
  char health[24];
  snprintf(health, sizeof health, "House %d%%", scene.health);
  drawHints(scene.away ? "" : "next", "", "");
  (void)health;
}

static Redraw onButton(Event e) {
  if (e == Event::AShort && !scene.away && scene.count) {
    current = (current + 1) % scene.count;
    return Redraw::Partial;
  }
  return Redraw::None;
}

static void status(char *out, size_t len) { snprintf(out, len, "House %d%%", scene.health); }

extern const App homeApp = {"Home", ICON_HOUSE, onEnter, onButton, draw, nullptr, nullptr, status, nullptr, true};

#endif
