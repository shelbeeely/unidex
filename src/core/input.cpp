#include "input.h"
#include <Arduino.h>
#include <esp_sleep.h>
#include "board.h"

static const uint32_t DEBOUNCE_MS = 30;  // no bounce seen at this value in STEP 0
static const uint32_t LONG_MS = 300;     // tuned on the device in STEP 2
static const uint32_t RESET_MS = 1000;   // both buttons held this long = restart

struct Button {
  int pin;
  Event shortEv, longEv;
  bool down;
  bool longSent;
  uint32_t changedAt;
};

#if UNIDEX_X4
// Xteink X4: the four front keys share one ADC pin (GPIO1) and the two side keys another (GPIO2), each through a
// resistor ladder that idles at the top of the range; Power is its own pin (GPIO3, active LOW). They map onto the
// two-button scheme: any arrow key (front Left/Right, side Up/Down) is A, Confirm and Power are B, and Back is
// "hold A" straight away. Two keys on the same ladder pin can't be told apart when both are down, so the restart
// is Power (or Confirm) held with a side key. Ranges: CrossPoint Reader's readings from real X4s (MIT).
enum : int { KEY_A = -1, KEY_B = -2, KEY_BACK = -3 };
static const int PIN_LADDER_FRONT = 1, PIN_LADDER_SIDE = 2, PIN_POWER = 3;

static Button buttons[] = {
  {KEY_A, Event::AShort, Event::ALong, false, false, 0},
  {KEY_B, Event::BShort, Event::BLong, false, false, 0},
  {KEY_BACK, Event::ALong, Event::ALong, false, false, 0},  // one "back" per press, however long
};

// Front ladder: Back ~3500, Confirm ~2700, Left ~1500, Right ~0; side: Up ~2250, Down ~0; nothing pressed ~4095.
static int frontKey() {
  const int v = analogRead(PIN_LADDER_FRONT);
  return v > 3900 ? 0 : v > 3100 ? KEY_BACK : v > 2090 ? KEY_B : KEY_A;
}
static bool sideDown() { return analogRead(PIN_LADDER_SIDE) <= 3900; }
static bool powerDown() { return digitalRead(PIN_POWER) == LOW; }

static bool isDown(const Button &b) {
  switch (b.pin) {
    case KEY_A: return sideDown() || frontKey() == KEY_A;
    case KEY_B: return powerDown() || frontKey() == KEY_B;
    default: return frontKey() == KEY_BACK;
  }
}
static bool bDown() { return powerDown() || frontKey() == KEY_B; }

static void pinsInit() {
  pinMode(PIN_LADDER_FRONT, INPUT);
  pinMode(PIN_LADDER_SIDE, INPUT);
  pinMode(PIN_POWER, INPUT_PULLUP);
  analogSetAttenuation(ADC_11db);  // the whole 0-3.3 V range
}

// Which keys can wake the board (power.cpp): Power, and the ladder keys that pull their pin right down (Right, Down).
static bool wokeBy(const Button &b, uint64_t mask) {
  if (b.pin == KEY_A) return mask & ((1ULL << PIN_LADDER_FRONT) | (1ULL << PIN_LADDER_SIDE));
  if (b.pin == KEY_B) return mask & (1ULL << PIN_POWER);
  return false;
}
static bool buttonWoke() { return esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_GPIO; }
static uint64_t wakeMask() { return esp_sleep_get_gpio_wakeup_status(); }
#else
// Both buttons are active LOW. GPIO0 is a strapping pin, but only at reset, so it's a normal input here.
static Button buttons[] = {
  {0, Event::AShort, Event::ALong, false, false, 0},
  {18, Event::BShort, Event::BLong, false, false, 0},
};
static bool isDown(const Button &b) { return digitalRead(b.pin) == LOW; }
static bool bDown() { return digitalRead(18) == LOW; }
static void pinsInit() {
  for (Button &b : buttons) pinMode(b.pin, INPUT_PULLUP);
}
static bool wokeBy(const Button &b, uint64_t mask) { return mask & (1ULL << b.pin); }
static bool buttonWoke() { return esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT1; }
static uint64_t wakeMask() { return esp_sleep_get_ext1_wakeup_status(); }
#endif

static Event pending = Event::None;  // a wake tap that was over before we could see it
static bool resetSent;

void inputInit() {
  // After a deep-sleep wake the press that woke us counts as input. After power-on it doesn't:
  // holding PWR to switch on would otherwise open an app.
  const bool buttonWake = buttonWoke();
  const uint64_t wokeMask = buttonWake ? wakeMask() : 0;
  pinsInit();
  for (Button &b : buttons) {
    const bool held = isDown(b);
    b.down = held;
    b.longSent = held && !buttonWake;  // power-on press: its release produces nothing
    b.changedAt = 0;                   // a held wake press started at about boot time
    if (!held && wokeBy(b, wokeMask)) pending = b.shortEv;  // tapped and released during boot
  }
}

#if UNIDEX_DEV
static uint32_t virtualHoldUntil;
void inputVirtualHoldB(uint32_t ms) { virtualHoldUntil = millis() + ms; }
bool inputBHeld() { return bDown() || (int32_t)(virtualHoldUntil - millis()) > 0; }
#else
bool inputBHeld() { return bDown(); }
#endif

bool inputAnyDown() {
  for (Button &b : buttons)
    if (b.down) return true;
  return false;
}

Event inputPoll() {
  if (pending != Event::None) {
    Event e = pending;
    pending = Event::None;
    return e;
  }
  uint32_t now = millis();
  // Both held: neither gives its own events (no "home" on the way to a restart), and after 3 s: Reset.
  Button &a = buttons[0], &b = buttons[1];
  if (a.down && b.down) {
    a.longSent = b.longSent = true;
    if (!resetSent && now - max(a.changedAt, b.changedAt) >= RESET_MS) {
      resetSent = true;
      return Event::Reset;
    }
  } else {
    resetSent = false;
  }
  for (Button &b : buttons) {
    bool pressed = isDown(b);
    if (pressed != b.down && now - b.changedAt > DEBOUNCE_MS) {
      b.down = pressed;
      b.changedAt = now;
      if (pressed) {
        b.longSent = false;
      } else if (!b.longSent) {
        return b.shortEv;
      }
    }
    // Long fires while still held, so the user knows when to let go.
    if (b.down && !b.longSent && now - b.changedAt >= LONG_MS) {
      b.longSent = true;
      return b.longEv;
    }
  }
  return Event::None;
}
